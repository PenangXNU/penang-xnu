/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Architectural timer admission and the Qualcomm GENI debug-UART candidate.
 *
 * Timer: XNU programs the EL1 virtual timer (CNTV_*, see
 * osfmk/arm64/machine_routines.c) and masks the physical one, so the
 * interrupt this platform must deliver is the tree's virtual-timer PPI.
 *
 * UART: register interface as documented by the public Linux driver
 * (include/linux/soc/qcom/geni-se.h, drivers/tty/serial/qcom_geni_serial.c);
 * no code was copied. Transmit only, polled, adopting whatever line settings
 * the boot chain left. Touching an unclocked serial engine can raise an
 * external abort, so the caller needs a working synchronous-abort recovery
 * path before the first probe; this module cannot make that safe by itself.
 */
#include "ppp_platform.h"

#define GENI_STATUS         0x040u
#define GENI_SER_M_CLK_CFG  0x048u
#define GENI_FW_REVISION_RO 0x068u
#define GENI_UART_TX_LEN    0x270u
#define GENI_M_CMD0         0x600u
#define GENI_M_IRQ_STATUS   0x610u
#define GENI_M_IRQ_CLEAR    0x618u
#define GENI_TX_FIFO        0x700u
#define GENI_TX_WATERMARK   0x80cu

#define GENI_PROTOCOL(fw)   (((fw) >> 8) & 0xffu)
#define GENI_PROTOCOL_UART  2u
#define GENI_SER_CLK_EN     (1u << 0)
#define GENI_M_CMD_ACTIVE   (1u << 0)
#define GENI_M_CMD_DONE     (1u << 0)
#define GENI_M_TX_WATERMARK (1u << 30)
#define GENI_UART_START_TX  (1u << 27)

#define DT_IRQ_LEVEL_HIGH   0x4u
#define DT_IRQ_LEVEL_LOW    0x8u

/* Policy in ppp_platform.h. Returns 0 and fills the record, or -1. */
static int
timer_trigger(uint32_t flags, uint32_t *polarity_low, uint32_t *defaulted)
{
    uint32_t trigger = flags & PPP_DT_IRQ_TRIGGER;
    if (flags & PPP_DT_IRQ_RESERVED) {
        return -1;
    }
    *polarity_low = trigger == DT_IRQ_LEVEL_LOW;
    *defaulted = trigger == 0;
    return (trigger == 0 || trigger == DT_IRQ_LEVEL_HIGH || trigger == DT_IRQ_LEVEL_LOW) ? 0 : -1;
}

int
ppp_timer_admit(const struct ppp_resources *res, const struct ppp_io *io, struct ppp_timer *out)
{
    uint64_t hz;
    if (res == NULL || io == NULL || io->sysreg_read == NULL || out == NULL ||
        res->abi_version != PPP_ABI_VERSION) {
        return PPP_TIMER_E_ARGS;
    }
    out->frequency_hz = 0;
    out->virtual_intid = 0;
    out->edge = 0;
    out->polarity_low = 0;
    out->trigger_defaulted = 0;
    out->cpu_mask = 0;
    if (res->timer_count <= PPP_TIMER_VIRTUAL || res->timer[PPP_TIMER_VIRTUAL].intid < 16 ||
        res->timer[PPP_TIMER_VIRTUAL].intid > 31) {
        return PPP_TIMER_E_NO_VIRTUAL;
    }
    for (uint32_t i = 0; i < res->timer_count && i < PPP_TIMER_MAX; ++i) {
        uint32_t low, defaulted;
        if (timer_trigger(res->timer[i].dt_flags, &low, &defaulted) != 0) {
            return PPP_TIMER_E_TRIGGER;
        }
    }
    /* CNTFRQ_EL0 is writable only at the highest exception level. If
     * firmware left it zero there is no trustworthy timebase to adopt. */
    hz = io->sysreg_read(io->ctx, PPP_SR_CNTFRQ_EL0) & 0xffffffffu;
    if (hz == 0) {
        return PPP_TIMER_E_FREQUENCY_ZERO;
    }
    if (res->timer_dt_hz != 0 && res->timer_dt_hz != hz) {
        return PPP_TIMER_E_FREQUENCY_MISMATCH;
    }
    (void)timer_trigger(res->timer[PPP_TIMER_VIRTUAL].dt_flags, &out->polarity_low,
        &out->trigger_defaulted);
    out->frequency_hz = hz;
    out->virtual_intid = res->timer[PPP_TIMER_VIRTUAL].intid;
    out->cpu_mask = (res->timer[PPP_TIMER_VIRTUAL].dt_flags & PPP_DT_IRQ_CPU_MASK) >> 8;
    return PPP_TIMER_OK;
}

static int
uart_wait(struct ppp_uart *u, uint32_t reg, uint32_t bits, int set)
{
    for (uint32_t i = 0; i < u->poll_limit; ++i) {
        if (((u->io.read32(u->io.ctx, u->base + reg) & bits) != 0) == (set != 0)) {
            return 0;
        }
    }
    return -1;
}

int
ppp_uart_probe(struct ppp_uart *uart, const struct ppp_resources *res,
    const struct ppp_io *io, uint32_t poll_limit)
{
    if (uart == NULL || res == NULL || io == NULL || io->read32 == NULL || io->write32 == NULL ||
        res->abi_version != PPP_ABI_VERSION) {
        return PPP_UART_E_ARGS;
    }
    uart->ready = 0;
    uart->sent = uart->dropped = 0;
    if (res->console_candidates != 1 || res->console_pa == 0) {
        return PPP_UART_E_STATE; /* none, or more than one: do not choose */
    }
    uart->io = *io;
    uart->base = res->console_pa;
    uart->poll_limit = poll_limit ? poll_limit : PPP_GIC_POLL_DEFAULT;
    if (GENI_PROTOCOL(io->read32(io->ctx, uart->base + GENI_FW_REVISION_RO)) !=
        GENI_PROTOCOL_UART) {
        return PPP_UART_E_NOT_UART;
    }
    if (!(io->read32(io->ctx, uart->base + GENI_SER_M_CLK_CFG) & GENI_SER_CLK_EN)) {
        return PPP_UART_E_UNCLOCKED;
    }
    uart->ready = 1;
    return PPP_UART_OK;
}

int
ppp_uart_putc(struct ppp_uart *uart, uint8_t byte)
{
    if (uart == NULL) {
        return PPP_UART_E_ARGS;
    }
    if (!uart->ready) {
        return PPP_UART_E_STATE;
    }
    if (uart_wait(uart, GENI_STATUS, GENI_M_CMD_ACTIVE, 0)) {
        goto timeout;
    }
    uart->io.write32(uart->io.ctx, uart->base + GENI_TX_WATERMARK, 2);
    uart->io.write32(uart->io.ctx, uart->base + GENI_UART_TX_LEN, 1);
    uart->io.write32(uart->io.ctx, uart->base + GENI_M_CMD0, GENI_UART_START_TX);
    if (uart_wait(uart, GENI_M_IRQ_STATUS, GENI_M_TX_WATERMARK, 1)) {
        goto timeout;
    }
    uart->io.write32(uart->io.ctx, uart->base + GENI_TX_FIFO, byte);
    uart->io.write32(uart->io.ctx, uart->base + GENI_M_IRQ_CLEAR, GENI_M_TX_WATERMARK);
    if (uart_wait(uart, GENI_M_IRQ_STATUS, GENI_M_CMD_DONE, 1)) {
        goto timeout;
    }
    uart->io.write32(uart->io.ctx, uart->base + GENI_M_IRQ_CLEAR, GENI_M_CMD_DONE);
    ++uart->sent;
    return PPP_UART_OK;
timeout:
    ++uart->dropped;
    uart->ready = 0;
    return PPP_UART_E_TIMEOUT;
}
