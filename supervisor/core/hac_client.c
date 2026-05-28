#include "hac_client.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>

/* ============== Client Management ============== */

int hac_client_open(hac_client_t *client)
{
  if (!client) return HAC_EINVAL;
  
  /* Open /dev/hac */
  int fd = open(HAC_DEV_PATH, O_RDWR);
  if (fd < 0) {
    perror("hac_client_open");
    return HAC_ESYSCALL;
  }
  
  /* Get our VMID */
  uint32_t vmid;
  if (ioctl(fd, HAC_IOCTL_VMID, &vmid) < 0) {
    perror("HAC_IOCTL_VMID");
    close(fd);
    return HAC_ESYSCALL;
  }
  
  client->fd = fd;
  client->vmid = vmid;
  client->caps = HAC_CAP_DISPLAY | HAC_CAP_TOUCH;  /* Default caps */
  
  return HAC_OK;
}

void hac_client_close(hac_client_t *client)
{
  if (client && client->fd >= 0) {
    close(client->fd);
    client->fd = -1;
  }
}

/* ============== HVC Interface ============== */

int hac_hvc(hac_client_t *client, uint32_t hvc_id,
            const uint64_t *args, size_t arg_count,
            uint64_t *result)
{
  if (!client || client->fd < 0) return HAC_EINVAL;
  if (arg_count > 6) return HAC_EINVAL;
  
  struct hac_hvc_req req = {
    .hvc_id = hvc_id,
  };
  
  /* Copy arguments */
  if (args && arg_count > 0) {
    memcpy(req.args, args, arg_count * sizeof(uint64_t));
  }
  
  /* Issue ioctl */
  if (ioctl(client->fd, HAC_IOCTL_HVC, &req) < 0) {
    perror("HAC_IOCTL_HVC");
    return HAC_ESYSCALL;
  }
  
  if (result) {
    *result = req.result;
  }
  
  /* Check return value for errors */
  if ((int64_t)req.result < 0) {
    return (int)req.result;
  }
  
  return HAC_OK;
}

/* ============== Capability Management ============== */

int hac_grant_capability(hac_client_t *client, uint32_t target_vmid,
                         uint32_t cap_mask, int grant)
{
  if (!client || client->fd < 0) return HAC_EINVAL;
  
  struct hac_caps_req req = {
    .vmid = target_vmid,
    .caps = cap_mask,
    .grant = grant ? 1 : 0,
  };
  
  if (ioctl(client->fd, HAC_IOCTL_CAPS, &req) < 0) {
    perror("HAC_IOCTL_CAPS");
    return HAC_ESYSCALL;
  }
  
  return HAC_OK;
}

/* ============== IPC Event Reading ============== */

int hac_ipc_read(hac_client_t *client, struct hac_ipc_msg *msg,
                 int timeout_ms)
{
  if (!client || client->fd < 0 || !msg) return HAC_EINVAL;
  
  /* Poll for readability */
  struct pollfd pfd = {
    .fd = client->fd,
    .events = POLLIN,
  };
  
  int ret = poll(&pfd, 1, timeout_ms);
  if (ret < 0) {
    perror("poll");
    return HAC_ESYSCALL;
  }
  if (ret == 0) {
    return HAC_EBUSY;  /* Timeout */
  }
  
  /* Read message */
  ssize_t n = read(client->fd, msg, sizeof(*msg));
  if (n < 0) {
    perror("read /dev/hac");
    return HAC_ESYSCALL;
  }
  if (n != sizeof(*msg)) {
    fprintf(stderr, "hac_ipc_read: partial read (%zd bytes)\n", n);
    return HAC_EINVAL;
  }
  
  return HAC_OK;
}

/* ============== Hardware Abstraction Layer ============== */

int hac_display_blit(hac_client_t *client, uint64_t framebuffer_pa,
                     uint32_t width, uint32_t height, uint32_t region_id)
{
  if (!client) return HAC_EINVAL;
  if (!(client->caps & HAC_CAP_DISPLAY)) return HAC_EPERM;
  
  uint64_t args[4] = {
    framebuffer_pa,
    ((uint64_t)height << 32) | width,
    region_id,
    0,
  };
  
  uint64_t result;
  return hac_hvc(client, 0x0001, args, 4, &result);
}

int hac_touch_poll(hac_client_t *client, uint32_t slot_idx,
                   uint16_t *x, uint16_t *y, uint8_t *pressure)
{
  if (!client || !x || !y || !pressure) return HAC_EINVAL;
  if (!(client->caps & HAC_CAP_TOUCH)) return HAC_EPERM;
  
  uint64_t args[2] = {
    slot_idx,
    0,
  };
  
  uint64_t result;
  int ret = hac_hvc(client, 0x0100, args, 2, &result);
  if (ret != HAC_OK) return ret;
  
  /* Parse result: [63:48]=x, [47:32]=y, [31:24]=pressure, [23:0]=status */
  *x = (result >> 48) & 0xFFFF;
  *y = (result >> 32) & 0xFFFF;
  *pressure = (result >> 24) & 0xFF;
  
  return HAC_OK;
}

int hac_storage_read(hac_client_t *client, uint64_t lba,
                     uint32_t count, uint64_t buffer_pa)
{
  if (!client) return HAC_EINVAL;
  if (!(client->caps & HAC_CAP_STORAGE)) return HAC_EPERM;
  
  uint64_t args[3] = {
    lba,
    count,
    buffer_pa,
  };
  
  uint64_t result;
  return hac_hvc(client, 0x0200, args, 3, &result);
}

int hac_storage_write(hac_client_t *client, uint64_t lba,
                      uint32_t count, uint64_t buffer_pa)
{
  if (!client) return HAC_EINVAL;
  if (!(client->caps & HAC_CAP_STORAGE)) return HAC_EPERM;
  
  uint64_t args[3] = {
    lba,
    count,
    buffer_pa,
  };
  
  uint64_t result;
  return hac_hvc(client, 0x0201, args, 3, &result);
}
