/*
 * AgentOS HAC — Early UART Debug Console
 * Apple A11 UART0 (Samsung s3c-compatible), 115200 8N1
 */
#include "hac.h"
#include "../mmio/a11_mmio.h"

/* A11 UART clock is typically 24 MHz from CLKOUT */
#define UART_CLOCK_HZ   24000000ULL
#define UART_BAUD       115200

void uart_early_init(void)
{
    /* Enable UART0 clock gate */
    uint32_t clk = MMIO_READ32(CLK_UART0_GATE);
    MMIO_WRITE32(CLK_UART0_GATE, clk | 1);

    /* 8N1 mode */
    MMIO_WRITE32(UART0_BASE + UART_ULCON, 0x03);

    /* Enable TX/RX, no interrupt */
    MMIO_WRITE32(UART0_BASE + UART_UCON, 0x05);

    /* Enable TX FIFO */
    MMIO_WRITE32(UART0_BASE + UART_UFCON, 0x01);

    /* Baud rate divisor: DIV = (CLKHZ / (BAUD * 16)) - 1 */
    uint32_t div = (uint32_t)(UART_CLOCK_HZ / (UART_BAUD * 16)) - 1;
    uint32_t frac = (uint32_t)(((UART_CLOCK_HZ / (UART_BAUD * 16.0f)) - div) * 16.0f);
    MMIO_WRITE32(UART0_BASE + UART_UBRDIV,   div);
    MMIO_WRITE32(UART0_BASE + UART_UFRACVAL, frac & 0xF);
}

void hac_uart_putc(char c)
{
    /* Wait for TX FIFO not full */
    while (!(MMIO_READ32(UART0_BASE + UART_UTRSTAT) & UART_UTRSTAT_TX_EMPTY))
        asm volatile("nop");
    MMIO_WRITE32(UART0_BASE + UART_UTXH, (uint8_t)c);
}

void hac_uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') hac_uart_putc('\r');
        hac_uart_putc(*s++);
    }
}

void hac_uart_puthex(uint64_t val)
{
    static const char hex[] = "0123456789ABCDEF";
    hac_uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        hac_uart_putc(hex[(val >> i) & 0xF]);
}
