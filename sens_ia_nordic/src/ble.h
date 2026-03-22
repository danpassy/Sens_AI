#ifndef BLE_H
#define BLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ble_init(void);
int ble_start_advertising(void);
bool ble_is_connected(void);
bool ble_streaming_enabled(void);
uint8_t ble_selected_activity_index(void);

int ble_notify_temperature_centi(int16_t temperature_centi_c);
int ble_notify_motion(int16_t gyro_x_centi_dps,
                      int16_t gyro_y_centi_dps,
                      int16_t gyro_z_centi_dps,
                      uint16_t pitch_centi_deg,
                      uint16_t roll_centi_deg);
int ble_notify_biometrics(uint16_t heart_rate_bpm, uint16_t spo2_percent);
int ble_notify_activity(uint8_t activity_class, float confidence);

#ifdef __cplusplus
}
#endif

#endif /* BLE_H */
