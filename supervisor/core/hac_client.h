#ifndef HAC_CLIENT_H
#define HAC_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <sys/ioctl.h>

/* ============== HAC Device Interface ============== */

#define HAC_DEV_PATH "/dev/hac"

/* Error codes */
typedef enum {
  HAC_OK = 0,
  HAC_EPERM = -1,      /* Permission denied */
  HAC_EINVAL = -2,     /* Invalid argument */
  HAC_ENOENT = -3,     /* Not found */
  HAC_EBUSY = -4,      /* Resource busy */
  HAC_ENOMEM = -5,     /* Out of memory */
  HAC_ESYSCALL = -6,   /* System call failed */
} hac_error_t;

/* ============== Capability Bits ============== */

#define HAC_CAP_DISPLAY    (1 << 0)   /* Display framebuffer blit */
#define HAC_CAP_TOUCH      (1 << 1)   /* Touchscreen poll */
#define HAC_CAP_STORAGE    (1 << 2)   /* NVMe read/write */
#define HAC_CAP_WIFI       (1 << 3)   /* WiFi TX/RX */
#define HAC_CAP_GPIO       (1 << 4)   /* GPIO set/get */
#define HAC_CAP_I2C        (1 << 5)   /* I2C sensor read */
#define HAC_CAP_SPI        (1 << 6)   /* SPI control */
#define HAC_CAP_CAMERA     (1 << 7)   /* Camera capture */
#define HAC_CAP_AUDIO      (1 << 8)   /* Audio playback/record */
#define HAC_CAP_MODEM      (1 << 9)   /* Cellular modem */
#define HAC_CAP_POWER      (1 << 10)  /* Power state control */
#define HAC_CAP_SENSOR     (1 << 11)  /* Motion/env sensors */
#define HAC_CAP_ALL        0xFFF      /* All capabilities */

/* ============== ioctl Commands ============== */

#define HAC_IOCTL_BASE 0xHA

struct hac_caps_req {
  uint32_t vmid;          /* Virtual machine ID */
  uint32_t caps;          /* Capability bitmask */
  uint32_t grant;         /* 1=grant, 0=revoke */
};

struct hac_hvc_req {
  uint32_t hvc_id;        /* HVC call ID */
  uint64_t args[6];       /* x1-x6 arguments */
  uint64_t result;        /* x0 return value (filled by HAC) */
};

#define HAC_IOCTL_CAPS   _IOWR(HAC_IOCTL_BASE, 1, struct hac_caps_req)
#define HAC_IOCTL_HVC    _IOWR(HAC_IOCTL_BASE, 2, struct hac_hvc_req)
#define HAC_IOCTL_VMID   _IOR(HAC_IOCTL_BASE, 3, uint32_t)

/* ============== IPC Message Structure ============== */

struct hac_ipc_msg {
  uint32_t type;          /* Message type (event code) */
  uint32_t vmid;          /* Source VMID */
  uint64_t payload[4];    /* Event-specific data */
};

/* IPC event types */
#define HAC_IPC_TOUCH_EVENT   0x1000
#define HAC_IPC_WIFI_RX       0x2000
#define HAC_IPC_SENSOR_DATA   0x3000
#define HAC_IPC_AUDIO_NOTIFY  0x4000

/* ============== C API ============== */

typedef struct hac_client {
  int fd;                 /* /dev/hac file descriptor */
  uint32_t vmid;          /* This process's VMID */
  uint32_t caps;          /* Granted capabilities */
} hac_client_t;

/* Open /dev/hac and get VMID */
int hac_client_open(hac_client_t *client);

/* Close /dev/hac */
void hac_client_close(hac_client_t *client);

/* Issue HVC call directly */
int hac_hvc(hac_client_t *client, uint32_t hvc_id, 
            const uint64_t *args, size_t arg_count, 
            uint64_t *result);

/* Grant/revoke capability to another VMID */
int hac_grant_capability(hac_client_t *client, uint32_t target_vmid, 
                         uint32_t cap_mask, int grant);

/* Wait for IPC event with timeout (ms) */
int hac_ipc_read(hac_client_t *client, struct hac_ipc_msg *msg, 
                 int timeout_ms);

/* Display framebuffer blit */
int hac_display_blit(hac_client_t *client, uint64_t framebuffer_pa, 
                     uint32_t width, uint32_t height, uint32_t region_id);

/* Touchscreen poll */
int hac_touch_poll(hac_client_t *client, uint32_t slot_idx, 
                   uint16_t *x, uint16_t *y, uint8_t *pressure);

/* Storage read */
int hac_storage_read(hac_client_t *client, uint64_t lba, 
                     uint32_t count, uint64_t buffer_pa);

/* Storage write */
int hac_storage_write(hac_client_t *client, uint64_t lba, 
                      uint32_t count, uint64_t buffer_pa);

#endif /* HAC_CLIENT_H */
