/*
 * AgentOS HAC — Apple A11 Bionic (t8010/t8015) MMIO Register Map
 *
 * Physical base addresses derived from:
 *   - Apple Device Tree extracted from iOS 16 IPSW (t8010ap / t8015ap)
 *   - PongoOS source (platform/t8010/include/)
 *   - hack-different/apple-knowledge hardware database
 *   - theapplewiki.com hardware documentation
 *   - XNU open-source IOKit driver headers
 *
 * All addresses are physical (no MMU on HAC side).
 * MMIO regions are in the range 0x200000000 – 0x7FFFFFFFFF on A11.
 *
 * WARNING: Many offsets are reverse-engineered and should be validated
 * against a live device using JTAG/serial before relying on them.
 */
#pragma once
#include <stdint.h>

/* ================================================================
 * Apple Interrupt Controller (AIC) — t8010
 * ================================================================ */
#define AIC_BASE                0x200000000ULL

/* AIC register offsets */
#define AIC_VERSION             0x0000
#define AIC_CONFIG              0x0004
#define AIC_GLOBAL_CFG          0x0010  /* IRQ enable/disable global */
#define AIC_EVENT               0x2004  /* Read to ack current event */
#define AIC_IRQ_ENABLE_SET(n)   (0x4100 + (n) * 4)
#define AIC_IRQ_ENABLE_CLR(n)   (0x4180 + (n) * 4)
#define AIC_IRQ_SOFT_SET(n)     (0x4200 + (n) * 4)
#define AIC_IRQ_SOFT_CLR(n)     (0x4280 + (n) * 4)
#define AIC_IPID(irq)           (0x4300 + (irq) * 4)  /* Target CPU routing */
#define AIC_IRQ_COUNT           256

/* AIC event fields */
#define AIC_EVENT_TYPE_MASK     0xFF000000
#define AIC_EVENT_TYPE_IRQ      0x01000000
#define AIC_EVENT_IRQ_MASK      0x000003FF

/* ================================================================
 * UART (Apple UART / Samsung s3c-compatible)
 * ================================================================ */
#define UART0_BASE              0x20A000000ULL  /* Debug UART on Lightning */
#define UART1_BASE              0x20A004000ULL
#define UART2_BASE              0x20A008000ULL

/* UART register offsets */
#define UART_ULCON              0x0000  /* Line control */
#define UART_UCON               0x0004  /* Control */
#define UART_UFCON              0x0008  /* FIFO control */
#define UART_UMCON              0x000C  /* Modem control */
#define UART_UTRSTAT            0x0010  /* TX/RX status */
#define UART_UERSTAT            0x0014  /* Error status */
#define UART_UFSTAT             0x0018  /* FIFO status */
#define UART_UMSTAT             0x001C  /* Modem status */
#define UART_UTXH               0x0020  /* TX holding */
#define UART_URXH               0x0024  /* RX holding */
#define UART_UBRDIV             0x0028  /* Baud rate divisor */
#define UART_UFRACVAL           0x002C  /* Fractional baud rate */

/* UART status bits */
#define UART_UTRSTAT_TX_EMPTY   BIT(2)
#define UART_UTRSTAT_RX_READY   BIT(0)

/* ================================================================
 * GPIO (Apple GPIO controller)
 * ================================================================ */
#define GPIO_BASE               0x20F100000ULL

#define GPIO_CFG(pin)           ((pin) * 4)       /* Pin configuration */
#define GPIO_DATA               0x800              /* Data register */
#define GPIO_DIR                0x804              /* Direction (1=output) */

#define GPIO_CFG_INPUT          0x0
#define GPIO_CFG_OUTPUT         0x1
#define GPIO_CFG_ALT(n)         (2 + (n))          /* Alternate function n */

/* ================================================================
 * I2C Controllers
 * ================================================================ */
#define I2C0_BASE               0x20A010000ULL  /* PMU bus */
#define I2C1_BASE               0x20A014000ULL  /* Touchscreen */
#define I2C2_BASE               0x20A018000ULL  /* Camera / accelerometer */
#define I2C3_BASE               0x20A01C000ULL
#define I2C4_BASE               0x20A020000ULL  /* SEP / digitizer (A11) */

/* I2C register offsets */
#define I2C_IICCON              0x00   /* Control */
#define I2C_IICSTAT             0x04   /* Status */
#define I2C_IICADD              0x08   /* Address */
#define I2C_IICDS               0x0C   /* Data shift */
#define I2C_IICLC               0x10   /* Line control */

#define I2C_STAT_BUSY           BIT(5)
#define I2C_STAT_ACK            BIT(0)

/* ================================================================
 * SPI Controllers
 * ================================================================ */
#define SPI0_BASE               0x20A080000ULL  /* Touchscreen SPI (Melfas) */
#define SPI1_BASE               0x20A084000ULL  /* NFC */
#define SPI2_BASE               0x20A088000ULL

/* SPI register offsets (Apple SPI controller) */
#define SPI_CONFIG              0x00
#define SPI_FRAME_SIZE          0x04
#define SPI_CLKDIVIDER          0x08
#define SPI_RXDATA              0x20
#define SPI_TXDATA              0x24
#define SPI_STATUS              0x28
#define SPI_CTRL                0x2C

#define SPI_STATUS_TXEMPTY      BIT(2)
#define SPI_STATUS_RXNE         BIT(0)

/* ================================================================
 * PMU / PMIC — Dialog DA9090 on I2C0
 * Register map is I2C-accessed, not direct MMIO
 * ================================================================ */
#define PMU_I2C_ADDR            0x68   /* 7-bit address on I2C0 */

/* ================================================================
 * USB OTG (Synopsys DWC2 compatible)
 * ================================================================ */
#define USB_BASE                0x20C000000ULL

#define USB_GAHBCFG             0x008
#define USB_GUSBCFG             0x00C
#define USB_GRSTCTL             0x010
#define USB_GINTSTS             0x014
#define USB_GINTMSK             0x018
#define USB_GRXFSIZ             0x024
#define USB_GNPTXFSIZ           0x028
#define USB_GHWCFG1             0x044
#define USB_GHWCFG2             0x048
#define USB_GHWCFG3             0x04C
#define USB_GHWCFG4             0x050

/* USB Device registers */
#define USB_DCFG                0x800
#define USB_DCTL                0x804
#define USB_DSTS                0x808

/* ================================================================
 * PCIe Host Controller (for WiFi/BT: BCM4377 / Apple W3)
 * ================================================================ */
#define PCIE_BASE               0x300000000ULL
#define PCIE_CFG_SPACE          0x310000000ULL  /* Config space window */

/* ================================================================
 * NAND / NVM Express Storage (Apple ANS2)
 * ================================================================ */
#define ANS_BASE                0x24E000000ULL  /* ANS2 coprocessor MMIO */
#define ANS_DOORBELL            0x000          /* Kick NVMe queue */
#define ANS_INTSTAT             0x004
#define ANS_INTMASK             0x008
#define ANS_QUEUE_BASE          0x100          /* Submission queue base */

/* Standard NVMe register offsets (same as spec) */
#define NVME_CAP                0x0000
#define NVME_VS                 0x0008
#define NVME_CC                 0x0014
#define NVME_CSTS               0x001C
#define NVME_AQA                0x0024
#define NVME_ASQ                0x0028
#define NVME_ACQ                0x0030

/* ================================================================
 * Display — MIPI DSI controller + DCP (Display Coprocessor)
 * ================================================================ */
#define DISP_BASE               0x230000000ULL  /* Display subsystem */
#define DCP_BASE                0x228000000ULL  /* Display Coprocessor */

/* Simplified MIPI DSI registers */
#define DSI_CTRL                0x000
#define DSI_STATUS              0x004
#define DSI_CMD_MODE_CFG        0x038
#define DSI_GEN_HDR             0x06C
#define DSI_GEN_PLD_DATA        0x070
#define DSI_CMD_PKT_STATUS      0x074
#define DSI_EDPI_CMD_SIZE       0x07C

/* Framebuffer (set up by Apple DCP, address in DeviceTree) */
#define FB_DEFAULT_PA           0x83C000000ULL  /* Default FB base */
#define FB_WIDTH                1125
#define FB_HEIGHT               2436
#define FB_STRIDE               (FB_WIDTH * 4)   /* ARGB8888 */
#define FB_SIZE                 (FB_STRIDE * FB_HEIGHT)

/* ================================================================
 * Touchscreen — Melfas / STMicro via SPI0
 * ================================================================ */
#define TOUCH_IRQ_GPIO          52   /* GPIO pin for touch interrupt */
#define TOUCH_RESET_GPIO        53
#define TOUCH_MAX_X             1125
#define TOUCH_MAX_Y             2436
#define TOUCH_MAX_POINTS        5

/* ================================================================
 * Camera ISP (ISP0 / ISP1)
 * ================================================================ */
#define ISP0_BASE               0x22A000000ULL
#define ISP1_BASE               0x22B000000ULL

#define ISP_CTRL                0x000
#define ISP_STATUS              0x004
#define ISP_FW_ADDR             0x020   /* ISP firmware load address */
#define ISP_CAPTURE_BUF         0x100   /* Output frame buffer PA */

/* Capture buffer: 12 MP at packed YUV420 = ~18 MB */
#define ISP_CAPTURE_PA          0x848000000ULL
#define ISP_CAPTURE_SIZE        0x01500000ULL

/* ================================================================
 * Audio — PDM microphone + I2S DAC
 * ================================================================ */
#define I2S0_BASE               0x20C200000ULL  /* Headphone / speaker */
#define I2S1_BASE               0x20C204000ULL  /* Earpiece */
#define PDM_BASE                0x20C210000ULL  /* PDM microphone */

/* ================================================================
 * Cellular modem — Qualcomm MDM9655 (iPhone 8 Intel variant differs)
 * Accessed via PCIe endpoint; AT commands over /dev/ttyUSB0-equivalent
 * ================================================================ */
#define MODEM_PCIE_DEV          0x01   /* PCIe device number */
#define MODEM_PCIE_FN           0x00

/* ================================================================
 * Secure Enclave Processor mailbox (treated as black box)
 * ================================================================ */
#define SEP_MBOX_BASE           0x204000200ULL  /* SEP mailbox registers */
#define SEP_MBOX_TX             0x000           /* Write SEP command */
#define SEP_MBOX_RX             0x008           /* Read SEP response */
#define SEP_MBOX_STATUS         0x010
#define SEP_MBOX_TX_EMPTY       BIT(0)
#define SEP_MBOX_RX_VALID       BIT(1)

/* ================================================================
 * Sensors
 * ================================================================ */
/* Accelerometer / Gyroscope: InvenSense ICM-20609 on I2C2 */
#define IMU_I2C_ADDR            0x68

/* Barometer: Bosch BMP280 on I2C2 */
#define BARO_I2C_ADDR           0x77

/* Proximity + ambient light: STMicro VL53L1X on I2C1 */
#define PROX_I2C_ADDR           0x29

/* ================================================================
 * Clock tree — clock gate addresses (abbreviated)
 * ================================================================ */
#define CLK_BASE                0x20E000000ULL
#define CLK_UART0_GATE          (CLK_BASE + 0x060)
#define CLK_SPI0_GATE           (CLK_BASE + 0x080)
#define CLK_I2C_GATE            (CLK_BASE + 0x090)
#define CLK_USB_GATE            (CLK_BASE + 0x0A0)
