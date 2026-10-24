/* SPDX-License-Identifier: GPL-3.0-only */
/* MUSB register-level functions from cpu.a (JieLi AC791N SoC).
 * Declarations match the SDK's asm/usb.h, using SDK-compatible types. */
#ifndef FM1_USB_MUSB_H
#define FM1_USB_MUSB_H
#include <stdint.h>

/* SDK type aliases (must match cpu.a's compiled types exactly) */
typedef unsigned char  usb_dev;

/* USB port index (FM-1 uses USB0 only) */
#define MUSB_ID  ((usb_dev)0)

/* HUSB SIE control bits */
#define HUSB_SYS_NRST       0
#define HUSB_SIE_EN         1
#define HUSB_SIE_NRST       4
#define HUSB_SIE_PND        5

/* CSR0 host-mode bits */
#define CSR0H_RxPktRdy      0x0001
#define CSR0H_TxPktRdy      0x0002
#define CSR0H_RxStall       0x0004
#define CSR0H_SetupPkt      0x0008
#define CSR0H_Error         0x0010
#define CSR0H_ReqPkt        0x0020
#define CSR0H_StatusPkt     0x0040
#define CSR0H_NAKTimeout    0x0080

/* TXCSR host-mode bits */
#define TXCSRH_TxPktRdy     0x01
#define TXCSRH_RxStall      0x04
#define TXCSRH_Error        0x20

/* RXCSR host-mode bits */
#define RXCSRH_RxPktRdy     0x01
#define RXCSRH_RxStall      0x04
#define RXCSRH_DataError    0x08
#define RXCSRH_ReqPkt       0x10

/* USB descriptor types */
#define USB_DT_DEVICE       1
#define USB_DT_CONFIG       2
#define USB_DT_INTERFACE    4
#define USB_DT_ENDPOINT     5

/* USB class codes */
#define USB_CLASS_MSC       0x08
#define USB_MSC_SUBCLASS_SCSI 0x06
#define USB_MSC_PROTO_BBB   0x50

/* Endpoint direction bit */
#define USB_EP_DIR_IN       0x80

/* Stub for SDK dependency: P33_SYSTEM_RESET (called on fatal USB error) */
void P33_SYSTEM_RESET(void);

/* --- cpu.a low-level functions (signatures match SDK asm/usb.h) --- */

extern uint32_t musb_read_usb(const usb_dev id, uint32_t addr);
extern void musb_write_usb(const usb_dev id, uint32_t addr, uint32_t data);
extern uint16_t musb_read_sofframe(const usb_dev id);
extern void musb_write_index(const usb_dev id, uint32_t endpoint);

extern uint32_t usb_read_csr0(const usb_dev id);
extern void usb_write_csr0(const usb_dev id, uint32_t csr0);
extern uint32_t usb_read_count0(const usb_dev id);
extern void usb_write_ep0(const usb_dev id, const uint8_t *ptr, uint32_t len);
extern void usb_read_ep0(const usb_dev id, uint8_t *ptr, uint32_t len);
extern void usb_ep0_ClrRxPktRdy(const usb_dev id);
extern void usb_ep0_TxPktEnd(const usb_dev id);
extern void usb_ep0_RxPktEnd(const usb_dev id);
extern void usb_ep0_Set_Stall(const usb_dev id);

extern void usb_write_faddr(const usb_dev id, uint32_t addr);
extern void usb_write_power(const usb_dev id, uint32_t value);
extern uint32_t usb_read_power(const usb_dev id);
extern uint32_t usb_read_devctl(const usb_dev id);
extern void usb_write_devctl(const usb_dev id, uint32_t value);

extern void usb_write_txcsr(const usb_dev id, const uint32_t ep, uint32_t txcsr);
extern uint32_t usb_read_txcsr(const usb_dev id, const uint32_t ep);
extern void usb_write_rxcsr(const usb_dev id, const uint32_t ep, uint32_t rxcsr);
extern uint32_t usb_read_rxcsr(const usb_dev id, const uint32_t ep);
extern uint32_t usb_read_rxcount(const usb_dev id, uint32_t ep);
extern void usb_write_txmaxp(const usb_dev id, const uint32_t ep, uint32_t value);
extern void usb_write_rxmaxp(const usb_dev id, const uint32_t ep, uint32_t value);
extern void usb_write_txtype(const usb_dev id, const uint32_t ep, uint32_t value);
extern void usb_write_rxtype(const usb_dev id, const uint32_t ep, uint32_t value);
extern void usb_write_rxinterval(const usb_dev id, const uint32_t ep, uint32_t value);

extern int usb_h_ep_write(const usb_dev id, uint8_t host_ep, uint16_t txmaxp,
                          uint8_t target_ep, const uint8_t *ptr, uint32_t len,
                          uint32_t xfer);
extern int usb_h_ep_read(const usb_dev id, uint8_t host_ep, uint16_t rxmaxp,
                         uint8_t target_ep, uint8_t *ptr, uint32_t len,
                         uint32_t xfer);
extern uint32_t usb_h_ep_config(const usb_dev id, uint32_t ep, uint32_t type,
                                uint32_t ie, uint32_t interval, uint8_t *ptr,
                                uint32_t dma_size);

extern void usb_h_sie_init(const usb_dev id);
extern void usb_h_sie_close(const usb_dev id);
extern int usb_h_chirp_and_reset(const usb_dev id, uint32_t reset_delay,
                                 uint32_t timeout);
extern void usb_h_sie_reset(const usb_dev id);
extern void usb_h_force_reset(const usb_dev id);
extern uint32_t usb_h_dev_status(const usb_dev id);

extern void usb_read_intr(const usb_dev id, uint32_t *intr_usb,
                          uint32_t *intr_tx, uint32_t *intr_rx);
extern void usb_write_intr_usbe(const usb_dev id, uint32_t intr_usbe);
extern void usb_set_intr_txe(const usb_dev id, const uint32_t ep);
extern void usb_clr_intr_txe(const usb_dev id, const uint32_t ep);
extern void usb_set_intr_rxe(const usb_dev id, const uint32_t ep);
extern void usb_clr_intr_rxe(const usb_dev id, const uint32_t ep);

extern void usb_io_reset(const usb_dev id);
extern void usb_recover_io_status(const usb_dev id);
extern void usb_mdelay(unsigned int ms);
extern void usb_set_low_speed(const usb_dev id, uint8_t flag);
extern void usb_var_init(const usb_dev id, void *ptr);
extern void usb_var_release(const usb_dev id);
extern void usb_enable_ep(const usb_dev id, uint32_t eps);
extern void usb_disable_ep(const usb_dev id, uint32_t eps);
extern void usb_sie_close(const usb_dev id);
extern void usb_write_ep_cnt(const usb_dev id, uint32_t ep, uint32_t len);

#endif /* FM1_USB_MUSB_H */
