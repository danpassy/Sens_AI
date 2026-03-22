#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/sensor.h>

#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "biohub_max32664.h"
#include "ble.h"
#include "imu_lsm6.h"

// Edge Impulse includes
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "model-parameters/model_variables.h"

#define PI_D 3.14159265358979323846
#define SAMPLE_PERIOD_MS 200
#define UART_BUF_SIZE 160
#define MAX32664_NODE DT_NODELABEL(max32664)
#define MAX32664_GPIO_NODE DT_NODELABEL(gpio0)
#define MAX32664_RST_PIN 0
#define MAX32664_MFIO_PIN 1

#if !DT_HAS_CHOSEN(ncs_ei_uart)
#error "Devicetree chosen 'ncs,ei-uart' missing. Set it in your board overlay."
#endif

static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(ncs_ei_uart));
static atomic_t uart_busy;

#if DT_NODE_HAS_STATUS(MAX32664_NODE, okay)
#define MAX32664_ADDR_7B DT_REG_ADDR(MAX32664_NODE)
static const struct device *const max32664_i2c_dev = DEVICE_DT_GET(DT_BUS(MAX32664_NODE));
#else
#define MAX32664_ADDR_7B BIOHUB_I2C_ADDR_7B
static const struct device *const max32664_i2c_dev = NULL;
#endif

#if DT_NODE_HAS_STATUS(MAX32664_GPIO_NODE, okay)
static const struct device *const max32664_gpio_dev = DEVICE_DT_GET(MAX32664_GPIO_NODE);
#else
static const struct device *const max32664_gpio_dev = NULL;
#endif

static biohub_t max32664 = {0};
static bioData cardio = {0};
static bool max32664_ready;
static uint16_t bio_hr_bpm;
static uint16_t bio_spo2_percent;

// Edge Impulse variables
#define EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW 8
#define EI_CLASSIFIER_RAW_SAMPLE_COUNT 208
static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE] = {0};
static int feature_index = 0;
static bool inference_ready = false;

static double sensor_to_double(const struct sensor_value *v)
{
    return (double)v->val1 + (double)v->val2 / 1000000.0;
}

static int16_t to_i16_scaled_100(double value)
{
    double scaled = round(value * 100.0);

    if (scaled > (double)INT16_MAX) {
        return INT16_MAX;
    }
    if (scaled < (double)INT16_MIN) {
        return INT16_MIN;
    }

    return (int16_t)scaled;
}

static uint16_t angle_to_u16_scaled_100(double angle_deg)
{
    while (angle_deg < 0.0) {
        angle_deg += 360.0;
    }
    while (angle_deg >= 360.0) {
        angle_deg -= 360.0;
    }

    double scaled = round(angle_deg * 100.0);
    if (scaled < 0.0) {
        scaled = 0.0;
    }
    if (scaled > 36000.0) {
        scaled = 36000.0;
    }

    return (uint16_t)scaled;
}

static void uart_cb(const struct device *dev, struct uart_event *evt, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    if (evt->type == UART_TX_DONE || evt->type == UART_TX_ABORTED) {
        (void)atomic_set(&uart_busy, false);
    }
}

static int init_uart_stream(void)
{
    if (!device_is_ready(uart_dev)) {
        printk("UART device not ready\n");
        return -ENODEV;
    }

    int ret = uart_callback_set(uart_dev, uart_cb, NULL);
    if (ret != 0) {
        printk("Cannot set UART callback (err %d)\n", ret);
        return ret;
    }

    (void)atomic_set(&uart_busy, false);
    return 0;
}

static int uart_send_sensor_csv(double ax, double ay, double az,
                                double gx_dps, double gy_dps, double gz_dps,
                                uint16_t hr_bpm, uint16_t spo2_percent)
{
    static uint8_t buf[UART_BUF_SIZE];

    if (!atomic_cas(&uart_busy, false, true)) {
        return -EBUSY;
    }

    int res = snprintf((char *)buf, sizeof(buf),
                       "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u\r\n",
                       ax, ay, az, gx_dps, gy_dps, gz_dps, hr_bpm, spo2_percent);
    if (res < 0) {
        (void)atomic_set(&uart_busy, false);
        return res;
    }
    if (res >= (int)sizeof(buf)) {
        (void)atomic_set(&uart_busy, false);
        return -ENOMEM;
    }

    int ret = uart_tx(uart_dev, buf, (size_t)res, SYS_FOREVER_MS);
    if (ret != 0) {
        (void)atomic_set(&uart_busy, false);
        return ret;
    }

    return 0;
}

static int uart_send_csv_header(void)
{
    static const uint8_t header[] = "acc_x,acc_y,acc_z,gyr_x,gyr_y,gyr_z,htr,o2\r\n";

    if (!atomic_cas(&uart_busy, false, true)) {
        return -EBUSY;
    }

    int ret = uart_tx(uart_dev, header, sizeof(header) - 1U, SYS_FOREVER_MS);
    if (ret != 0) {
        (void)atomic_set(&uart_busy, false);
        return ret;
    }

    return 0;
}

static int init_max32664(void)
{
    if (max32664_i2c_dev == NULL || !device_is_ready(max32664_i2c_dev)) {
        return -ENODEV;
    }

    if (max32664_gpio_dev == NULL || !device_is_ready(max32664_gpio_dev)) {
        return -ENODEV;
    }

    int rc = gpio_pin_configure(max32664_gpio_dev, MAX32664_RST_PIN, GPIO_OUTPUT_HIGH);
    if (rc != 0) {
        return rc;
    }

    rc = gpio_pin_configure(max32664_gpio_dev, MAX32664_MFIO_PIN, GPIO_OUTPUT_HIGH);
    if (rc != 0) {
        return rc;
    }

    uint8_t mode = biohub_begin(&max32664,
                                max32664_i2c_dev,
                                max32664_gpio_dev,
                                MAX32664_RST_PIN,
                                MAX32664_MFIO_PIN);

    if (mode == BOOTLOADER_MODE) {
        return -EIO;
    }

    if (mode != APP_MODE) {
        return -EIO;
    }

    version fw = {0};
    (void)biohub_readSensorHubVersion(&max32664, &fw);

    uint8_t cfg = biohub_configBpm(&max32664, MODE_TWO);
    if (cfg != SFE_BIO_SUCCESS) {
        return -EIO;
    }

    max32664_ready = true;
    return 0;
}

static void poll_max32664(void)
{
    if (!max32664_ready) {
        return;
    }

    uint8_t status = biohub_readBpm(&max32664, &cardio);
    if (status == ERR_TRY_AGAIN) {
        return;
    }

    if (status != SFE_BIO_SUCCESS) {
        return;
    }

    bio_hr_bpm = (cardio.heartRate <= 250U) ? cardio.heartRate : 0U;
    bio_spo2_percent = (cardio.oxygen <= 100U) ? cardio.oxygen : 0U;
}

static void run_inference(void)
{
    ei_impulse_result_t result = {0};

    // Prepare signal for inference
    signal_t signal;
    numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);

    // Run classifier
    EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

    if (res != EI_IMPULSE_OK) {
        printk("Edge Impulse classifier failed (%d)\n", res);
        return;
    }

    // Print predictions
    printk("Predictions:\n");
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        printk("  %s: %.5f\n", result.classification[i].label, result.classification[i].value);
    }

    // Send results via BLE if connected
    if (ble_is_connected()) {
        // Find the class with highest probability
        size_t best_index = 0;
        float best_value = 0.0f;
        for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
            if (result.classification[i].value > best_value) {
                best_value = result.classification[i].value;
                best_index = i;
            }
        }

        // Send activity classification via BLE
        (void)ble_notify_activity((uint8_t)best_index, best_value);

        // Print the result
        printk("Detected activity: %s (confidence: %.2f)\n",
               result.classification[best_index].label,
               result.classification[best_index].value);
    }
}

int main(void)
{
    imu_lsm6_t imu = {0};
    int ret = 0;

    ret = init_uart_stream();
    if (ret != 0) {
        return 0;
    }

    ret = ble_init();
    if (ret != 0) {
        return 0;
    }

    ret = ble_start_advertising();
    if (ret != 0) {
        return 0;
    }

    ret = imu_lsm6_init(&imu);
    if (ret != 0) {
        return 0;
    }

    (void)imu_lsm6_set_odr_hz(&imu, 104);

    ret = init_max32664();
    (void)ret;

    (void)uart_send_csv_header();

    while (1) {
        struct sensor_value accel[3];
        struct sensor_value gyro[3];
        double ax;
        double ay;
        double az;
        double gx_rad;
        double gy_rad;
        double gz_rad;
        double gx_dps;
        double gy_dps;
        double gz_dps;
        double pitch_deg;
        double roll_deg;
        int16_t temp_centi = 0;
        int temp_read_ret;

        ret = imu_lsm6_read(&imu, accel, gyro);
        if (ret != 0) {
            k_sleep(K_MSEC(500));
            continue;
        }

        ax = sensor_to_double(&accel[0]);
        ay = sensor_to_double(&accel[1]);
        az = sensor_to_double(&accel[2]);
        gx_rad = sensor_to_double(&gyro[0]);
        gy_rad = sensor_to_double(&gyro[1]);
        gz_rad = sensor_to_double(&gyro[2]);
        gx_dps = gx_rad * (180.0 / PI_D);
        gy_dps = gy_rad * (180.0 / PI_D);
        gz_dps = gz_rad * (180.0 / PI_D);

        pitch_deg = atan2(ay, sqrt((ax * ax) + (az * az))) * (180.0 / PI_D);
        roll_deg = atan2(-ax, az) * (180.0 / PI_D);

        temp_read_ret = imu_lsm6_read_die_temp_centi(&imu, &temp_centi);

        poll_max32664();
        (void)uart_send_sensor_csv(ax, ay, az,
                                   gx_dps, gy_dps, gz_dps,
                                   bio_hr_bpm, bio_spo2_percent);

        // Add sensor data to features buffer for Edge Impulse
        features[feature_index++] = (float)ax;
        features[feature_index++] = (float)ay;
        features[feature_index++] = (float)az;
        features[feature_index++] = (float)gx_dps;
        features[feature_index++] = (float)gy_dps;
        features[feature_index++] = (float)gz_dps;
        features[feature_index++] = (float)bio_hr_bpm;
        features[feature_index++] = (float)bio_spo2_percent;

        // Check if we have enough data for inference
        if (feature_index >= EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
            run_inference();
            feature_index = 0; // Reset buffer for next inference window
        }

        if (ble_is_connected()) {
            if (temp_read_ret == 0) {
                (void)ble_notify_temperature_centi(temp_centi);
            }

            (void)ble_notify_motion(to_i16_scaled_100(gx_dps),
                                    to_i16_scaled_100(gy_dps),
                                    to_i16_scaled_100(gz_dps),
                                    angle_to_u16_scaled_100(pitch_deg),
                                    angle_to_u16_scaled_100(roll_deg));
            (void)ble_notify_biometrics(bio_hr_bpm, bio_spo2_percent);
        }

        k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
    }

    return 0;
}
