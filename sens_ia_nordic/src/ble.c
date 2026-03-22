#include "ble.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <string.h>

/* UUIDs attendus par l'application Sens IA (service P2P STM32 + Heart Rate). */
#define BT_UUID_P2P_SERVICE_VAL BT_UUID_128_ENCODE(0x0000fe40, 0xcc7a, 0x482a, 0x984a, 0x7f2ed5b3e58f)
#define BT_UUID_LED_CHAR_VAL    BT_UUID_128_ENCODE(0x0000fe41, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19)
#define BT_UUID_SWITCH_CHAR_VAL BT_UUID_128_ENCODE(0x0000fe42, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19)
#define BT_UUID_LONG_CHAR_VAL   BT_UUID_128_ENCODE(0x0000fe43, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19)
#define BT_UUID_FILE_CTRL_VAL   BT_UUID_128_ENCODE(0x0000fe44, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19)
#define BT_UUID_FILE_DATA_VAL   BT_UUID_128_ENCODE(0x0000fe45, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19)

static struct bt_uuid_128 uuid_p2p_service = BT_UUID_INIT_128(BT_UUID_P2P_SERVICE_VAL);
static struct bt_uuid_128 uuid_led_char = BT_UUID_INIT_128(BT_UUID_LED_CHAR_VAL);
static struct bt_uuid_128 uuid_switch_char = BT_UUID_INIT_128(BT_UUID_SWITCH_CHAR_VAL);
static struct bt_uuid_128 uuid_long_char = BT_UUID_INIT_128(BT_UUID_LONG_CHAR_VAL);
static struct bt_uuid_128 uuid_file_ctrl_char = BT_UUID_INIT_128(BT_UUID_FILE_CTRL_VAL);
static struct bt_uuid_128 uuid_file_data_char = BT_UUID_INIT_128(BT_UUID_FILE_DATA_VAL);

static struct bt_conn *active_conn;
static bool switch_notify_enabled;
static bool long_notify_enabled;
static bool file_data_notify_enabled;
static bool hrm_notify_enabled;

static bool stream_enabled = false;
static uint8_t selected_activity_index;

static uint8_t led_ctrl_value[2] = {0x00, 0x00};
static uint8_t body_sensor_location = 0x01; /* chest */

#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#define HAS_STATUS_LED 1
#else
#define HAS_STATUS_LED 0
#endif
static bool status_led_ready;

static int notify_file_data(const uint8_t *data, uint16_t len);

enum p2p_attr_index {
    P2P_ATTR_PRIMARY = 0,
    P2P_ATTR_LED_DECL,
    P2P_ATTR_LED_VALUE,
    P2P_ATTR_SWITCH_DECL,
    P2P_ATTR_SWITCH_VALUE,
    P2P_ATTR_SWITCH_CCC,
    P2P_ATTR_LONG_DECL,
    P2P_ATTR_LONG_VALUE,
    P2P_ATTR_LONG_CCC,
    P2P_ATTR_FILE_CTRL_DECL,
    P2P_ATTR_FILE_CTRL_VALUE,
    P2P_ATTR_FILE_DATA_DECL,
    P2P_ATTR_FILE_DATA_VALUE,
    P2P_ATTR_FILE_DATA_CCC,
};

enum hrs_attr_index {
    HRS_ATTR_PRIMARY = 0,
    HRS_ATTR_MEAS_DECL,
    HRS_ATTR_MEAS_VALUE,
    HRS_ATTR_MEAS_CCC,
    HRS_ATTR_LOC_DECL,
    HRS_ATTR_LOC_VALUE,
    HRS_ATTR_CTRL_DECL,
    HRS_ATTR_CTRL_VALUE,
};

static void switch_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    switch_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("BLE CCC FE42 notify=%d\n", switch_notify_enabled ? 1 : 0);
}

static void long_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    long_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("BLE CCC FE43 notify=%d\n", long_notify_enabled ? 1 : 0);
}

static void file_data_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    file_data_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static void hrm_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    hrm_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static ssize_t led_ctrl_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    ARG_UNUSED(attr);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, led_ctrl_value, sizeof(led_ctrl_value));
}

static ssize_t led_ctrl_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len == 0U || len > sizeof(led_ctrl_value)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    memset(led_ctrl_value, 0, sizeof(led_ctrl_value));
    memcpy(led_ctrl_value, buf, len);

    if (len >= 2U) {
        uint8_t cmd = led_ctrl_value[0];
        uint8_t arg = led_ctrl_value[1];

        if (cmd == 0x02U) {
            if (arg == 0U) {
                stream_enabled = false;
                printk("BLE CMD: STOP recording\n");
            } else {
                uint8_t activity = (uint8_t)(arg - 1U);
                if (activity > 5U) {
                    activity = 5U;
                }
                stream_enabled = true;
                selected_activity_index = activity;
                printk("BLE CMD: START recording activity=%u\n", selected_activity_index);
            }
        } else if (cmd == 0x00U) {
            if (status_led_ready) {
                (void)gpio_pin_set_dt(&status_led, (arg != 0U) ? 1 : 0);
            }
            printk("BLE CMD: LED state=%u\n", arg);
        } else {
            printk("BLE CMD: FE41 unknown cmd=0x%02x arg=0x%02x\n", cmd, arg);
        }
    }

    return (ssize_t)len;
}

static ssize_t file_ctrl_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    const uint8_t *rx = buf;

    if (offset != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len == 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    switch (rx[0]) {
    case 0x01: {
        /* LIST: renvoie "fin de liste" immédiate (aucun fichier exposé côté Nordic). */
        uint8_t end_list[] = {0x22};
        (void)notify_file_data(end_list, sizeof(end_list));
        break;
    }
    case 0x02: {
        /* GET non implémenté sur ce firmware. */
        uint8_t error[] = {0xEE, 0x01};
        (void)notify_file_data(error, sizeof(error));
        break;
    }
    case 0x03:
        printk("BLE CMD: FILE abort\n");
        break;
    default:
        printk("BLE CMD: FE44 unknown cmd=0x%02x\n", rx[0]);
        break;
    }

    return (ssize_t)len;
}

static ssize_t body_sensor_loc_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset)
{
    ARG_UNUSED(attr);
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             &body_sensor_location, sizeof(body_sensor_location));
}

static ssize_t hr_ctrl_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    const uint8_t *rx = buf;

    if (offset != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len != 1U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    if (rx[0] == 0x01U) {
        printk("BLE CMD: Heart Rate reset energy\n");
    } else {
        printk("BLE CMD: HR control unknown=0x%02x\n", rx[0]);
    }

    return (ssize_t)len;
}

BT_GATT_SERVICE_DEFINE(p2p_svc,
    BT_GATT_PRIMARY_SERVICE(&uuid_p2p_service),
    BT_GATT_CHARACTERISTIC(&uuid_led_char.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           led_ctrl_read, led_ctrl_write, led_ctrl_value),
    BT_GATT_CHARACTERISTIC(&uuid_switch_char.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(switch_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&uuid_long_char.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(long_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&uuid_file_ctrl_char.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE,
                           NULL, file_ctrl_write, NULL),
    BT_GATT_CHARACTERISTIC(&uuid_file_data_char.uuid,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(file_data_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

BT_GATT_SERVICE_DEFINE(hrs_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HRS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HRS_MEASUREMENT,
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(hrm_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BT_UUID_HRS_BODY_SENSOR,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           body_sensor_loc_read, NULL, &body_sensor_location),
    BT_GATT_CHARACTERISTIC(BT_UUID_HRS_CONTROL_POINT,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL, hr_ctrl_write, NULL));

static int notify_checked(const struct bt_gatt_attr *attr, const void *data,
                          uint16_t len, bool notify_enabled)
{
    if (active_conn == NULL) {
        return -ENOTCONN;
    }

    if (!notify_enabled) {
        return 0;
    }

    return bt_gatt_notify(active_conn, attr, data, len);
}

static int notify_file_data(const uint8_t *data, uint16_t len)
{
    return notify_checked(&p2p_svc.attrs[P2P_ATTR_FILE_DATA_VALUE],
                          data, len, file_data_notify_enabled);
}

static int start_advertising_internal(void)
{
    const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0x0D, 0x18),
    };
    const struct bt_data sd[] = {
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, 0x40, 0xFE, 0x00, 0x00,
                      0x7A, 0xCC, 0x2A, 0x48, 0x98, 0x4A,
                      0x7F, 0x2E, 0xD5, 0xB3, 0xE5, 0x8F),
        BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
    };

    int ret = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (ret == -EALREADY) {
        return 0;
    }

    return ret;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err != 0U) {
        printk("BLE connect failed (err 0x%02x)\n", err);
        return;
    }

    if (active_conn == NULL) {
        active_conn = bt_conn_ref(conn);
    }

    printk("BLE connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);

    if (active_conn != NULL) {
        bt_conn_unref(active_conn);
        active_conn = NULL;
    }

    switch_notify_enabled = false;
    long_notify_enabled = false;
    file_data_notify_enabled = false;
    hrm_notify_enabled = false;

    printk("BLE disconnected (reason 0x%02x)\n", reason);

    (void)start_advertising_internal();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

int ble_init(void)
{
    int ret = bt_enable(NULL);
    if (ret != 0) {
        printk("BLE init failed: %d\n", ret);
        return ret;
    }

    printk("BLE initialized\n");

#if HAS_STATUS_LED
    if (gpio_is_ready_dt(&status_led)) {
        if (gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE) == 0) {
            status_led_ready = true;
        }
    }
#endif

    return 0;
}

int ble_start_advertising(void)
{
    int ret = start_advertising_internal();
    if (ret != 0) {
        printk("BLE advertising failed: %d\n", ret);
        return ret;
    }

    printk("BLE advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);
    return 0;
}

bool ble_is_connected(void)
{
    return active_conn != NULL;
}

bool ble_streaming_enabled(void)
{
    return stream_enabled;
}

uint8_t ble_selected_activity_index(void)
{
    return selected_activity_index;
}

int ble_notify_temperature_centi(int16_t temperature_centi_c)
{
    uint8_t payload[2] = {
        (uint8_t)(temperature_centi_c & 0xFF),
        (uint8_t)((uint16_t)temperature_centi_c >> 8),
    };

    return notify_checked(&p2p_svc.attrs[P2P_ATTR_SWITCH_VALUE],
                          payload, sizeof(payload), switch_notify_enabled);
}

int ble_notify_motion(int16_t gyro_x_centi_dps,
                      int16_t gyro_y_centi_dps,
                      int16_t gyro_z_centi_dps,
                      uint16_t pitch_centi_deg,
                      uint16_t roll_centi_deg)
{
    uint8_t payload[12] = {
        (uint8_t)(gyro_x_centi_dps & 0xFF),
        (uint8_t)((uint16_t)gyro_x_centi_dps >> 8),
        (uint8_t)(gyro_y_centi_dps & 0xFF),
        (uint8_t)((uint16_t)gyro_y_centi_dps >> 8),
        (uint8_t)(gyro_z_centi_dps & 0xFF),
        (uint8_t)((uint16_t)gyro_z_centi_dps >> 8),
        (uint8_t)(pitch_centi_deg & 0xFF),
        (uint8_t)(pitch_centi_deg >> 8),
        (uint8_t)(roll_centi_deg & 0xFF),
        (uint8_t)(roll_centi_deg >> 8),
        0x00,
        0x00,
    };

    return notify_checked(&p2p_svc.attrs[P2P_ATTR_LONG_VALUE],
                          payload, sizeof(payload), long_notify_enabled);
}

int ble_notify_biometrics(uint16_t heart_rate_bpm, uint16_t spo2_percent)
{
    /* Format attendu par l'app: Flags=0x09, HR 16-bit LE, "Energy" 16-bit LE (utilisé pour SpO2). */
    uint8_t payload[5] = {
        0x09,
        (uint8_t)(heart_rate_bpm & 0xFF),
        (uint8_t)(heart_rate_bpm >> 8),
        (uint8_t)(spo2_percent & 0xFF),
        (uint8_t)(spo2_percent >> 8),
    };

    return notify_checked(&hrs_svc.attrs[HRS_ATTR_MEAS_VALUE],
                          payload, sizeof(payload), hrm_notify_enabled);
}

int ble_notify_activity(uint8_t activity_class, float confidence)
{
    /* Send activity classification via file data characteristic */
    /* Format: [0xAA, activity_class, confidence_int, confidence_frac] */
    uint8_t confidence_int = (uint8_t)(confidence * 100.0f);
    uint8_t confidence_frac = (uint8_t)((confidence * 100.0f - confidence_int) * 100.0f);

    uint8_t payload[4] = {
        0xAA,  // Activity notification marker
        activity_class,
        confidence_int,
        confidence_frac
    };

    return notify_checked(&p2p_svc.attrs[P2P_ATTR_FILE_DATA_VALUE],
                          payload, sizeof(payload), file_data_notify_enabled);
}
