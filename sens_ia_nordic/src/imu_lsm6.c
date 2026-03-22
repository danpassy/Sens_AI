#include "imu_lsm6.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <limits.h>

#define IMU_NODE DT_ALIAS(imu)

#if !DT_NODE_EXISTS(DT_ALIAS(imu))
#error "Devicetree: alias 'imu' manquant. Ajoutez dans l'overlay: / { aliases { imu = &<node_capteur>; }; };"
#endif

static int read_whoami(const struct device *i2c, uint16_t addr, uint8_t *out)
{
    /* WHO_AM_I = 0x0F (famille LSM6DSO/LSM6DSOX) */
    return i2c_reg_read_byte(i2c, addr, 0x0F, out);
}

static void probe_lsm6_addresses(const struct device *i2c)
{
    const uint16_t candidates[] = {0x6A, 0x6B};

    for (size_t i = 0; i < ARRAY_SIZE(candidates); i++) {
        uint8_t who = 0;
        int ret = read_whoami(i2c, candidates[i], &who);
        printk("IMU probe: addr=0x%02X WHO_AM_I ret=%d value=0x%02X\n",
               candidates[i], ret, who);
    }
}

static void scan_i2c_bus(const struct device *i2c)
{
    uint8_t dummy = 0;
    int found = 0;

    printk("IMU scan: bus=%s range=0x08..0x77\n", i2c->name);

    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        int ret = i2c_read(i2c, &dummy, 1, addr);
        if (ret == 0) {
            printk("IMU scan: ACK at 0x%02X\n", addr);
            found++;
        }
    }

    if (found == 0) {
        printk("IMU scan: no ACK on this bus\n");
    }
}

int imu_lsm6_init(imu_lsm6_t *ctx)
{
    if (ctx == NULL) {
        return -EINVAL;
    }

    ctx->imu_dev = DEVICE_DT_GET(IMU_NODE);
    ctx->i2c_dev = DEVICE_DT_GET(DT_BUS(IMU_NODE));
    ctx->i2c_addr = (uint16_t)DT_REG_ADDR(IMU_NODE);
    ctx->whoami = 0x00;

    if (!device_is_ready(ctx->imu_dev)) {
        printk("IMU: device not ready (driver/devicetree)\n");
        if (device_is_ready(ctx->i2c_dev)) {
            probe_lsm6_addresses(ctx->i2c_dev);
            scan_i2c_bus(ctx->i2c_dev);
        }
        return -ENODEV;
    }

    if (!device_is_ready(ctx->i2c_dev)) {
        printk("IMU: I2C bus not ready\n");
        return -ENODEV;
    }

    int ret = read_whoami(ctx->i2c_dev, ctx->i2c_addr, &ctx->whoami);
    printk("IMU: addr=0x%02X WHO_AM_I ret=%d value=0x%02X\n",
           ctx->i2c_addr, ret, ctx->whoami);

    return ret; /* 0 si OK */
}

int imu_lsm6_set_odr_hz(const imu_lsm6_t *ctx, int odr_hz)
{
    if (ctx == NULL || ctx->imu_dev == NULL) {
        return -EINVAL;
    }

    struct sensor_value odr = { .val1 = odr_hz, .val2 = 0 };

    int r1 = sensor_attr_set(ctx->imu_dev,
                             SENSOR_CHAN_ACCEL_XYZ,
                             SENSOR_ATTR_SAMPLING_FREQUENCY,
                             &odr);

    int r2 = sensor_attr_set(ctx->imu_dev,
                             SENSOR_CHAN_GYRO_XYZ,
                             SENSOR_ATTR_SAMPLING_FREQUENCY,
                             &odr);

    /* On retourne la première erreur si l’une échoue */
    if (r1 != 0) {
        return r1;
    }
    return r2;
}

int imu_lsm6_read(const imu_lsm6_t *ctx,
                  struct sensor_value accel_xyz[3],
                  struct sensor_value gyro_xyz[3])
{
    if (ctx == NULL || ctx->imu_dev == NULL) {
        return -EINVAL;
    }

    int ret = sensor_sample_fetch(ctx->imu_dev);
    if (ret != 0) {
        return ret;
    }

    ret = sensor_channel_get(ctx->imu_dev, SENSOR_CHAN_ACCEL_XYZ, accel_xyz);
    if (ret != 0) {
        return ret;
    }

    ret = sensor_channel_get(ctx->imu_dev, SENSOR_CHAN_GYRO_XYZ, gyro_xyz);
    return ret;
}

int imu_lsm6_read_die_temp_centi(const imu_lsm6_t *ctx, int16_t *temp_centi_c)
{
    if (ctx == NULL || ctx->i2c_dev == NULL || temp_centi_c == NULL) {
        return -EINVAL;
    }

    /* LSM6DSOX: OUT_TEMP_L/H = 0x20/0x21 */
    uint8_t temp_l = 0;
    uint8_t temp_h = 0;

    int ret = i2c_reg_read_byte(ctx->i2c_dev, ctx->i2c_addr, 0x20, &temp_l);
    if (ret != 0) {
        return ret;
    }

    ret = i2c_reg_read_byte(ctx->i2c_dev, ctx->i2c_addr, 0x21, &temp_h);
    if (ret != 0) {
        return ret;
    }

    int16_t temp_raw = (int16_t)(((uint16_t)temp_h << 8) | temp_l);

    /* temp_c = 25 + raw/256 -> centi-deg C */
    int32_t centi = 2500 + ((int32_t)temp_raw * 100) / 256;

    if (centi > INT16_MAX) {
        centi = INT16_MAX;
    } else if (centi < INT16_MIN) {
        centi = INT16_MIN;
    }

    *temp_centi_c = (int16_t)centi;
    return 0;
}

int imu_lsm6_dump_i2c_raw(const imu_lsm6_t *ctx)
{
    if (ctx == NULL || ctx->i2c_dev == NULL) {
        return -EINVAL;
    }

    /* Registres typiques */
    const uint8_t REG_STATUS = 0x1E;
    const uint8_t REG_OUT_T  = 0x20; /* OUT_TEMP_L/H (2 bytes) */
    const uint8_t REG_OUT_G  = 0x22; /* OUTX_L_G.. (6 bytes) */
    const uint8_t REG_OUT_XL = 0x28; /* OUTX_L_XL.. (6 bytes) */

    uint8_t who = 0;
    int ret = read_whoami(ctx->i2c_dev, ctx->i2c_addr, &who);
    printk("IMU RAW: WHO_AM_I ret=%d value=0x%02X\n", ret, who);

    uint8_t st = 0;
    ret = i2c_reg_read_byte(ctx->i2c_dev, ctx->i2c_addr, REG_STATUS, &st);
    printk("IMU RAW: STATUS  ret=%d value=0x%02X\n", ret, st);

    uint8_t g[6] = {0};
    uint8_t a[6] = {0};
    uint8_t t[2] = {0};

    ret = i2c_burst_read(ctx->i2c_dev, ctx->i2c_addr, REG_OUT_T, t, sizeof(t));
    printk("IMU RAW: T ret=%d %02X %02X\n", ret, t[0], t[1]);

    ret = i2c_burst_read(ctx->i2c_dev, ctx->i2c_addr, REG_OUT_G, g, sizeof(g));
    printk("IMU RAW: G ret=%d %02X %02X %02X %02X %02X %02X\n",
           ret, g[0], g[1], g[2], g[3], g[4], g[5]);

    ret = i2c_burst_read(ctx->i2c_dev, ctx->i2c_addr, REG_OUT_XL, a, sizeof(a));
    printk("IMU RAW: A ret=%d %02X %02X %02X %02X %02X %02X\n",
           ret, a[0], a[1], a[2], a[3], a[4], a[5]);

    return 0;
}
