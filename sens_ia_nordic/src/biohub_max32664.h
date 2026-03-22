#ifndef BIOHUB_MAX32664_H
#define BIOHUB_MAX32664_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <zephyr/device.h>

#define BIOHUB_I2C_ADDR_7B    0x55
#define BIOHUB_I2C_ADDR_8B    (BIOHUB_I2C_ADDR_7B << 1)

#define BIOHUB_DISABLE        0x00
#define BIOHUB_ENABLE         0x01
#define MODE_ONE              0x01
#define MODE_TWO              0x02
#define APP_MODE              0x00
#define BOOTLOADER_MODE       0x08

#define ENABLE_CMD_DELAY      45
#define CMD_DELAY             2
#define MAXFAST_ARRAY_SIZE    6
#define MAXFAST_EXTENDED_DATA 5

typedef enum {
    SFE_BIO_SUCCESS       = 0x00,
    ERR_UNAVAIL_CMD       = 0x01,
    ERR_UNAVAIL_FUNC      = 0x02,
    ERR_DATA_FORMAT       = 0x03,
    ERR_INPUT_VALUE       = 0x04,
    ERR_TRY_AGAIN         = 0x05,
    ERR_BTLDR_GENERAL     = 0x80,
    ERR_BTLDR_CHECKSUM    = 0x81,
    ERR_BTLDR_AUTH        = 0x82,
    ERR_BTLDR_INVALID_APP = 0x83,
    ERR_UNKNOWN           = 0xFF
} hub_status_t;

typedef enum {
    HUB_STATUS              = 0x00,
    SET_DEVICE_MODE         = 0x01,
    READ_DEVICE_MODE        = 0x02,
    OUTPUT_MODE             = 0x10,
    READ_OUTPUT_MODE        = 0x11,
    READ_DATA_OUTPUT        = 0x12,
    READ_DATA_INPUT         = 0x13,
    WRITE_INPUT             = 0x14,
    WRITE_REGISTER          = 0x40,
    READ_REGISTER           = 0x41,
    READ_ATTRIBUTES_AFE     = 0x42,
    DUMP_REGISTERS          = 0x43,
    ENABLE_SENSOR           = 0x44,
    READ_SENSOR_MODE        = 0x45,
    CHANGE_ALGORITHM_CONFIG = 0x50,
    READ_ALGORITHM_CONFIG   = 0x51,
    ENABLE_ALGORITHM        = 0x52,
    BOOTLOADER_FLASH        = 0x80,
    BOOTLOADER_INFO         = 0x81,
    IDENTITY                = 0xFF
} family_byte_t;

typedef enum {
    SET_FORMAT = 0x00,
    READ_FORMAT = 0x01
} output_mode_index_t;

typedef enum {
    NUM_SAMPLES = 0x00,
    READ_DATA = 0x01
} fifo_output_index_t;

typedef enum {
    WRITE_SET_THRESHOLD = 0x01
} write_input_index_t;

typedef enum {
    PAUSE = 0x00,
    SENSOR_DATA = 0x01,
    ALGO_DATA = 0x02,
    SENSOR_AND_ALGORITHM = 0x03,
    PAUSE_TWO = 0x04,
    SENSOR_COUNTER_BYTE = 0x05,
    ALGO_COUNTER_BYTE = 0x06,
    SENSOR_ALGO_COUNTER = 0x07
} output_mode_write_t;

typedef enum {
    ENABLE_MAX30101 = 0x03,
    ENABLE_ACCELEROMETER = 0x04
} sensor_enable_index_t;

typedef enum {
    ENABLE_AGC_ALGO = 0x00,
    ENABLE_WHRM_ALGO = 0x02
} algo_enable_index_t;

typedef enum {
    READ_MCU_TYPE = 0x00,
    READ_SENSOR_HUB_VERS = 0x03,
    READ_ALGO_VERS = 0x07
} identity_index_t;

typedef struct {
    uint16_t heartRate;
    uint8_t confidence;
    uint16_t oxygen;
    uint8_t status;
    int8_t extStatus;
    uint16_t rValue;
    uint8_t reserveOne;
    uint8_t reserveTwo;
} bioData;

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t revision;
} version;

typedef struct {
    const struct device *i2c_dev;
    const struct device *gpio_dev;
    uint16_t rst_pin;
    uint16_t mfio_pin;
    uint8_t userSelectedMode;
    uint8_t sampleRate;
} biohub_t;

uint8_t biohub_begin(biohub_t *dev,
                     const struct device *i2c_dev,
                     const struct device *gpio_dev,
                     uint16_t rst_pin,
                     uint16_t mfio_pin);
uint8_t biohub_configBpm(biohub_t *dev, uint8_t mode);
uint8_t biohub_readBpm(biohub_t *dev, bioData *body);
uint8_t biohub_readSensorHubStatus(biohub_t *dev);
uint8_t biohub_numSamplesOutFifo(biohub_t *dev);
uint8_t biohub_readDeviceMode(biohub_t *dev, uint8_t *mode);
uint8_t biohub_readSensorHubVersion(biohub_t *dev, version *vers);

#ifdef __cplusplus
}
#endif

#endif /* BIOHUB_MAX32664_H */
