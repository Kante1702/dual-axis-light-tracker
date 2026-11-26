/*
 * comm_host_hw.h
 * Nahrada USART2 pre test comm.c na PC.
 */
#ifndef COMM_HOST_HW_H
#define COMM_HOST_HW_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

extern bool mock_rx_ready;
extern uint8_t mock_rx_byte;
extern bool mock_txe_irq;
extern char mock_tx_out[];
extern size_t mock_tx_len;

static inline void hw_rx_irq_enable(void) { }
static inline void hw_clear_errors(void) { }
static inline bool hw_rx_ready(void) { return mock_rx_ready; }
static inline uint8_t hw_rx_read(void) { mock_rx_ready = false; return mock_rx_byte; }
static inline bool hw_tx_ready(void)  { return true; }
static inline void hw_tx_write(uint8_t b)  { mock_tx_out[mock_tx_len++] = (char)b; }
static inline bool hw_tx_irq_enabled(void) { return mock_txe_irq; }
static inline void hw_tx_irq(bool on)  { mock_txe_irq = on; }

#endif /* COMM_HOST_HW_H */