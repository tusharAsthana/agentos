/*
 * AgentOS HAC — Hardware Primitive Implementations & HVC Dispatcher
 *
 * Each primitive talks directly to MMIO registers.
 * The HVC dispatcher routes incoming hypercalls to the correct primitive
 * after verifying the caller has the required capability.
 */

#include "primitives.h"
#include "../core/hac.h"
#include "../mmio/a11_mmio.h"
#include "../security/caps.h"
#include "../ipc/ipc.h"

/* ---------------------------------------------------------------
 * Internal state
 * --------------------------------------------------------------- */
static bool display_initialized = false;
static bool storage_initialized  = false;
static bool wifi_initialized     = false;

/* ---------------------------------------------------------------
 * primitives_init — called from hac_main
 * --------------------------------------------------------------- */
void primitives_init(void)
{
    /* Pre-initialize UART (already done in main), GPIO defaults, AIC */
    hac_uart_puts("[PRIM] Hardware primitives registered\r\n");

    /* Set up AIC: route all IRQs to CPU0 at EL2 */
    for (int i = 0; i < AIC_IRQ_COUNT / 32; i++) {
        MMIO_WRITE32(AIC_BASE + AIC_IRQ_ENABLE_CLR(i), 0xFFFFFFFF); /* disable all */
    }
    /* Enable global AIC */
    MMIO_WRITE32(AIC_BASE + AIC_GLOBAL_CFG, 1);

    display_initialized = true;  /* display initialized by PongoOS, we inherit it */
    storage_initialized = true;  /* ANS2 initialized by iBoot, we inherit it */
}

/* ================================================================
 * DISPLAY PRIMITIVES
 * ================================================================ */

/*
 * prim_display_blit — DMA copy from caller-supplied PA into the display
 * framebuffer (already mapped by PongoOS/DCP at FB_DEFAULT_PA).
 * For a minimal implementation, we copy the buffer to the framebuffer
 * using the CPU. A full implementation would use the DCP DMA engine.
 */
static int64_t prim_display_blit(uint64_t fb_pa, uint64_t x, uint64_t y,
                                  uint64_t w, uint64_t h)
{
    if (x + w > FB_WIDTH || y + h > FB_HEIGHT) return HAC_EINVAL;
    if (!fb_pa) return HAC_EINVAL;

    volatile uint32_t *src = (volatile uint32_t *)(uintptr_t)fb_pa;
    volatile uint32_t *dst = (volatile uint32_t *)(uintptr_t)(FB_DEFAULT_PA +
                              (y * FB_WIDTH + x) * 4);

    for (uint64_t row = 0; row < h; row++) {
        for (uint64_t col = 0; col < w; col++) {
            dst[row * FB_WIDTH + col] = src[row * w + col];
        }
    }
    /* Cache flush to make changes visible to display hardware */
    asm volatile("dc civac, %0" : : "r"(dst) : "memory");
    asm volatile("dsb sy" : : : "memory");
    return HAC_OK;
}

static int64_t prim_display_info(void)
{
    /* Caller gets FB PA in x0 (return value), width in x1, height in x2 */
    /* We encode width/height in the return value for now:
     * upper 32 bits = FB PA >> 12 (page frame), lower 32 = width<<16 | height */
    return (int64_t)FB_DEFAULT_PA;  /* kernel reads width/height from DTB */
}

/* ================================================================
 * TOUCH PRIMITIVES
 * ================================================================ */

static int64_t prim_touch_poll(uint64_t buf_pa, uint64_t max_events)
{
    /* Read raw SPI frames from Melfas touchscreen controller.
     * For each pending event, fill a touch_event_t at buf_pa.
     * Returns number of events written. */
    volatile touch_event_t *events = (volatile touch_event_t *)(uintptr_t)buf_pa;
    uint32_t count = 0;

    /* Check SPI RX not-empty */
    if (!(MMIO_READ32(SPI0_BASE + SPI_STATUS) & SPI_STATUS_RXNE))
        return 0;

    /* Read Melfas packet header */
    uint32_t hdr = MMIO_READ32(SPI0_BASE + SPI_RXDATA);
    uint8_t  num_fingers = (hdr >> 16) & 0xF;
    if (num_fingers > TOUCH_MAX_POINTS) num_fingers = (uint8_t)TOUCH_MAX_POINTS;
    if (num_fingers > max_events)       num_fingers = (uint8_t)max_events;

    for (uint8_t i = 0; i < num_fingers; i++) {
        uint32_t word0 = MMIO_READ32(SPI0_BASE + SPI_RXDATA);
        uint32_t word1 = MMIO_READ32(SPI0_BASE + SPI_RXDATA);
        events[i].x        = (uint16_t)(word0 & 0xFFF);
        events[i].y        = (uint16_t)((word0 >> 12) & 0xFFF);
        events[i].pressure = (uint8_t)((word0 >> 24) & 0xFF);
        events[i].id       = i;
        events[i].state    = (uint8_t)(word1 & 0x3);
        count++;
    }
    return (int64_t)count;
}

/* ================================================================
 * STORAGE PRIMITIVES (NVMe / ANS2)
 * ================================================================ */

/* Minimal NVMe submission queue entry (64 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  opc;          /* Opcode: 0x02=read, 0x01=write */
    uint8_t  fuse;
    uint16_t cid;
    uint32_t nsid;
    uint64_t _reserved;
    uint64_t mptr;
    uint64_t prp1;         /* Data buffer PA */
    uint64_t prp2;
    uint32_t cdw10;        /* Starting LBA low */
    uint32_t cdw11;        /* Starting LBA high */
    uint32_t cdw12;        /* Number of blocks - 1 */
    uint32_t cdw13, cdw14, cdw15;
} nvme_sqe_t;

/* Submission queue (1 entry for simplicity — production needs ring) */
static volatile nvme_sqe_t *ansq = (volatile nvme_sqe_t *)(uintptr_t)(ANS_BASE + ANS_QUEUE_BASE);
static uint16_t sqe_cid = 0;

static int64_t prim_storage_op(bool write, uint64_t lba, uint64_t count, uint64_t buf_pa)
{
    if (!storage_initialized) return HAC_ENODEV;
    if (!buf_pa) return HAC_EINVAL;

    ansq->opc   = write ? 0x01 : 0x02;
    ansq->fuse  = 0;
    ansq->cid   = ++sqe_cid;
    ansq->nsid  = 1;
    ansq->prp1  = buf_pa;
    ansq->prp2  = 0;
    ansq->cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    ansq->cdw11 = (uint32_t)(lba >> 32);
    ansq->cdw12 = (uint32_t)(count - 1);

    /* Ring doorbell */
    MMIO_WRITE32(ANS_BASE + ANS_DOORBELL, 1);

    /* Poll for completion (simplified — production uses completion queue) */
    unsigned timeout = 100000;
    while ((MMIO_READ32(ANS_BASE + ANS_INTSTAT) == 0) && --timeout)
        asm volatile("nop");

    MMIO_WRITE32(ANS_BASE + ANS_INTSTAT, 0xFFFFFFFF); /* clear interrupt */
    return (timeout > 0) ? HAC_OK : HAC_ETIMEOUT;
}

/* ================================================================
 * GPIO PRIMITIVES
 * ================================================================ */

static int64_t prim_gpio_set(uint64_t pin, uint64_t val)
{
    /* Set output direction */
    MMIO_WRITE32(GPIO_BASE + GPIO_CFG((uint32_t)pin), GPIO_CFG_OUTPUT);
    /* Set value */
    uint32_t data = MMIO_READ32(GPIO_BASE + GPIO_DATA);
    if (val) data |=  (1U << pin);
    else     data &= ~(1U << pin);
    MMIO_WRITE32(GPIO_BASE + GPIO_DATA, data);
    return HAC_OK;
}

static int64_t prim_gpio_get(uint64_t pin)
{
    MMIO_WRITE32(GPIO_BASE + GPIO_CFG((uint32_t)pin), GPIO_CFG_INPUT);
    uint32_t data = MMIO_READ32(GPIO_BASE + GPIO_DATA);
    return (int64_t)((data >> pin) & 1);
}

/* ================================================================
 * SENSOR PRIMITIVES (I2C-based)
 * ================================================================ */

/* Simple I2C read helper */
static uint8_t i2c_read_byte(uintptr_t i2c_base, uint8_t dev_addr, uint8_t reg)
{
    /* START + write address */
    MMIO_WRITE32(i2c_base + I2C_IICADD, dev_addr << 1);
    MMIO_WRITE32(i2c_base + I2C_IICDS,  reg);
    MMIO_WRITE32(i2c_base + I2C_IICSTAT, 0xF0); /* Master TX mode */

    unsigned to = 10000;
    while ((MMIO_READ32(i2c_base + I2C_IICSTAT) & I2C_STAT_BUSY) && --to)
        asm volatile("nop");

    /* Repeated START + read */
    MMIO_WRITE32(i2c_base + I2C_IICADD,  (dev_addr << 1) | 1);
    MMIO_WRITE32(i2c_base + I2C_IICSTAT,  0xB0); /* Master RX mode */

    to = 10000;
    while ((MMIO_READ32(i2c_base + I2C_IICSTAT) & I2C_STAT_BUSY) && --to)
        asm volatile("nop");

    /* STOP */
    MMIO_WRITE32(i2c_base + I2C_IICSTAT, 0x90);
    return (uint8_t)MMIO_READ32(i2c_base + I2C_IICDS);
}

static int64_t prim_sensor_read(uint64_t sensor_id, uint64_t buf_pa)
{
    volatile sensor_data_t *out = (volatile sensor_data_t *)(uintptr_t)buf_pa;
    out->id = (sensor_id_t)sensor_id;

    switch (sensor_id) {
    case SENSOR_ACCEL:
    case SENSOR_GYRO: {
        /* ICM-20609 on I2C2: registers 0x3B-0x48 */
        uint8_t xh = i2c_read_byte(I2C2_BASE, IMU_I2C_ADDR, 0x3B);
        uint8_t xl = i2c_read_byte(I2C2_BASE, IMU_I2C_ADDR, 0x3C);
        uint8_t yh = i2c_read_byte(I2C2_BASE, IMU_I2C_ADDR, 0x3D);
        uint8_t yl = i2c_read_byte(I2C2_BASE, IMU_I2C_ADDR, 0x3E);
        uint8_t zh = i2c_read_byte(I2C2_BASE, IMU_I2C_ADDR, 0x3F);
        uint8_t zl = i2c_read_byte(I2C2_BASE, IMU_I2C_ADDR, 0x40);
        out->x = (int32_t)(int16_t)((xh << 8) | xl);
        out->y = (int32_t)(int16_t)((yh << 8) | yl);
        out->z = (int32_t)(int16_t)((zh << 8) | zl);
        break;
    }
    case SENSOR_BARO: {
        /* BMP280 on I2C2 */
        uint8_t msb  = i2c_read_byte(I2C2_BASE, BARO_I2C_ADDR, 0xF7);
        uint8_t lsb  = i2c_read_byte(I2C2_BASE, BARO_I2C_ADDR, 0xF8);
        uint8_t xlsb = i2c_read_byte(I2C2_BASE, BARO_I2C_ADDR, 0xF9);
        out->scalar = (int32_t)(((uint32_t)msb << 12) |
                                ((uint32_t)lsb << 4)  |
                                ((uint32_t)xlsb >> 4));
        break;
    }
    default:
        return HAC_EINVAL;
    }
    return HAC_OK;
}

/* ================================================================
 * HVC DISPATCHER
 * ================================================================ */

int64_t hvc_dispatch(uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3,
                     uint64_t x4, uint64_t x5, uint64_t x6 __attribute__((unused)),
                     uint64_t x7 __attribute__((unused)), uint64_t hvc_nr)
{
    hac_hvc_t id = (hac_hvc_t)(x0);
    (void)hvc_nr; /* HVC immediate; we use x0 as the call identifier */

    /* Audit every call */
    caps_audit(id, x1);

    /* Capability check: only HAC_HVC_VERSION, VERSION queries are always allowed */
    if (id != HAC_HVC_VERSION && !caps_check_current(id))
        return HAC_EPERM;

    switch (id) {
    /* ---- System ---- */
    case HAC_HVC_VERSION:
        return 0x00010000;   /* v0.1.0 */
    case HAC_HVC_GET_CAPS:
        return (int64_t)caps_get_current();
    case HAC_HVC_GRANT_CAP:
        caps_grant((uint32_t)x1, (uint32_t)x2); return HAC_OK;
    case HAC_HVC_REVOKE_CAP:
        caps_revoke((uint32_t)x1, (uint32_t)x2); return HAC_OK;

    /* ---- Display ---- */
    case HAC_HVC_DISPLAY_BLIT:
        return prim_display_blit(x1, x2, x3, x4, x5);
    case HAC_HVC_DISPLAY_INFO:
        return prim_display_info();
    case HAC_HVC_DISPLAY_BLANK:
        /* Write to backlight GPIO */
        prim_gpio_set(30, x1 ? 0 : 1);  /* GPIO 30 = backlight enable */
        return HAC_OK;
    case HAC_HVC_DISPLAY_BRIGHT:
        /* Write PWM duty cycle to backlight controller (simplified) */
        MMIO_WRITE32(GPIO_BASE + GPIO_CFG(31), GPIO_CFG_ALT(2)); /* PWM mode */
        return HAC_OK;

    /* ---- Touch ---- */
    case HAC_HVC_TOUCH_POLL:
        return prim_touch_poll(x1, x2);

    /* ---- Storage ---- */
    case HAC_HVC_STORAGE_READ:
        return prim_storage_op(false, x1, x2, x3);
    case HAC_HVC_STORAGE_WRITE:
        return prim_storage_op(true,  x1, x2, x3);
    case HAC_HVC_STORAGE_FLUSH:
        MMIO_WRITE32(ANS_BASE + ANS_DOORBELL, 0x10000);
        return HAC_OK;

    /* ---- GPIO ---- */
    case HAC_HVC_GPIO_SET:
        return prim_gpio_set(x1, x2);
    case HAC_HVC_GPIO_GET:
        return prim_gpio_get(x1);

    /* ---- Sensors ---- */
    case HAC_HVC_SENSOR_READ:
        return prim_sensor_read(x1, x2);

    /* ---- Camera ---- */
    case HAC_HVC_CAMERA_CAPTURE: {
        /* Write output buffer address to ISP, trigger capture */
        MMIO_WRITE32(ISP0_BASE + ISP_CAPTURE_BUF,     (uint32_t)(x1 & 0xFFFFFFFF));
        MMIO_WRITE32(ISP0_BASE + ISP_CAPTURE_BUF + 4, (uint32_t)(x1 >> 32));
        MMIO_WRITE32(ISP0_BASE + ISP_CTRL, 1);  /* Start */
        unsigned to = 500000;
        while ((MMIO_READ32(ISP0_BASE + ISP_STATUS) & 1) && --to)
            asm volatile("nop");
        return (to > 0) ? (int64_t)x2 : HAC_ETIMEOUT;
    }

    /* ---- Power ---- */
    case HAC_HVC_POWER_SLEEP:
        asm volatile("wfi");
        return HAC_OK;

    /* ---- Inference ---- */
    case HAC_HVC_INFER_STATUS:
        return 1;   /* Always ready in stub */
    case HAC_HVC_INFER_PROMPT:
        /* Dispatch to llama.cpp inference engine */
        /* inference_run((char*)(uintptr_t)x1, (size_t)x2, (char*)(uintptr_t)x3, (size_t)x4); */
        return HAC_OK;

    default:
        return HAC_EINVAL;
    }
}
