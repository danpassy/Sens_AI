#include "sd.h"

#include <errno.h>
#include <ff.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT "/" DISK_DRIVE_NAME ":"
#define CSV_FILE_NAME "imu_log.csv"
#define CSV_HEADER "timestamp_ms,temperature_c,gyro_x,gyro_y,gyro_z,accel_x,accel_y,accel_z\n"
#define SESSION_FILE_PREFIX "ACQ_"
#define SESSION_FILE_SUFFIX ".csv"

#define SD_INIT_RETRY_COUNT 3
#define FS_MOUNT_RETRY_COUNT 5
#define RETRY_DELAY_MS 500

static FATFS fat_fs;
static struct fs_mount_t mount_point = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .storage_dev = (void *)DISK_DRIVE_NAME,
    .mnt_point = DISK_MOUNT_PT,
};

static bool g_disk_initialized;
static bool g_fs_mounted;
static bool g_csv_header_checked;
static bool g_session_active;
static uint8_t g_session_activity_index;
static char g_session_file_name[48];

static const char *const g_activity_prefixes[] = {
    "COURSE",
    "MARCHE",
    "VELO",
    "FITNESS",
    "YOGA",
    "AUTRE",
};

static int build_path(const char *file_name, char *path, size_t path_len)
{
    int n;

    if (file_name == NULL || path == NULL || path_len == 0U) {
        return -EINVAL;
    }

    if (file_name[0] == '/') {
        n = snprintf(path, path_len, "%s%s", DISK_MOUNT_PT, file_name);
    } else {
        n = snprintf(path, path_len, "%s/%s", DISK_MOUNT_PT, file_name);
    }

    if (n < 0 || (size_t)n >= path_len) {
        return -ENAMETOOLONG;
    }

    return 0;
}

static int fs_mount_with_retry(void)
{
    int ret = -1;

    for (int attempt = 1; attempt <= FS_MOUNT_RETRY_COUNT; attempt++) {
        ret = fs_mount(&mount_point);
        if (ret == FR_OK) {
            printk("SD: mount OK (%d/%d)\n", attempt, FS_MOUNT_RETRY_COUNT);
            return 0;
        }

        printk("SD: mount failed (%d), retry %d/%d\n",
               ret, attempt, FS_MOUNT_RETRY_COUNT);
        k_sleep(K_MSEC(RETRY_DELAY_MS));
    }

    return ret;
}

static bool activity_index_valid(uint8_t activity_index)
{
    return activity_index < ARRAY_SIZE(g_activity_prefixes);
}

static bool session_name_matches(const char *name, const char *activity_prefix, int *out_index)
{
    const size_t prefix_len = strlen(SESSION_FILE_PREFIX);
    const size_t activity_len = strlen(activity_prefix);
    const size_t suffix_len = strlen(SESSION_FILE_SUFFIX);
    const char *digits;
    size_t name_len;
    int value = 0;

    if (name == NULL || activity_prefix == NULL || out_index == NULL) {
        return false;
    }

    name_len = strlen(name);
    if (name_len <= (prefix_len + activity_len + 1U + suffix_len)) {
        return false;
    }

    if (strncmp(name, SESSION_FILE_PREFIX, prefix_len) != 0) {
        return false;
    }

    if (strncmp(name + prefix_len, activity_prefix, activity_len) != 0) {
        return false;
    }

    if (name[prefix_len + activity_len] != '_') {
        return false;
    }

    if (strcmp(name + name_len - suffix_len, SESSION_FILE_SUFFIX) != 0) {
        return false;
    }

    digits = name + prefix_len + activity_len + 1U;
    for (const char *p = digits; p < (name + name_len - suffix_len); p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = (value * 10) + (*p - '0');
    }

    *out_index = value;
    return true;
}

static int find_next_session_index(const char *activity_prefix, int *out_next_index)
{
    struct fs_dir_t dir;
    struct fs_dirent entry;
    int ret;
    int max_index = 0;

    if (activity_prefix == NULL || out_next_index == NULL) {
        return -EINVAL;
    }

    fs_dir_t_init(&dir);

    ret = fs_opendir(&dir, DISK_MOUNT_PT);
    if (ret != 0) {
        return ret;
    }

    for (;;) {
        int idx = 0;

        ret = fs_readdir(&dir, &entry);
        if (ret != 0 || entry.name[0] == '\0') {
            break;
        }

        if (entry.type != FS_DIR_ENTRY_FILE) {
            continue;
        }

        if (session_name_matches(entry.name, activity_prefix, &idx) && idx > max_index) {
            max_index = idx;
        }
    }

    (void)fs_closedir(&dir);
    if (ret != 0) {
        return ret;
    }

    *out_next_index = max_index + 1;
    return 0;
}

static int build_session_file_name(uint8_t activity_index, char *file_name, size_t file_name_len)
{
    const char *prefix;
    int next_index;
    int ret;
    int n;

    if (!activity_index_valid(activity_index) || file_name == NULL || file_name_len == 0U) {
        return -EINVAL;
    }

    prefix = g_activity_prefixes[activity_index];
    ret = find_next_session_index(prefix, &next_index);
    if (ret != 0) {
        return ret;
    }

    n = snprintf(file_name, file_name_len, "%s%s_%03d%s",
                 SESSION_FILE_PREFIX, prefix, next_index, SESSION_FILE_SUFFIX);
    if (n < 0 || (size_t)n >= file_name_len) {
        return -ENAMETOOLONG;
    }

    return 0;
}

static int ensure_header_for_file(const char *file_name, const char *header_line)
{
    char file_path[128];
    struct fs_dirent entry = {0};
    struct fs_file_t file;
    bool write_header = false;
    int ret;
    ssize_t written;
    size_t header_len;

    if (file_name == NULL || header_line == NULL) {
        return -EINVAL;
    }

    ret = build_path(file_name, file_path, sizeof(file_path));
    if (ret != 0) {
        return ret;
    }

    ret = fs_stat(file_path, &entry);
    if (ret == -ENOENT) {
        write_header = true;
    } else if (ret == 0) {
        write_header = (entry.size == 0U);
    } else {
        return ret;
    }

    fs_file_t_init(&file);
    ret = fs_open(&file, file_path, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
    if (ret != 0) {
        return ret;
    }

    if (write_header) {
        header_len = strlen(header_line);
        written = fs_write(&file, header_line, header_len);
        if (written < 0) {
            (void)fs_close(&file);
            return (int)written;
        }
        if ((size_t)written != header_len) {
            (void)fs_close(&file);
            return -EIO;
        }
    }

    ret = fs_sync(&file);
    (void)fs_close(&file);
    return ret;
}

static int ensure_csv_header(void)
{
    int ret;

    if (g_csv_header_checked) {
        return 0;
    }

    ret = ensure_header_for_file(CSV_FILE_NAME, CSV_HEADER);
    if (ret != 0) {
        return ret;
    }

    g_csv_header_checked = true;
    return 0;
}

const char *app_sd_mount_point(void)
{
    return DISK_MOUNT_PT;
}

int app_sd_init(void)
{
    uint32_t block_count = 0U;
    uint32_t block_size = 0U;
    uint64_t memory_size_mb;
    int ret = -1;

    if (g_fs_mounted) {
        return 0;
    }

    for (int attempt = 1; attempt <= SD_INIT_RETRY_COUNT; attempt++) {
        printk("SD: init attempt %d/%d\n", attempt, SD_INIT_RETRY_COUNT);
        ret = disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_CTRL_INIT, NULL);
        if (ret == 0) {
            g_disk_initialized = true;
            break;
        }

        printk("SD: init failed (%d), retry %d/%d\n",
               ret, attempt, SD_INIT_RETRY_COUNT);
        k_sleep(K_MSEC(RETRY_DELAY_MS));
    }

    if (!g_disk_initialized) {
        if (ret == -ETIMEDOUT) {
            printk("SD: timeout waiting card response (CMD0/CMD8). "
                   "Check 3.3V power and SPI wiring (CS/SCK/MISO/MOSI).\n");
        }
        return ret;
    }

    ret = disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &block_count);
    if (ret != 0) {
        return ret;
    }

    ret = disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &block_size);
    if (ret != 0) {
        return ret;
    }

    memory_size_mb = ((uint64_t)block_count * block_size) >> 20;
    printk("SD: blocks=%u block=%u size=%llu MB\n",
           block_count, block_size, (unsigned long long)memory_size_mb);

    ret = fs_mount_with_retry();
    if (ret != 0) {
        (void)disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_CTRL_DEINIT, NULL);
        g_disk_initialized = false;
        return ret;
    }

    g_fs_mounted = true;
    g_csv_header_checked = false;
    g_session_active = false;
    g_session_activity_index = 0U;
    g_session_file_name[0] = '\0';

    ret = ensure_csv_header();
    if (ret != 0) {
        printk("SD: CSV header setup failed (%d)\n", ret);
        return ret;
    }

    return 0;
}

static int append_imu_csv_to_file(const char *file_name,
                                  int64_t timestamp_ms,
                                  double temperature_c,
                                  double gyro_x,
                                  double gyro_y,
                                  double gyro_z,
                                  double accel_x,
                                  double accel_y,
                                  double accel_z)
{
    char file_path[128];
    char line[192];
    struct fs_file_t file;
    int ret;
    int n;
    ssize_t written;

    if (!g_fs_mounted) {
        return -EACCES;
    }

    ret = build_path(file_name, file_path, sizeof(file_path));
    if (ret != 0) {
        return ret;
    }

    n = snprintf(line, sizeof(line),
                 "%lld,%.2f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                 (long long)timestamp_ms,
                 temperature_c,
                 gyro_x, gyro_y, gyro_z,
                 accel_x, accel_y, accel_z);
    if (n < 0 || (size_t)n >= sizeof(line)) {
        return -ENAMETOOLONG;
    }

    fs_file_t_init(&file);
    ret = fs_open(&file, file_path, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
    if (ret != 0) {
        return ret;
    }

    written = fs_write(&file, line, (size_t)n);
    if (written < 0) {
        (void)fs_close(&file);
        return (int)written;
    }
    if (written != n) {
        (void)fs_close(&file);
        return -EIO;
    }

    ret = fs_sync(&file);
    (void)fs_close(&file);
    return ret;
}

int app_sd_append_imu_csv(int64_t timestamp_ms,
                          double temperature_c,
                          double gyro_x,
                          double gyro_y,
                          double gyro_z,
                          double accel_x,
                          double accel_y,
                          double accel_z)
{
    int ret;

    ret = ensure_csv_header();
    if (ret != 0) {
        return ret;
    }

    return append_imu_csv_to_file(CSV_FILE_NAME,
                                  timestamp_ms,
                                  temperature_c,
                                  gyro_x, gyro_y, gyro_z,
                                  accel_x, accel_y, accel_z);
}

int app_sd_start_session(uint8_t activity_index)
{
    int ret;

    if (!g_fs_mounted) {
        return -EACCES;
    }

    if (!activity_index_valid(activity_index)) {
        return -EINVAL;
    }

    ret = build_session_file_name(activity_index, g_session_file_name, sizeof(g_session_file_name));
    if (ret != 0) {
        return ret;
    }

    ret = ensure_header_for_file(g_session_file_name, CSV_HEADER);
    if (ret != 0) {
        return ret;
    }

    g_session_activity_index = activity_index;
    g_session_active = true;

    printk("SD: session start activity=%u file=%s\n",
           (unsigned int)g_session_activity_index,
           g_session_file_name);
    return 0;
}

int app_sd_stop_session(void)
{
    if (!g_fs_mounted) {
        return -EACCES;
    }

    if (g_session_active) {
        printk("SD: session stop file=%s\n", g_session_file_name);
    }

    g_session_active = false;
    return 0;
}

bool app_sd_session_active(void)
{
    return g_session_active;
}

const char *app_sd_current_session_file(void)
{
    return g_session_active ? g_session_file_name : "";
}

int app_sd_append_session_imu_csv(int64_t timestamp_ms,
                                  double temperature_c,
                                  double gyro_x,
                                  double gyro_y,
                                  double gyro_z,
                                  double accel_x,
                                  double accel_y,
                                  double accel_z)
{
    if (!g_session_active) {
        return -EACCES;
    }

    return append_imu_csv_to_file(g_session_file_name,
                                  timestamp_ms,
                                  temperature_c,
                                  gyro_x, gyro_y, gyro_z,
                                  accel_x, accel_y, accel_z);
}

int app_sd_deinit(void)
{
    int ret;

    if (g_fs_mounted) {
        ret = fs_unmount(&mount_point);
        if (ret != FR_OK) {
            return ret;
        }
        g_fs_mounted = false;
    }

    if (g_disk_initialized) {
        ret = disk_access_ioctl(DISK_DRIVE_NAME, DISK_IOCTL_CTRL_DEINIT, NULL);
        if (ret != 0) {
            return ret;
        }
        g_disk_initialized = false;
    }

    g_session_active = false;
    g_session_activity_index = 0U;
    g_session_file_name[0] = '\0';

    return 0;
}
