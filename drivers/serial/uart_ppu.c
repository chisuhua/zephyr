/* ppu.c - PPUD serial driver */

#define DT_DRV_COMPAT ppu_coreuart

/*
 * Copyright (c) 2010, 2012-2015 Wind River Systems, Inc.
 * Copyright (c) 2020 Intel Corp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief PPU Serial Driver
 *
 * This is the driver for the Intel PPU UART Chip used on the PC 386.
 * It uses the SCCs in asynchronous mode only.
 *
 * Before individual UART port can be used, uart_ppu_port_init() has to be
 * called to setup the port.
 *
 * - the following macro for the number of bytes between register addresses:
 *
 *  UART_REG_ADDR_INTERVAL
 */

#include <errno.h>
#include <kernel.h>
#include <arch/cpu.h>
#include <zephyr/types.h>
#include <soc.h>

#include <init.h>
#include <toolchain.h>
#include <linker/sections.h>
#include <drivers/uart.h>
#include <sys/sys_io.h>

#include "uart_ppu.h"

/*
 * If PCP is set for any of the ports, enable support.
 * Ditto for DLF and PCI(e).
 */

#if DT_INST_NODE_HAS_PROP(0, pcp) || \
	DT_INST_NODE_HAS_PROP(1, pcp) || \
	DT_INST_NODE_HAS_PROP(2, pcp) || \
	DT_INST_NODE_HAS_PROP(3, pcp)
#define UART_PPU_PCP_ENABLED
#endif

#if DT_INST_NODE_HAS_PROP(0, dlf) || \
	DT_INST_NODE_HAS_PROP(1, dlf) || \
	DT_INST_NODE_HAS_PROP(2, dlf) || \
	DT_INST_NODE_HAS_PROP(3, dlf)
#define UART_PPU_DLF_ENABLED
#endif

#if DT_INST_PROP(0, pcie) || \
	DT_INST_PROP(1, pcie) || \
	DT_INST_PROP(2, pcie) || \
	DT_INST_PROP(3, pcie)
BUILD_ASSERT(IS_ENABLED(CONFIG_PCIE), "PPU(s) in DT need CONFIG_PCIE");
#define UART_PPU_PCIE_ENABLED
#include <drivers/pcie/pcie.h>
#endif

/* register definitions */

#define REG_THR 0x00  /* Transmitter holding reg.       */
#define REG_RDR 0x00  /* Receiver data reg.             */
#define REG_BRDL 0x00 /* Baud rate divisor (LSB)        */
#define REG_BRDH 0x01 /* Baud rate divisor (MSB)        */
#define REG_IER 0x01  /* Interrupt enable reg.          */
#define REG_IIR 0x02  /* Interrupt ID reg.              */
#define REG_FCR 0x02  /* FIFO control reg.              */
#define REG_LCR 0x03  /* Line control reg.              */
#define REG_MDC 0x04  /* Modem control reg.             */
#define REG_LSR 0x05  /* Line status reg.               */
#define REG_MSR 0x06  /* Modem status reg.              */
#define REG_DLF 0xC0  /* Divisor Latch Fraction         */
#define REG_PCP 0x200 /* PRV_CLOCK_PARAMS (Apollo Lake) */

/* equates for interrupt enable register */

#define IER_RXRDY 0x01 /* receiver data ready */
#define IER_TBE 0x02   /* transmit bit enable */
#define IER_LSR 0x04   /* line status interrupts */
#define IER_MSI 0x08   /* modem status interrupts */

/* equates for interrupt identification register */

#define IIR_MSTAT 0x00 /* modem status interrupt  */
#define IIR_NIP   0x01 /* no interrupt pending    */
#define IIR_THRE  0x02 /* transmit holding register empty interrupt */
#define IIR_RBRF  0x04 /* receiver buffer register full interrupt */
#define IIR_LS    0x06 /* receiver line status interrupt */
#define IIR_MASK  0x07 /* interrupt id bits mask  */
#define IIR_ID    0x06 /* interrupt ID mask without NIP */

/* equates for FIFO control register */

#define FCR_FIFO 0x01    /* enable XMIT and RCVR FIFO */
#define FCR_RCVRCLR 0x02 /* clear RCVR FIFO */
#define FCR_XMITCLR 0x04 /* clear XMIT FIFO */

/* equates for Apollo Lake clock control register (PRV_CLOCK_PARAMS) */

#define PCP_UPDATE 0x80000000 /* update clock */
#define PCP_EN 0x00000001     /* enable clock output */

/*
 * Per PC16550D (Literature Number: SNLS378B):
 *
 * RXRDY, Mode 0: When in the 16450 Mode (FCR0 = 0) or in
 * the FIFO Mode (FCR0 = 1, FCR3 = 0) and there is at least 1
 * character in the RCVR FIFO or RCVR holding register, the
 * RXRDY pin (29) will be low active. Once it is activated the
 * RXRDY pin will go inactive when there are no more charac-
 * ters in the FIFO or holding register.
 *
 * RXRDY, Mode 1: In the FIFO Mode (FCR0 = 1) when the
 * FCR3 = 1 and the trigger level or the timeout has been
 * reached, the RXRDY pin will go low active. Once it is acti-
 * vated it will go inactive when there are no more characters
 * in the FIFO or holding register.
 *
 * TXRDY, Mode 0: In the 16450 Mode (FCR0 = 0) or in the
 * FIFO Mode (FCR0 = 1, FCR3 = 0) and there are no charac-
 * ters in the XMIT FIFO or XMIT holding register, the TXRDY
 * pin (24) will be low active. Once it is activated the TXRDY
 * pin will go inactive after the first character is loaded into the
 * XMIT FIFO or holding register.
 *
 * TXRDY, Mode 1: In the FIFO Mode (FCR0 = 1) when
 * FCR3 = 1 and there are no characters in the XMIT FIFO, the
 * TXRDY pin will go low active. This pin will become inactive
 * when the XMIT FIFO is completely full.
 */
#define FCR_MODE0 0x00 /* set receiver in mode 0 */
#define FCR_MODE1 0x08 /* set receiver in mode 1 */

/* RCVR FIFO interrupt levels: trigger interrupt with this bytes in FIFO */
#define FCR_FIFO_1 0x00  /* 1 byte in RCVR FIFO */
#define FCR_FIFO_4 0x40  /* 4 bytes in RCVR FIFO */
#define FCR_FIFO_8 0x80  /* 8 bytes in RCVR FIFO */
#define FCR_FIFO_14 0xC0 /* 14 bytes in RCVR FIFO */

/*
 * UART NS16750 supports 64 bytes FIFO, which can be enabled
 * via the FCR register
 */
#define FCR_FIFO_64 0x20 /* Enable 64 bytes FIFO */

/* constants for line control register */

#define LCR_CS5 0x00   /* 5 bits data size */
#define LCR_CS6 0x01   /* 6 bits data size */
#define LCR_CS7 0x02   /* 7 bits data size */
#define LCR_CS8 0x03   /* 8 bits data size */
#define LCR_2_STB 0x04 /* 2 stop bits */
#define LCR_1_STB 0x00 /* 1 stop bit */
#define LCR_PEN 0x08   /* parity enable */
#define LCR_PDIS 0x00  /* parity disable */
#define LCR_EPS 0x10   /* even parity select */
#define LCR_SP 0x20    /* stick parity select */
#define LCR_SBRK 0x40  /* break control bit */
#define LCR_DLAB 0x80  /* divisor latch access enable */

/* constants for the modem control register */

#define MCR_DTR 0x01  /* dtr output */
#define MCR_RTS 0x02  /* rts output */
#define MCR_OUT1 0x04 /* output #1 */
#define MCR_OUT2 0x08 /* output #2 */
#define MCR_LOOP 0x10 /* loop back */
#define MCR_AFCE 0x20 /* auto flow control enable */

/* constants for line status register */

#define LSR_RXRDY 0x01 /* receiver data available */
#define LSR_OE 0x02    /* overrun error */
#define LSR_PE 0x04    /* parity error */
#define LSR_FE 0x08    /* framing error */
#define LSR_BI 0x10    /* break interrupt */
#define LSR_EOB_MASK 0x1E /* Error or Break mask */
#define LSR_THRE 0x20  /* transmit holding register empty */
#define LSR_TEMT 0x40  /* transmitter empty */

/* constants for modem status register */

#define MSR_DCTS 0x01 /* cts change */
#define MSR_DDSR 0x02 /* dsr change */
#define MSR_DRI 0x04  /* ring change */
#define MSR_DDCD 0x08 /* data carrier change */
#define MSR_CTS 0x10  /* complement of cts */
#define MSR_DSR 0x20  /* complement of dsr */
#define MSR_RI 0x40   /* complement of ring signal */
#define MSR_DCD 0x80  /* complement of dcd */

/* convenience defines */

#define DEV_CFG(dev) \
	((struct uart_ppu_device_config * const) \
	 (dev)->config->config_info)
#define DEV_DATA(dev) \
	((struct uart_ppu_dev_data_t *)(dev)->driver_data)

#define THR(dev) (DEV_CFG(dev)->devconf.port + REG_THR * UART_REG_ADDR_INTERVAL)
#define RDR(dev) (DEV_CFG(dev)->devconf.port + REG_RDR * UART_REG_ADDR_INTERVAL)
#define BRDL(dev) \
	(DEV_CFG(dev)->devconf.port + REG_BRDL * UART_REG_ADDR_INTERVAL)
#define BRDH(dev) \
	(DEV_CFG(dev)->devconf.port + REG_BRDH * UART_REG_ADDR_INTERVAL)
#define IER(dev) (DEV_CFG(dev)->devconf.port + REG_IER * UART_REG_ADDR_INTERVAL)
#define IIR(dev) (DEV_CFG(dev)->devconf.port + REG_IIR * UART_REG_ADDR_INTERVAL)
#define FCR(dev) (DEV_CFG(dev)->devconf.port + REG_FCR * UART_REG_ADDR_INTERVAL)
#define LCR(dev) (DEV_CFG(dev)->devconf.port + REG_LCR * UART_REG_ADDR_INTERVAL)
#define MDC(dev) (DEV_CFG(dev)->devconf.port + REG_MDC * UART_REG_ADDR_INTERVAL)
#define LSR(dev) (DEV_CFG(dev)->devconf.port + REG_LSR * UART_REG_ADDR_INTERVAL)
#define MSR(dev) (DEV_CFG(dev)->devconf.port + REG_MSR * UART_REG_ADDR_INTERVAL)
#define DLF(dev) (DEV_CFG(dev)->devconf.port + REG_DLF)
#define PCP(dev) (DEV_CFG(dev)->devconf.port + REG_PCP)

#define IIRC(dev) (DEV_DATA(dev)->iir_cache)

#if DT_INST_NODE_HAS_PROP(0, reg_shift)
#define UART_REG_ADDR_INTERVAL (1<<DT_INST_PROP(0, reg_shift))
#endif

#ifdef UART_PPU_ACCESS_IOPORT
#define INBYTE(x) sys_in8(x)
#define INWORD(x) sys_in32(x)
#define OUTBYTE(x, d) sys_out8(d, x)
#define OUTWORD(x, d) sys_out32(d, x)
#ifndef UART_REG_ADDR_INTERVAL
#define UART_REG_ADDR_INTERVAL 1 /* address diff of adjacent regs. */
#endif /* UART_REG_ADDR_INTERVAL */
#else
#define INBYTE(x) sys_read8(x)
#define INWORD(x) sys_read32(x)
#define OUTBYTE(x, d) sys_write8(d, x)
#define OUTWORD(x, d) sys_write32(d, x)
#ifndef UART_REG_ADDR_INTERVAL
#define UART_REG_ADDR_INTERVAL 4 /* address diff of adjacent regs. */
#endif
#endif /* UART_PPU_ACCESS_IOPORT */

#ifdef CONFIG_UART_PPU_ACCESS_WORD_ONLY
#undef INBYTE
#define INBYTE(x) INWORD(x)
#undef OUTBYTE
#define OUTBYTE(x, d) OUTWORD(x, d)
#endif

/* device config */
struct uart_ppu_device_config {
	struct uart_device_config devconf;
#ifdef UART_PPU_PCP_ENABLED
	u32_t pcp;
#endif

#ifdef UART_PPU_PCIE_ENABLED
	bool pcie;
	pcie_bdf_t pcie_bdf;
	pcie_id_t pcie_id;
#endif
};

/** Device data structure */
struct uart_ppu_dev_data_t {
	struct uart_config uart_config;

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	u8_t iir_cache;	/**< cache of IIR since it clears when read */
	uart_irq_callback_user_data_t cb;  /**< Callback function pointer */
	void *cb_data;	/**< Callback function arg */
#endif

#ifdef UART_PPU_DLF_ENABLED
	u8_t dlf;		/**< DLF value */
#endif
};

static const struct uart_driver_api uart_ppu_driver_api;

static void set_baud_rate(struct device *dev, u32_t baud_rate)
{
	const struct uart_ppu_device_config * const dev_cfg = DEV_CFG(dev);
	struct uart_ppu_dev_data_t * const dev_data = DEV_DATA(dev);
	u32_t divisor; /* baud rate divisor */
	u8_t lcr_cache;

	if ((baud_rate != 0U) && (dev_cfg->devconf.sys_clk_freq != 0U)) {
		/*
		 * calculate baud rate divisor. a variant of
		 * (u32_t)(dev_cfg->sys_clk_freq / (16.0 * baud_rate) + 0.5)
		 */
		divisor = ((dev_cfg->devconf.sys_clk_freq + (baud_rate << 3))
					/ baud_rate) >> 4;

		/* set the DLAB to access the baud rate divisor registers */
		lcr_cache = INBYTE(LCR(dev));
		OUTBYTE(LCR(dev), LCR_DLAB | lcr_cache);
		OUTBYTE(BRDL(dev), (unsigned char)(divisor & 0xff));
		OUTBYTE(BRDH(dev), (unsigned char)((divisor >> 8) & 0xff));

		/* restore the DLAB to access the baud rate divisor registers */
		OUTBYTE(LCR(dev), lcr_cache);

		dev_data->uart_config.baudrate = baud_rate;
	}
}

static int uart_ppu_configure(struct device *dev,
				const struct uart_config *cfg)
{
	struct uart_ppu_dev_data_t * const dev_data = DEV_DATA(dev);
	struct uart_ppu_device_config * const dev_cfg = DEV_CFG(dev);

	unsigned int old_level;     /* old interrupt lock level */
	u8_t mdc = 0U;

	ARG_UNUSED(dev_data);
	ARG_UNUSED(dev_cfg);

#ifdef UART_PPU_PCIE_ENABLED
	if (dev_cfg->pcie) {
		if (!pcie_probe(dev_cfg->pcie_bdf, dev_cfg->pcie_id)) {
			return -EINVAL;
		}

		dev_cfg->devconf.port = pcie_get_mbar(dev_cfg->pcie_bdf, 0);
		pcie_set_cmd(dev_cfg->pcie_bdf, PCIE_CONF_CMDSTAT_MEM, true);
	}
#endif

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	dev_data->iir_cache = 0U;
#endif

	old_level = irq_lock();

#ifdef UART_PPU_DLF_ENABLED
	OUTBYTE(DLF(dev), dev_data->dlf);
#endif

#ifdef UART_PPU_PCP_ENABLED
	u32_t pcp = dev_cfg->pcp;

	if (pcp) {
		pcp |= PCP_EN;
		OUTWORD(PCP(dev), pcp & ~PCP_UPDATE);
		OUTWORD(PCP(dev), pcp | PCP_UPDATE);
	}
#endif

	set_baud_rate(dev, cfg->baudrate);

	/* Local structure to hold temporary values to pass to OUTBYTE() */
	struct uart_config uart_cfg;

	switch (cfg->data_bits) {
	case UART_CFG_DATA_BITS_5:
		uart_cfg.data_bits = LCR_CS5;
		break;
	case UART_CFG_DATA_BITS_6:
		uart_cfg.data_bits = LCR_CS6;
		break;
	case UART_CFG_DATA_BITS_7:
		uart_cfg.data_bits = LCR_CS7;
		break;
	case UART_CFG_DATA_BITS_8:
		uart_cfg.data_bits = LCR_CS8;
		break;
	default:
		return -ENOTSUP;
	}

	switch (cfg->stop_bits) {
	case UART_CFG_STOP_BITS_1:
		uart_cfg.stop_bits = LCR_1_STB;
		break;
	case UART_CFG_STOP_BITS_2:
		uart_cfg.stop_bits = LCR_2_STB;
		break;
	default:
		return -ENOTSUP;
	}

	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		uart_cfg.parity = LCR_PDIS;
		break;
	case UART_CFG_PARITY_EVEN:
		uart_cfg.parity = LCR_EPS;
		break;
	default:
		return -ENOTSUP;
	}

	dev_data->uart_config = *cfg;

	/* data bits, stop bits, parity, clear DLAB */
	OUTBYTE(LCR(dev),
		uart_cfg.data_bits | uart_cfg.stop_bits | uart_cfg.parity);

	mdc = MCR_OUT2 | MCR_RTS | MCR_DTR;
#ifdef CONFIG_UART_NS16750
	if (cfg->flow_ctrl == UART_CFG_FLOW_CTRL_RTS_CTS) {
		mdc |= MCR_AFCE;
	}
#endif

	OUTBYTE(MDC(dev), mdc);

	/*
	 * Program FIFO: enabled, mode 0 (set for compatibility with quark),
	 * generate the interrupt at 8th byte
	 * Clear TX and RX FIFO
	 */
	OUTBYTE(FCR(dev),
		FCR_FIFO | FCR_MODE0 | FCR_FIFO_8 | FCR_RCVRCLR | FCR_XMITCLR
#ifdef CONFIG_UART_NS16750
		| FCR_FIFO_64
#endif
		);

	/* clear the port */
	INBYTE(RDR(dev));

	/* disable interrupts  */
	OUTBYTE(IER(dev), 0x00);

	irq_unlock(old_level);

	return 0;
};

static int uart_ppu_config_get(struct device *dev, struct uart_config *cfg)
{
	struct uart_ppu_dev_data_t *data = DEV_DATA(dev);

	cfg->baudrate = data->uart_config.baudrate;
	cfg->parity = data->uart_config.parity;
	cfg->stop_bits = data->uart_config.stop_bits;
	cfg->data_bits = data->uart_config.data_bits;
	cfg->flow_ctrl = data->uart_config.flow_ctrl;

	return 0;
}

/**
 * @brief Initialize individual UART port
 *
 * This routine is called to reset the chip in a quiescent state.
 *
 * @param dev UART device struct
 *
 * @return 0 if successful, failed otherwise
 */
static int uart_ppu_init(struct device *dev)
{
	uart_ppu_configure(dev, &DEV_DATA(dev)->uart_config);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	DEV_CFG(dev)->devconf.irq_config_func(dev);
#endif

	return 0;
}

/**
 * @brief Poll the device for input.
 *
 * @param dev UART device struct
 * @param c Pointer to character
 *
 * @return 0 if a character arrived, -1 if the input buffer if empty.
 */
static int uart_ppu_poll_in(struct device *dev, unsigned char *c)
{
	if ((INBYTE(LSR(dev)) & LSR_RXRDY) == 0x00) {
		return (-1);
	}

	/* got a character */
	*c = INBYTE(RDR(dev));

	return 0;
}

/**
 * @brief Output a character in polled mode.
 *
 * Checks if the transmitter is empty. If empty, a character is written to
 * the data register.
 *
 * If the hardware flow control is enabled then the handshake signal CTS has to
 * be asserted in order to send a character.
 *
 * @param dev UART device struct
 * @param c Character to send
 */
static void uart_ppu_poll_out(struct device *dev,
					   unsigned char c)
{
	/* wait for transmitter to ready to accept a character */
	while ((INBYTE(LSR(dev)) & LSR_THRE) == 0) {
	}

	OUTBYTE(THR(dev), c);
}

/**
 * @brief Check if an error was received
 *
 * @param dev UART device struct
 *
 * @return one of UART_ERROR_OVERRUN, UART_ERROR_PARITY, UART_ERROR_FRAMING,
 * UART_BREAK if an error was detected, 0 otherwise.
 */
static int uart_ppu_err_check(struct device *dev)
{
	return (INBYTE(LSR(dev)) & LSR_EOB_MASK) >> 1;
}

#if CONFIG_UART_INTERRUPT_DRIVEN

/**
 * @brief Fill FIFO with data
 *
 * @param dev UART device struct
 * @param tx_data Data to transmit
 * @param size Number of bytes to send
 *
 * @return Number of bytes sent
 */
static int uart_ppu_fifo_fill(struct device *dev, const u8_t *tx_data,
				  int size)
{
	int i;

	for (i = 0; i < size && (INBYTE(LSR(dev)) & LSR_THRE) != 0; i++) {
		OUTBYTE(THR(dev), tx_data[i]);
	}
	return i;
}

/**
 * @brief Read data from FIFO
 *
 * @param dev UART device struct
 * @param rxData Data container
 * @param size Container size
 *
 * @return Number of bytes read
 */
static int uart_ppu_fifo_read(struct device *dev, u8_t *rx_data,
				  const int size)
{
	int i;

	for (i = 0; i < size && (INBYTE(LSR(dev)) & LSR_RXRDY) != 0; i++) {
		rx_data[i] = INBYTE(RDR(dev));
	}

	return i;
}

/**
 * @brief Enable TX interrupt in IER
 *
 * @param dev UART device struct
 *
 * @return N/A
 */
static void uart_ppu_irq_tx_enable(struct device *dev)
{
	OUTBYTE(IER(dev), INBYTE(IER(dev)) | IER_TBE);
}

/**
 * @brief Disable TX interrupt in IER
 *
 * @param dev UART device struct
 *
 * @return N/A
 */
static void uart_ppu_irq_tx_disable(struct device *dev)
{
	OUTBYTE(IER(dev), INBYTE(IER(dev)) & (~IER_TBE));
}

/**
 * @brief Check if Tx IRQ has been raised
 *
 * @param dev UART device struct
 *
 * @return 1 if an IRQ is ready, 0 otherwise
 */
static int uart_ppu_irq_tx_ready(struct device *dev)
{
	return ((IIRC(dev) & IIR_ID) == IIR_THRE);
}

/**
 * @brief Check if nothing remains to be transmitted
 *
 * @param dev UART device struct
 *
 * @return 1 if nothing remains to be transmitted, 0 otherwise
 */
static int uart_ppu_irq_tx_complete(struct device *dev)
{
	return (INBYTE(LSR(dev)) & (LSR_TEMT | LSR_THRE)) == (LSR_TEMT | LSR_THRE);
}

/**
 * @brief Enable RX interrupt in IER
 *
 * @param dev UART device struct
 *
 * @return N/A
 */
static void uart_ppu_irq_rx_enable(struct device *dev)
{
	OUTBYTE(IER(dev), INBYTE(IER(dev)) | IER_RXRDY);
}

/**
 * @brief Disable RX interrupt in IER
 *
 * @param dev UART device struct
 *
 * @return N/A
 */
static void uart_ppu_irq_rx_disable(struct device *dev)
{
	OUTBYTE(IER(dev), INBYTE(IER(dev)) & (~IER_RXRDY));
}

/**
 * @brief Check if Rx IRQ has been raised
 *
 * @param dev UART device struct
 *
 * @return 1 if an IRQ is ready, 0 otherwise
 */
static int uart_ppu_irq_rx_ready(struct device *dev)
{
	return ((IIRC(dev) & IIR_ID) == IIR_RBRF);
}

/**
 * @brief Enable error interrupt in IER
 *
 * @param dev UART device struct
 *
 * @return N/A
 */
static void uart_ppu_irq_err_enable(struct device *dev)
{
	OUTBYTE(IER(dev), INBYTE(IER(dev)) | IER_LSR);
}

/**
 * @brief Disable error interrupt in IER
 *
 * @param dev UART device struct
 *
 * @return 1 if an IRQ is ready, 0 otherwise
 */
static void uart_ppu_irq_err_disable(struct device *dev)
{
	OUTBYTE(IER(dev), INBYTE(IER(dev)) & (~IER_LSR));
}

/**
 * @brief Check if any IRQ is pending
 *
 * @param dev UART device struct
 *
 * @return 1 if an IRQ is pending, 0 otherwise
 */
static int uart_ppu_irq_is_pending(struct device *dev)
{
	return (!(IIRC(dev) & IIR_NIP));
}

/**
 * @brief Update cached contents of IIR
 *
 * @param dev UART device struct
 *
 * @return Always 1
 */
static int uart_ppu_irq_update(struct device *dev)
{
	IIRC(dev) = INBYTE(IIR(dev));

	return 1;
}

/**
 * @brief Set the callback function pointer for IRQ.
 *
 * @param dev UART device struct
 * @param cb Callback function pointer.
 *
 * @return N/A
 */
static void uart_ppu_irq_callback_set(struct device *dev,
					  uart_irq_callback_user_data_t cb,
					  void *cb_data)
{
	struct uart_ppu_dev_data_t * const dev_data = DEV_DATA(dev);

	dev_data->cb = cb;
	dev_data->cb_data = cb_data;
}

/**
 * @brief Interrupt service routine.
 *
 * This simply calls the callback function, if one exists.
 *
 * @param arg Argument to ISR.
 *
 * @return N/A
 */
static void uart_ppu_isr(void *arg)
{
	struct device *dev = arg;
	struct uart_ppu_dev_data_t * const dev_data = DEV_DATA(dev);

	if (dev_data->cb) {
		dev_data->cb(dev_data->cb_data);
	}
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#ifdef CONFIG_UART_PPU_LINE_CTRL

/**
 * @brief Manipulate line control for UART.
 *
 * @param dev UART device struct
 * @param ctrl The line control to be manipulated
 * @param val Value to set the line control
 *
 * @return 0 if successful, failed otherwise
 */
static int uart_ppu_line_ctrl_set(struct device *dev,
				      u32_t ctrl, u32_t val)
{
	u32_t mdc, chg;

	switch (ctrl) {
	case UART_LINE_CTRL_BAUD_RATE:
		set_baud_rate(dev, val);
		return 0;

	case UART_LINE_CTRL_RTS:
	case UART_LINE_CTRL_DTR:
		mdc = INBYTE(MDC(dev));

		if (ctrl == UART_LINE_CTRL_RTS) {
			chg = MCR_RTS;
		} else {
			chg = MCR_DTR;
		}

		if (val) {
			mdc |= chg;
		} else {
			mdc &= ~(chg);
		}
		OUTBYTE(MDC(dev), mdc);
		return 0;
	}

	return -ENOTSUP;
}

#endif /* CONFIG_UART_PPU_LINE_CTRL */

#ifdef CONFIG_UART_PPU_DRV_CMD

/**
 * @brief Send extra command to driver
 *
 * @param dev UART device struct
 * @param cmd Command to driver
 * @param p Parameter to the command
 *
 * @return 0 if successful, failed otherwise
 */
static int uart_ppu_drv_cmd(struct device *dev, u32_t cmd, u32_t p)
{
#ifdef UART_PPU_DLF_ENABLED
	if (cmd == CMD_SET_DLF) {
		struct uart_ppu_dev_data_t * const dev_data = DEV_DATA(dev);
		dev_data->dlf = p;
		OUTBYTE(DLF(dev), dev_data->dlf);
		return 0;
	}
#endif

	return -ENOTSUP;
}

#endif /* CONFIG_UART_PPU_DRV_CMD */


static const struct uart_driver_api uart_ppu_driver_api = {
	.poll_in = uart_ppu_poll_in,
	.poll_out = uart_ppu_poll_out,
	.err_check = uart_ppu_err_check,
	.configure = uart_ppu_configure,
	.config_get = uart_ppu_config_get,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN

	.fifo_fill = uart_ppu_fifo_fill,
	.fifo_read = uart_ppu_fifo_read,
	.irq_tx_enable = uart_ppu_irq_tx_enable,
	.irq_tx_disable = uart_ppu_irq_tx_disable,
	.irq_tx_ready = uart_ppu_irq_tx_ready,
	.irq_tx_complete = uart_ppu_irq_tx_complete,
	.irq_rx_enable = uart_ppu_irq_rx_enable,
	.irq_rx_disable = uart_ppu_irq_rx_disable,
	.irq_rx_ready = uart_ppu_irq_rx_ready,
	.irq_err_enable = uart_ppu_irq_err_enable,
	.irq_err_disable = uart_ppu_irq_err_disable,
	.irq_is_pending = uart_ppu_irq_is_pending,
	.irq_update = uart_ppu_irq_update,
	.irq_callback_set = uart_ppu_irq_callback_set,

#endif

#ifdef CONFIG_UART_PPU_LINE_CTRL
	.line_ctrl_set = uart_ppu_line_ctrl_set,
#endif

#ifdef CONFIG_UART_PPU_DRV_CMD
	.drv_cmd = uart_ppu_drv_cmd,
#endif
};

// #include <uart_ppu_port_0.h>
// #include <uart_ppu_port_1.h>
// #include <uart_ppu_port_2.h>
// #include <uart_ppu_port_3.h>
#if DT_HAS_DRV_INST(0)

static struct uart_ppu_data uart_ppu_data_0;

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static void uart_ppu_irq_cfg_func_0(struct device *dev);
#endif

static const struct uart_ppu_device_config uart_ppu_dev_cfg_0 = {
	.uart_addr    = DT_INST_REG_ADDR(0),
	.sys_clk_freq = DT_INST_PROP(0, clock_frequency),
	.line_config  = PPU_UART_0_LINECFG,
	.baud_rate    = DT_INST_PROP(0, current_speed),
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
//	.cfg_func     = uart_ppu_irq_cfg_func_0,
#endif
};

DEVICE_AND_API_INIT(uart_ppu_0, DT_INST_LABEL(0),
		    uart_ppu_init, &uart_ppu_data_0, &uart_ppu_dev_cfg_0,
		    PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    (void *)&uart_ppu_driver_api);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
/*
static void uart_ppu_irq_cfg_func_0(struct device *dev)
{
	// Create a thread which will poll for data - replacement for IRQ
	k_thread_create(&rx_thread, rx_stack, 500,
			uart_ppu_rx_thread, dev, NULL, NULL, K_PRIO_COOP(2),
			0, K_NO_WAIT);
}
*/
#endif

#endif /* DT_HAS_DRV_INST(0) */