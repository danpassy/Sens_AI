#ifndef IMU_LSM6_H
#define IMU_LSM6_H

#include <stdint.h>
#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const struct device *imu_dev;   /* device "sensor" (driver Zephyr) */
    const struct device *i2c_dev;   /* bus I2C du capteur */
    uint16_t i2c_addr;              /* 0x6A ou 0x6B */
    uint8_t whoami;                 /* valeur WHO_AM_I lue */
} imu_lsm6_t;

/**
 * Initialise le capteur IMU à partir de l'alias devicetree: DT_ALIAS(imu).
 * - Vérifie device_is_ready
 * - Lit WHO_AM_I (registre 0x0F)
 */
int imu_lsm6_init(imu_lsm6_t *ctx);

/**
 * Force la fréquence d'échantillonnage (Hz) via sensor_attr_set().
 * Ex: 104, 208...
 */
int imu_lsm6_set_odr_hz(const imu_lsm6_t *ctx, int odr_hz);

/**
 * Lit un échantillon (fetch) puis récupère accel + gyro.
 * accel en m/s^2, gyro en rad/s (unités Zephyr).
 */
int imu_lsm6_read(const imu_lsm6_t *ctx,
                  struct sensor_value accel_xyz[3],
                  struct sensor_value gyro_xyz[3]);

/**
 * Lit la température interne du LSM6DSOX en centi-degrés Celsius.
 * Conversion: temp_c = 25 + raw/256 (datasheet ST).
 */
int imu_lsm6_read_die_temp_centi(const imu_lsm6_t *ctx, int16_t *temp_centi_c);

/**
 * Dump I2C brut utile debug (WHO_AM_I, STATUS, RAW accel/gyro)
 * Utile quand vous suspectez adresse/bus/pull-up.
 */
int imu_lsm6_dump_i2c_raw(const imu_lsm6_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* IMU_LSM6_H */
