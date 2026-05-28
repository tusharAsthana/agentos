/*
 * AgentOS HAC — Hardware Primitive Interface
 *
 * This is the contract between EL2 (HAC) and EL1 (Linux kernel).
 * Every hardware operation is expressed as a named primitive with
 * a fixed HVC number and a structured argument/return layout.
 *
 * Calling convention from EL1:
 *   x0 = hvc_id (one of HAC_HVC_*)
 *   x1..x7 = arguments (primitive-specific)
 *   hvc #0
 *   x0 = return value (0 = success, negative = error)
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------
 * HVC call numbers
 * --------------------------------------------------------------- */
typedef enum {
    /* System */
    HAC_HVC_VERSION         = 0x0000,  /* x0=0 → returns HAC version */
    HAC_HVC_GET_CAPS        = 0x0001,  /* Get capability bitmask for caller VMID */
    HAC_HVC_GRANT_CAP       = 0x0002,  /* Supervisor grants cap to app VMID */
    HAC_HVC_REVOKE_CAP      = 0x0003,
    HAC_HVC_AUDIT_LOG       = 0x0004,  /* Read next audit log entry */

    /* Display */
    HAC_HVC_DISPLAY_BLIT    = 0x0100,  /* x1=fb_pa x2=x x3=y x4=w x5=h */
    HAC_HVC_DISPLAY_BLANK   = 0x0101,  /* x1=1 to blank, 0 to unblank */
    HAC_HVC_DISPLAY_BRIGHT  = 0x0102,  /* x1=brightness 0-255 */
    HAC_HVC_DISPLAY_INFO    = 0x0103,  /* Returns FB PA in x0, geometry in x1/x2 */

    /* Touch */
    HAC_HVC_TOUCH_POLL      = 0x0200,  /* Fills touch_event at x1=buf_pa, x2=max_events */
    HAC_HVC_TOUCH_ENABLE    = 0x0201,

    /* WiFi */
    HAC_HVC_WIFI_TX         = 0x0300,  /* x1=pkt_pa x2=len */
    HAC_HVC_WIFI_RX         = 0x0301,  /* x1=buf_pa x2=max_len → returns actual len */
    HAC_HVC_WIFI_SCAN       = 0x0302,  /* x1=results_pa x2=max_results → count */
    HAC_HVC_WIFI_CONNECT    = 0x0303,  /* x1=ssid_pa x2=ssid_len x3=key_pa x4=key_len */
    HAC_HVC_WIFI_DISCONNECT = 0x0304,
    HAC_HVC_WIFI_STATUS     = 0x0305,  /* Returns wifi_status struct at x1=buf_pa */

    /* Storage */
    HAC_HVC_STORAGE_READ    = 0x0400,  /* x1=lba x2=count x3=buf_pa */
    HAC_HVC_STORAGE_WRITE   = 0x0401,  /* x1=lba x2=count x3=buf_pa */
    HAC_HVC_STORAGE_INFO    = 0x0402,  /* Returns total_lba in x0, lba_size in x1 */
    HAC_HVC_STORAGE_FLUSH   = 0x0403,

    /* GPIO */
    HAC_HVC_GPIO_SET        = 0x0500,  /* x1=pin x2=value(0/1) */
    HAC_HVC_GPIO_GET        = 0x0501,  /* x1=pin → returns 0 or 1 in x0 */
    HAC_HVC_GPIO_DIRECTION  = 0x0502,  /* x1=pin x2=dir(0=in,1=out) */

    /* Sensors */
    HAC_HVC_SENSOR_READ     = 0x0600,  /* x1=sensor_id x2=buf_pa → fills sensor_data */
    HAC_HVC_SENSOR_ENABLE   = 0x0601,  /* x1=sensor_id x2=enable x3=rate_hz */

    /* Camera */
    HAC_HVC_CAMERA_CAPTURE  = 0x0700,  /* x1=buf_pa x2=buf_size → returns frame size */
    HAC_HVC_CAMERA_STREAM   = 0x0701,  /* x1=buf_pa x2=buf_size x3=fps */
    HAC_HVC_CAMERA_STOP     = 0x0702,

    /* Audio */
    HAC_HVC_AUDIO_PLAY      = 0x0800,  /* x1=buf_pa x2=len x3=sample_rate x4=channels */
    HAC_HVC_AUDIO_RECORD    = 0x0801,  /* x1=buf_pa x2=max_len → returns bytes captured */
    HAC_HVC_AUDIO_STOP      = 0x0802,
    HAC_HVC_AUDIO_VOLUME    = 0x0803,  /* x1=volume 0-100 */

    /* Power */
    HAC_HVC_POWER_SLEEP     = 0x0900,  /* x1=level (0=idle, 1=suspend, 2=deep) */
    HAC_HVC_POWER_WAKE      = 0x0901,
    HAC_HVC_BATTERY_STATUS  = 0x0902,  /* Returns battery_status at x1=buf_pa */

    /* Cellular */
    HAC_HVC_MODEM_AT        = 0x0A00,  /* x1=cmd_pa x2=cmd_len x3=resp_pa x4=resp_max */

    /* SEP */
    HAC_HVC_SEP_ENCRYPT     = 0x0B00,  /* x1=in_pa x2=len x3=out_pa → uses SEP AES */
    HAC_HVC_SEP_DECRYPT     = 0x0B01,

    /* LLM Inference (HAC-side model) */
    HAC_HVC_INFER_PROMPT    = 0x0C00,  /* x1=prompt_pa x2=len x3=out_pa x4=out_max */
    HAC_HVC_INFER_STATUS    = 0x0C01,  /* Returns 0=busy 1=ready */

    HAC_HVC_MAX             = 0x0FFF
} hac_hvc_t;

/* ---------------------------------------------------------------
 * Error codes returned in x0
 * --------------------------------------------------------------- */
#define HAC_OK              0
#define HAC_EPERM           -1   /* No capability */
#define HAC_EINVAL          -2   /* Bad argument */
#define HAC_EBUSY           -3   /* Hardware busy */
#define HAC_ETIMEOUT        -4
#define HAC_ENOMEM          -5
#define HAC_ENODEV          -6   /* Device not present / not initialized */

/* ---------------------------------------------------------------
 * Sensor IDs (for HAC_HVC_SENSOR_READ)
 * --------------------------------------------------------------- */
typedef enum {
    SENSOR_ACCEL        = 0,   /* 3-axis accelerometer (m/s²) */
    SENSOR_GYRO         = 1,   /* 3-axis gyroscope (rad/s) */
    SENSOR_BARO         = 2,   /* Barometric pressure (Pa) */
    SENSOR_PROXIMITY    = 3,   /* Proximity (mm) */
    SENSOR_AMBIENT      = 4,   /* Ambient light (lux) */
    SENSOR_COMPASS      = 5,   /* Magnetometer (µT) */
    SENSOR_TEMP         = 6,   /* Die temperature (°C * 100) */
    SENSOR_MAX          = 7
} sensor_id_t;

/* ---------------------------------------------------------------
 * Shared data structures (packed, written into caller-supplied buffers)
 * --------------------------------------------------------------- */

/* Touch event (filled by HAC_HVC_TOUCH_POLL) */
typedef struct __attribute__((packed)) {
    uint16_t x;
    uint16_t y;
    uint8_t  pressure;    /* 0-255 */
    uint8_t  id;          /* finger slot 0-4 */
    uint8_t  state;       /* 0=up 1=down 2=move */
    uint8_t  _reserved;
} touch_event_t;

/* Sensor data (filled by HAC_HVC_SENSOR_READ) */
typedef struct __attribute__((packed)) {
    sensor_id_t id;
    int32_t  x, y, z;       /* Primary axes */
    int32_t  scalar;         /* Single-value sensors */
    uint64_t timestamp_ns;
} sensor_data_t;

/* Battery status (filled by HAC_HVC_BATTERY_STATUS) */
typedef struct __attribute__((packed)) {
    uint8_t  present;
    uint8_t  charging;
    uint8_t  charge_pct;   /* 0-100 */
    int16_t  temp_deciC;   /* Temperature in 0.1°C */
    uint16_t voltage_mV;
    int16_t  current_mA;   /* negative = discharging */
} battery_status_t;

/* WiFi status */
typedef struct __attribute__((packed)) {
    uint8_t  connected;
    uint8_t  ssid[32];
    uint8_t  bssid[6];
    int8_t   rssi;
    uint8_t  _pad;
    uint32_t ip4_addr;
    uint32_t ip4_mask;
    uint32_t ip4_gw;
} wifi_status_t;

/* ---------------------------------------------------------------
 * HAC primitive initialization (called from hac_main)
 * --------------------------------------------------------------- */
void primitives_init(void);

/* ---------------------------------------------------------------
 * HVC dispatcher (called from entry.S on every HVC trap)
 * Returns value placed in caller's x0
 * --------------------------------------------------------------- */
int64_t hvc_dispatch(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3,
                     uint64_t x4, uint64_t x5, uint64_t x6, uint64_t x7,
                     uint64_t hvc_nr);
