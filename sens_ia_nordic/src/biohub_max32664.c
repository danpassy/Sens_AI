#include "biohub_max32664.h"

#include <stdbool.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define BIOHUB_LOG(...) printk(__VA_ARGS__)
#define ALGO_CMD_DELAY_LONG 45

static uint8_t bpmArr[MAXFAST_ARRAY_SIZE];
static uint8_t bpmArrTwo[MAXFAST_ARRAY_SIZE + MAXFAST_EXTENDED_DATA];
static uint8_t rxBuf[MAXFAST_ARRAY_SIZE + MAXFAST_EXTENDED_DATA + 1];

static bool i2c_tx(biohub_t *dev, const uint8_t *buf, uint16_t len)
{
    if (dev == NULL || dev->i2c_dev == NULL || buf == NULL) {
        return false;
    }

    return i2c_write(dev->i2c_dev, buf, len, BIOHUB_I2C_ADDR_7B) == 0;
}

static bool i2c_rx(biohub_t *dev, uint8_t *buf, uint16_t len)
{
    if (dev == NULL || dev->i2c_dev == NULL || buf == NULL) {
        return false;
    }

    return i2c_read(dev->i2c_dev, buf, len, BIOHUB_I2C_ADDR_7B) == 0;
}

static uint8_t writeByte(biohub_t *dev, uint8_t familyByte, uint8_t indexByte, uint8_t writeByteValue)
{
    uint8_t cmd[3] = {familyByte, indexByte, writeByteValue};
    uint8_t statusByte = ERR_UNKNOWN;

    if (!i2c_tx(dev, cmd, sizeof(cmd))) {
        return ERR_UNKNOWN;
    }

    k_msleep(CMD_DELAY);

    if (!i2c_rx(dev, &statusByte, 1)) {
        return ERR_UNKNOWN;
    }

    return statusByte;
}

static uint8_t enableWrite(biohub_t *dev, uint8_t familyByte, uint8_t indexByte, uint8_t enableByte)
{
    uint8_t cmd[3] = {familyByte, indexByte, enableByte};
    uint8_t statusByte = ERR_UNKNOWN;

    if (!i2c_tx(dev, cmd, sizeof(cmd))) {
        return ERR_UNKNOWN;
    }

    if (familyByte == ENABLE_SENSOR && indexByte == ENABLE_MAX30101) {
        k_msleep(100);
    } else if (familyByte == ENABLE_ALGORITHM && indexByte == ENABLE_AGC_ALGO) {
        k_msleep(ENABLE_CMD_DELAY);
    } else if (familyByte == ENABLE_ALGORITHM && indexByte == ENABLE_WHRM_ALGO) {
        k_msleep(ALGO_CMD_DELAY_LONG);
    } else {
        k_msleep(CMD_DELAY);
    }

    if (!i2c_rx(dev, &statusByte, 1)) {
        return ERR_UNKNOWN;
    }

    return statusByte;
}

static uint8_t readByte(biohub_t *dev, uint8_t familyByte, uint8_t indexByte)
{
    uint8_t cmd[2] = {familyByte, indexByte};
    uint8_t rx[2] = {0};

    if (!i2c_tx(dev, cmd, sizeof(cmd))) {
        return ERR_UNKNOWN;
    }

    k_msleep(CMD_DELAY);

    if (!i2c_rx(dev, rx, sizeof(rx))) {
        return ERR_UNKNOWN;
    }

    return rx[1];
}

static uint8_t readFillArray(biohub_t *dev, uint8_t familyByte, uint8_t indexByte,
                             uint8_t numOfReads, uint8_t array[])
{
    uint8_t cmd[2] = {familyByte, indexByte};
    uint8_t statusByte;

    if (dev == NULL || array == NULL) {
        return ERR_UNKNOWN;
    }

    if ((uint16_t)numOfReads + 1U > sizeof(rxBuf)) {
        return ERR_UNKNOWN;
    }

    if (!i2c_tx(dev, cmd, sizeof(cmd))) {
        return ERR_UNKNOWN;
    }

    k_msleep(CMD_DELAY);

    if (!i2c_rx(dev, rxBuf, (uint16_t)numOfReads + 1U)) {
        return ERR_UNKNOWN;
    }

    statusByte = rxBuf[0];
    if (statusByte != SFE_BIO_SUCCESS) {
        memset(array, 0, numOfReads);
        return statusByte;
    }

    memcpy(array, &rxBuf[1], numOfReads);
    return statusByte;
}

uint8_t biohub_begin(biohub_t *dev,
                     const struct device *i2c_dev,
                     const struct device *gpio_dev,
                     uint16_t rst_pin,
                     uint16_t mfio_pin)
{
    if (dev == NULL || i2c_dev == NULL || gpio_dev == NULL) {
        return ERR_UNKNOWN;
    }

    dev->i2c_dev = i2c_dev;
    dev->gpio_dev = gpio_dev;
    dev->rst_pin = rst_pin;
    dev->mfio_pin = mfio_pin;
    dev->userSelectedMode = MODE_TWO;
    dev->sampleRate = 100;

    gpio_pin_set(dev->gpio_dev, dev->mfio_pin, 1);
    gpio_pin_set(dev->gpio_dev, dev->rst_pin, 0);
    k_msleep(10);
    gpio_pin_set(dev->gpio_dev, dev->rst_pin, 1);

    uint8_t responseByte = ERR_UNKNOWN;
    for (uint8_t i = 0; i < 20U; i++) {
        k_msleep(50);
        responseByte = readByte(dev, READ_DEVICE_MODE, 0x00);
        if (responseByte == APP_MODE || responseByte == BOOTLOADER_MODE) {
            return responseByte;
        }
    }

    return responseByte;
}

uint8_t biohub_configBpm(biohub_t *dev, uint8_t mode)
{
    uint8_t statusChauf;

    if (dev == NULL) {
        return ERR_UNKNOWN;
    }

    if (mode != MODE_ONE && mode != MODE_TWO) {
        BIOHUB_LOG("[MAX32664] ERROR: invalid mode\n");
        return ERR_INPUT_VALUE;
    }

    BIOHUB_LOG("[MAX32664] Configuring BPM mode %u...\n", mode);

    statusChauf = writeByte(dev, OUTPUT_MODE, SET_FORMAT, ALGO_DATA);
    if (statusChauf != SFE_BIO_SUCCESS) {
        BIOHUB_LOG("[MAX32664] setOutputMode failed (0x%02X)\n", statusChauf);
        return statusChauf;
    }

    statusChauf = writeByte(dev, OUTPUT_MODE, WRITE_SET_THRESHOLD, 0x01);
    if (statusChauf != SFE_BIO_SUCCESS) {
        BIOHUB_LOG("[MAX32664] setFifoThreshold failed (0x%02X)\n", statusChauf);
        return statusChauf;
    }

    statusChauf = enableWrite(dev, ENABLE_ALGORITHM, ENABLE_AGC_ALGO, BIOHUB_ENABLE);
    if (statusChauf != SFE_BIO_SUCCESS) {
        BIOHUB_LOG("[MAX32664] agcAlgoControl failed (0x%02X)\n", statusChauf);
        return statusChauf;
    }

    for (uint8_t retry = 0; retry < 5U; retry++) {
        statusChauf = enableWrite(dev, ENABLE_SENSOR, ENABLE_MAX30101, BIOHUB_ENABLE);
        if (statusChauf == SFE_BIO_SUCCESS) {
            break;
        }
        if (retry == 4U) {
            BIOHUB_LOG("[MAX32664] max30101Control failed (0x%02X)\n", statusChauf);
            return statusChauf;
        }
        BIOHUB_LOG("[MAX32664] max30101 retry %u/5\n", retry + 1U);
        k_msleep(100);
    }

    statusChauf = enableWrite(dev, ENABLE_ALGORITHM, ENABLE_WHRM_ALGO, mode);
    if (statusChauf != SFE_BIO_SUCCESS) {
        BIOHUB_LOG("[MAX32664] maximFastAlgoControl failed (0x%02X)\n", statusChauf);
        return statusChauf;
    }

    dev->userSelectedMode = mode;
    BIOHUB_LOG("[MAX32664] configured successfully\n");
    return SFE_BIO_SUCCESS;
}

uint8_t biohub_readBpm(biohub_t *dev, bioData *body)
{
    uint8_t statusChauf;
    uint8_t fifoSamples;

    if (dev == NULL || body == NULL) {
        return ERR_UNKNOWN;
    }

    memset(body, 0, sizeof(*body));
    statusChauf = biohub_readSensorHubStatus(dev);

    if (statusChauf != SFE_BIO_SUCCESS) {
        return statusChauf;
    }

    fifoSamples = biohub_numSamplesOutFifo(dev);
    if (fifoSamples == 0U) {
        return ERR_TRY_AGAIN;
    }

    if (dev->userSelectedMode == MODE_ONE) {
        statusChauf = readFillArray(dev, READ_DATA_OUTPUT, READ_DATA, MAXFAST_ARRAY_SIZE, bpmArr);
        if (statusChauf != SFE_BIO_SUCCESS) {
            return statusChauf;
        }

        body->heartRate = ((uint16_t)bpmArr[0] << 8) | bpmArr[1];
        body->heartRate /= 10U;
        body->confidence = bpmArr[2];
        body->oxygen = ((uint16_t)bpmArr[3] << 8) | bpmArr[4];
        body->oxygen /= 10U;
        body->status = bpmArr[5];
        return SFE_BIO_SUCCESS;
    }

    if (dev->userSelectedMode == MODE_TWO) {
        statusChauf = readFillArray(dev, READ_DATA_OUTPUT, READ_DATA,
                                    MAXFAST_ARRAY_SIZE + MAXFAST_EXTENDED_DATA,
                                    bpmArrTwo);
        if (statusChauf != SFE_BIO_SUCCESS) {
            return statusChauf;
        }

        body->heartRate = ((uint16_t)bpmArrTwo[0] << 8) | bpmArrTwo[1];
        body->heartRate /= 10U;
        body->confidence = bpmArrTwo[2];
        body->oxygen = ((uint16_t)bpmArrTwo[3] << 8) | bpmArrTwo[4];
        body->oxygen /= 10U;
        body->status = bpmArrTwo[5];
        body->rValue = ((uint16_t)bpmArrTwo[6] << 8) | bpmArrTwo[7];
        body->extStatus = (int8_t)bpmArrTwo[8];
        return SFE_BIO_SUCCESS;
    }

    return ERR_UNKNOWN;
}

uint8_t biohub_readSensorHubStatus(biohub_t *dev)
{
    uint8_t cmd[2] = {HUB_STATUS, 0x00};
    uint8_t statusByte = ERR_UNKNOWN;

    if (dev == NULL) {
        return ERR_UNKNOWN;
    }

    if (!i2c_tx(dev, cmd, sizeof(cmd))) {
        return ERR_UNKNOWN;
    }

    k_msleep(CMD_DELAY);

    if (!i2c_rx(dev, &statusByte, 1)) {
        return ERR_UNKNOWN;
    }

    return statusByte;
}

uint8_t biohub_numSamplesOutFifo(biohub_t *dev)
{
    if (dev == NULL) {
        return 0;
    }

    return readByte(dev, READ_DATA_OUTPUT, NUM_SAMPLES);
}

uint8_t biohub_readDeviceMode(biohub_t *dev, uint8_t *mode)
{
    if (dev == NULL || mode == NULL) {
        return ERR_UNKNOWN;
    }

    *mode = readByte(dev, READ_DEVICE_MODE, 0x00);
    return SFE_BIO_SUCCESS;
}

uint8_t biohub_readSensorHubVersion(biohub_t *dev, version *vers)
{
    uint8_t cmd[2] = {IDENTITY, READ_SENSOR_HUB_VERS};
    uint8_t rx[4] = {0};

    if (dev == NULL || vers == NULL) {
        return ERR_UNKNOWN;
    }

    if (!i2c_tx(dev, cmd, sizeof(cmd))) {
        return ERR_UNKNOWN;
    }

    k_msleep(CMD_DELAY);

    if (!i2c_rx(dev, rx, sizeof(rx))) {
        return ERR_UNKNOWN;
    }

    vers->major = rx[1];
    vers->minor = rx[2];
    vers->revision = rx[3];

    return rx[0];
}
