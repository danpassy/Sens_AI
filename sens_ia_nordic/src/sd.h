#ifndef SD_H_
#define SD_H_

#include <stdbool.h>
#include <stdint.h>

int app_sd_init(void);
int app_sd_deinit(void);
int app_sd_append_imu_csv(int64_t timestamp_ms,
                          double temperature_c,
                          double gyro_x,
                          double gyro_y,
                          double gyro_z,
                          double accel_x,
                          double accel_y,
                          double accel_z);
int app_sd_start_session(uint8_t activity_index);
int app_sd_stop_session(void);
bool app_sd_session_active(void);
const char *app_sd_current_session_file(void);
int app_sd_append_session_imu_csv(int64_t timestamp_ms,
                                  double temperature_c,
                                  double gyro_x,
                                  double gyro_y,
                                  double gyro_z,
                                  double accel_x,
                                  double accel_y,
                                  double accel_z);
const char *app_sd_mount_point(void);

#endif /* SD_H_ */
