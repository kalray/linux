// SPDX-License-Identifier: GPL-2.0-only
/*
 * Designware SPI core controller driver (refer pxa2xx_spi.c)
 *
 * Copyright (c) 2009, Intel Corporation.
 * Copyright (c) 2020, Kalray Inc.
 * Author: Clement Leger
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/swab.h>
#include <linux/spi/spi.h>

#include "spi-dw.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

/* Slave spi_dev related */
struct chip_data {
	u8 tmode;		/* TR/TO/RO/EEPROM */
	u8 type;		/* SPI/SSP/MicroWire */
	u8 spi_frf;             /* DUAL/QUAD/OCTAL */

	u16 clk_div;		/* baud rate divider */
	u32 speed_hz;		/* baud rate */
};

#ifdef CONFIG_DEBUG_FS

#define DW_SPI_DBGFS_REG(_name, _off)	\
{					\
	.name = _name,			\
	.offset = _off,			\
}

static const struct debugfs_reg32 dw_spi_dbgfs_regs[] = {
	DW_SPI_DBGFS_REG("CTRLR0", DW_SPI_CTRLR0),
	DW_SPI_DBGFS_REG("CTRLR1", DW_SPI_CTRLR1),
	DW_SPI_DBGFS_REG("SSIENR", DW_SPI_SSIENR),
	DW_SPI_DBGFS_REG("SER", DW_SPI_SER),
	DW_SPI_DBGFS_REG("BAUDR", DW_SPI_BAUDR),
	DW_SPI_DBGFS_REG("TXFTLR", DW_SPI_TXFTLR),
	DW_SPI_DBGFS_REG("RXFTLR", DW_SPI_RXFTLR),
	DW_SPI_DBGFS_REG("TXFLR", DW_SPI_TXFLR),
	DW_SPI_DBGFS_REG("RXFLR", DW_SPI_RXFLR),
	DW_SPI_DBGFS_REG("SR", DW_SPI_SR),
	DW_SPI_DBGFS_REG("IMR", DW_SPI_IMR),
	DW_SPI_DBGFS_REG("ISR", DW_SPI_ISR),
	DW_SPI_DBGFS_REG("DMACR", DW_SPI_DMACR),
	DW_SPI_DBGFS_REG("DMATDLR", DW_SPI_DMATDLR),
	DW_SPI_DBGFS_REG("DMARDLR", DW_SPI_DMARDLR),
};

static int dw_spi_debugfs_init(struct dw_spi *dws)
{
	char name[32];

	snprintf(name, 32, "dw_spi%d", dws->master->bus_num);
	dws->debugfs = debugfs_create_dir(name, NULL);
	if (!dws->debugfs)
		return -ENOMEM;

	dws->regset.regs = dw_spi_dbgfs_regs;
	dws->regset.nregs = ARRAY_SIZE(dw_spi_dbgfs_regs);
	dws->regset.base = dws->regs;
	debugfs_create_regset32("registers", 0400, dws->debugfs, &dws->regset);

	return 0;
}

static void dw_spi_debugfs_remove(struct dw_spi *dws)
{
	debugfs_remove_recursive(dws->debugfs);
}

#else
static inline int dw_spi_debugfs_init(struct dw_spi *dws)
{
	return 0;
}

static inline void dw_spi_debugfs_remove(struct dw_spi *dws)
{
}
#endif /* CONFIG_DEBUG_FS */

void dw_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct dw_spi *dws = spi_controller_get_devdata(spi->controller);
	bool cs_high = !!(spi->mode & SPI_CS_HIGH);

	/*
	 * DW SPI controller demands any native CS being set in order to
	 * proceed with data transfer. So in order to activate the SPI
	 * communications we must set a corresponding bit in the Slave
	 * Enable register no matter whether the SPI core is configured to
	 * support active-high or active-low CS level.
	 */
	if (cs_high == enable)
		dw_writel(dws, DW_SPI_SER, BIT(spi->chip_select));
	else if (dws->cs_override)
		dw_writel(dws, DW_SPI_SER, 0);
}
EXPORT_SYMBOL_GPL(dw_spi_set_cs);

/* Return the max entries we can fill into tx fifo */
static inline u32 tx_max(struct dw_spi *dws)
{
	u32 tx_left, tx_room, rxtx_gap;

	tx_left = (dws->tx_end - dws->tx) / dws->n_bytes;
	tx_room = dws->fifo_len - dw_readl(dws, DW_SPI_TXFLR);

	/*
	 * Another concern is about the tx/rx mismatch, we
	 * though to use (dws->fifo_len - rxflr - txflr) as
	 * one maximum value for tx, but it doesn't cover the
	 * data which is out of tx/rx fifo and inside the
	 * shift registers. So a control from sw point of
	 * view is taken.
	 */
	rxtx_gap =  ((dws->rx_end - dws->rx) - (dws->tx_end - dws->tx))
			/ dws->n_bytes;

	return min3(tx_left, tx_room, (u32) (dws->fifo_len - rxtx_gap));
}

/* Return the max entries we should read out of rx fifo */
static inline u32 rx_max(struct dw_spi *dws)
{
	u32 rx_left = (dws->rx_end - dws->rx) / dws->n_bytes;

	return min_t(u32, rx_left, dw_readl(dws, DW_SPI_RXFLR));
}

static void dw_writer(struct dw_spi *dws)
{
	u32 max;
	u32 txw = 0;

	spin_lock(&dws->buf_lock);
	max = tx_max(dws);
	while (max--) {
		/* Set the tx word if the transfer's original "tx" is not null */
		if (dws->tx_end - dws->len) {
			if (dws->n_bytes == 1)
				txw = *(u8 *)(dws->tx);
			else if (dws->n_bytes == 2)
                                txw = *(u16 *)(dws->tx);
			else
				txw = *(u32 *)(dws->tx);
		}
		dw_write_io_reg(dws, DW_SPI_DR, txw);
		dws->tx += dws->n_bytes;
	}
	spin_unlock(&dws->buf_lock);
}

static void dw_reader(struct dw_spi *dws)
{
	u32 max;
	u32 rxw;

	spin_lock(&dws->buf_lock);
	max = rx_max(dws);
	while (max--) {
		rxw = dw_read_io_reg(dws, DW_SPI_DR);
		/* Care rx only if the transfer's original "rx" is not null */
		if (dws->rx_end - dws->len) {
			if (dws->n_bytes == 1)
				*(u8 *)(dws->rx) = rxw;
			else if (dws->n_bytes == 2)
				*(u16 *)(dws->rx) = rxw;
			else
				*(u32 *)(dws->rx) = rxw;
		}
		dws->rx += dws->n_bytes;
	}
	spin_unlock(&dws->buf_lock);
}

static void int_error_stop(struct dw_spi *dws, const char *msg)
{
	spi_reset_chip(dws);

	dev_err(&dws->master->dev, "%s\n", msg);
	dws->master->cur_msg->status = -EIO;
	spi_finalize_current_transfer(dws->master);
}

static irqreturn_t interrupt_transfer(struct dw_spi *dws)
{
	u16 irq_status = dw_readl(dws, DW_SPI_ISR);

	/* Error handling */
	if (irq_status & (SPI_INT_TXOI | SPI_INT_RXOI | SPI_INT_RXUI)) {
		dw_readl(dws, DW_SPI_ICR);
		int_error_stop(dws, "interrupt_transfer: fifo overrun/underrun");
		return IRQ_HANDLED;
	}

	dw_reader(dws);
	if (dws->rx_end == dws->rx) {
		spi_mask_intr(dws, SPI_INT_TXEI);
		spi_finalize_current_transfer(dws->master);
		return IRQ_HANDLED;
	}
	if (irq_status & SPI_INT_TXEI) {
		spi_mask_intr(dws, SPI_INT_TXEI);
		dw_writer(dws);
		/* Enable TX irq always, it will be disabled when RX finished */
		spi_umask_intr(dws, SPI_INT_TXEI);
	}

	return IRQ_HANDLED;
}

static irqreturn_t dw_spi_irq(int irq, void *dev_id)
{
	struct spi_controller *master = dev_id;
	struct dw_spi *dws = spi_controller_get_devdata(master);
	u16 irq_status = dw_readl(dws, DW_SPI_ISR) & 0x3f;

	if (!irq_status)
		return IRQ_NONE;

	/* When using spimem, there is no cur_msg member */
	if (!dws->mem_op && !master->cur_msg) {
		spi_mask_intr(dws, SPI_INT_TXEI);
		return IRQ_HANDLED;
	}

	return dws->transfer_handler(dws);
}

static void dw_spi_setup_xfer(struct dw_spi *dws, struct spi_device *spi,
			     u32 speed_hz, u8 bpw)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	u32 cr0;

	/* Handle per transfer options for bpw and speed */
	if (speed_hz != dws->current_freq) {
		if (speed_hz != chip->speed_hz) {
			/* clk_div doesn't support odd number */
			chip->clk_div = (DIV_ROUND_UP(dws->max_freq, speed_hz) + 1) & 0xfffe;
			chip->speed_hz = speed_hz;
		}
		dws->current_freq = speed_hz;
		spi_set_clk(dws, chip->clk_div);
	}

	cr0 = dws->update_cr0(spi, bpw);

	dw_writel(dws, DW_SPI_CTRLR0, cr0);
}

/* Configure CTRLR0 for DW_apb_ssi */
u32 dw_spi_update_cr0(struct spi_device *spi, u8 bpw)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	u32 cr0;

	/* Default SPI mode is SCPOL = 0, SCPH = 0 */
	cr0 = (bpw - 1)
		| (chip->type << SPI_FRF_OFFSET)
		| ((((spi->mode & SPI_CPOL) ? 1 : 0) << SPI_SCPOL_OFFSET) |
		   (((spi->mode & SPI_CPHA) ? 1 : 0) << SPI_SCPH_OFFSET) |
		   (((spi->mode & SPI_LOOP) ? 1 : 0) << SPI_SRL_OFFSET))
		| (chip->tmode << SPI_TMOD_OFFSET);

	return cr0;
}
EXPORT_SYMBOL_GPL(dw_spi_update_cr0);

/* Configure CTRLR0 for DWC_ssi */
u32 dw_spi_update_cr0_v1_01a(struct spi_device *spi, u8 bpw)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	u32 cr0;

	/* CTRLR0[ 4: 0] Data Frame Size */
	cr0 = (bpw - 1);

	/* CTRLR0[ 7: 6] Frame Format */
	cr0 |= chip->type << DWC_SSI_CTRLR0_FRF_OFFSET;

	/*
	 * SPI mode (SCPOL|SCPH)
	 * CTRLR0[ 8] Serial Clock Phase
	 * CTRLR0[ 9] Serial Clock Polarity
	 */
	cr0 |= ((spi->mode & SPI_CPOL) ? 1 : 0) << DWC_SSI_CTRLR0_SCPOL_OFFSET;
	cr0 |= ((spi->mode & SPI_CPHA) ? 1 : 0) << DWC_SSI_CTRLR0_SCPH_OFFSET;

	/* CTRLR0[11:10] Transfer Mode */
	cr0 |= chip->tmode << DWC_SSI_CTRLR0_TMOD_OFFSET;

	/* CTRLR0[13] Shift Register Loop */
	cr0 |= ((spi->mode & SPI_LOOP) ? 1 : 0) << DWC_SSI_CTRLR0_SRL_OFFSET;

	/* CTRLR0[23:22] SPI Frame Format */
	cr0 |= chip->spi_frf << DWC_SSI_CTRLR0_SPI_FRF_OFFSET;

	return cr0;
}
EXPORT_SYMBOL_GPL(dw_spi_update_cr0_v1_01a);

static int dw_spi_transfer_one(struct spi_controller *master,
		struct spi_device *spi, struct spi_transfer *transfer)
{
	struct dw_spi *dws = spi_controller_get_devdata(master);
	struct chip_data *chip = spi_get_ctldata(spi);
	unsigned long flags;
	u8 imask = 0;
	u16 txlevel = 0;
	int ret;

	dws->dma_mapped = 0;
	spin_lock_irqsave(&dws->buf_lock, flags);
	dws->tx = (void *)transfer->tx_buf;
	dws->tx_end = dws->tx + transfer->len;
	dws->rx = transfer->rx_buf;
	dws->rx_end = dws->rx + transfer->len;
	dws->len = transfer->len;
	spin_unlock_irqrestore(&dws->buf_lock, flags);

	/* Ensure dw->rx and dw->rx_end are visible */
	smp_mb();

	spi_enable_chip(dws, 0);

	dw_spi_setup_xfer(dws, spi, transfer->speed_hz, transfer->bits_per_word);

	transfer->effective_speed_hz = dws->max_freq / chip->clk_div;
	dws->n_bytes = DIV_ROUND_UP(transfer->bits_per_word, BITS_PER_BYTE);

	/* Check if current transfer is a DMA transaction */
	if (master->can_dma && master->can_dma(master, spi, transfer))
		dws->dma_mapped = master->cur_msg_mapped;

	/* For poll mode just disable all interrupts */
	spi_mask_intr(dws, 0xff);

	/*
	 * Interrupt mode
	 * we only need set the TXEI IRQ, as TX/RX always happen syncronizely
	 */
	if (dws->dma_mapped) {
		ret = dws->dma_ops->dma_setup(dws, transfer);
		if (ret < 0) {
			spi_enable_chip(dws, 1);
			return ret;
		}
	} else {
		txlevel = min_t(u16, dws->fifo_len / 2, dws->len / dws->n_bytes);
		dw_writel(dws, DW_SPI_TXFTLR, txlevel);

		/* Set the interrupt mask */
		imask |= SPI_INT_TXEI | SPI_INT_TXOI |
			 SPI_INT_RXUI | SPI_INT_RXOI;
		spi_umask_intr(dws, imask);

		dws->transfer_handler = interrupt_transfer;
	}

	spi_enable_chip(dws, 1);

	if (dws->dma_mapped)
		return dws->dma_ops->dma_transfer(dws, transfer);

	return 1;
}

static void dw_spi_handle_err(struct spi_controller *master,
		struct spi_message *msg)
{
	struct dw_spi *dws = spi_controller_get_devdata(master);

	if (dws->dma_mapped)
		dws->dma_ops->dma_stop(dws);

	spi_reset_chip(dws);
}

/* This may be called twice for each spi dev */
static int dw_spi_setup(struct spi_device *spi)
{
	struct chip_data *chip;

	/* Only alloc on first setup */
	chip = spi_get_ctldata(spi);
	if (!chip) {
		chip = kzalloc(sizeof(struct chip_data), GFP_KERNEL);
		if (!chip)
			return -ENOMEM;
		spi_set_ctldata(spi, chip);
	}

	chip->tmode = SPI_TMOD_TR;
	chip->spi_frf = SPI_SPI_FRF_STANDARD;

	return 0;
}

static void dw_spi_cleanup(struct spi_device *spi)
{
	struct chip_data *chip = spi_get_ctldata(spi);

	kfree(chip);
	spi_set_ctldata(spi, NULL);
}

static void dw_spi_mem_enhanced_read_rx_fifo(struct dw_spi *dws)
{
	const struct spi_mem_op *op = dws->mem_op;
	unsigned int max_data = dw_readl(dws, DW_SPI_RXFLR);
	unsigned int remaining;
	u8 *buf = op->data.buf.in;
	u32 val;

	while (max_data--) {
		val = dw_read_io_reg(dws, DW_SPI_DR);
		if (dws->bytes_per_word == 4) {
			val = swab32(val);
			memcpy(&buf[dws->cur_data_off], &val, 4);
		} else {
			buf[dws->cur_data_off] = val;
		}

		dws->cur_data_off += dws->bytes_per_word;
	}

	if (dws->cur_data_off == dws->cur_xfer_size)
		return;

	/*
	 * Transfer is not over, we want to trigger an interrupt for the
	 * remaining bytes to come
	 */
	remaining = dws->cur_xfer_size - dws->cur_data_off;
	remaining >>= ffs(dws->bytes_per_word) - 1;
	if (remaining < dws->fifo_len)
		dw_writel(dws, DW_SPI_RXFTLR, (remaining - 1));
}

static void dw_spi_mem_enhanced_write_tx_fifo(struct dw_spi *dws)
{
	const struct spi_mem_op *op = dws->mem_op;
	unsigned int max_data = dws->fifo_len - dw_readl(dws, DW_SPI_TXFLR);
	const u8 *buf = op->data.buf.out;
	u32 val;

	while (max_data--) {
		if (dws->cur_data_off == dws->cur_xfer_size)
			break;

		if (dws->bytes_per_word == 4) {
			memcpy(&val, &buf[dws->cur_data_off], 4);
			val = swab32(val);
		} else {
			val = buf[dws->cur_data_off];
		}

		dw_write_io_reg(dws, DW_SPI_DR, val);
		dws->cur_data_off += dws->bytes_per_word;
	}
}

static void dw_spi_mem_std_read_rx_fifo(struct dw_spi *dws)
{
	const struct spi_mem_op *op = dws->mem_op;
	u16 cmd_addr_dummy_len = 1 + op->addr.nbytes + op->dummy.nbytes;
	u8 byte;
	u8 *buf = op->data.buf.in;
	int i, off = 0;

	for (i = 0; i < dws->cur_xfer_size; i++) {
		byte = dw_read_io_reg(dws, DW_SPI_DR);
		/* First bytes read are only sampled data on TX for cmd */
		if (i < cmd_addr_dummy_len)
			continue;

		buf[off] = byte;
		off++;
	}
}

static void spi_mem_finish_transfer(struct dw_spi *dws)
{
	spi_mask_intr(dws, 0xff);
	dws->comp_status = 0;
	complete(&dws->comp);
}


static void dw_spi_mem_handle_irq(struct dw_spi *dws)
{
	if (!dws->enhanced_xfer) {
		/* We were expecting data, read the rx fifo */
		if (dws->mem_op->data.dir == SPI_MEM_DATA_IN)
			dw_spi_mem_std_read_rx_fifo(dws);

		spi_mem_finish_transfer(dws);
	} else {
		spin_lock(&dws->buf_lock);
		if (dws->mem_op->data.dir == SPI_MEM_DATA_IN)
			dw_spi_mem_enhanced_read_rx_fifo(dws);

		if (dws->cur_data_off == dws->cur_xfer_size) {
			spi_mem_finish_transfer(dws);
			spin_unlock(&dws->buf_lock);
			return;
		}
		if (dws->mem_op->data.dir == SPI_MEM_DATA_OUT)
			dw_spi_mem_enhanced_write_tx_fifo(dws);

		spin_unlock(&dws->buf_lock);
	}
}

static irqreturn_t dw_spi_mem_irq(struct dw_spi *dws)
{
	u16 irq_status = dw_readl(dws, DW_SPI_ISR);

	/* Error handling */
	if (irq_status & (SPI_INT_TXOI | SPI_INT_RXOI | SPI_INT_RXUI)) {
		dw_readl(dws, DW_SPI_ICR);
		spi_reset_chip(dws);
		complete(&dws->comp);
		return IRQ_HANDLED;
	}

	if (irq_status & SPI_INT_RXFI) {
		/* This is a spurious IRQ, should not happen */
		if (dws->mem_op->data.dir != SPI_MEM_DATA_IN) {
			pr_err("Unexpected RX full irq\n");
			return IRQ_HANDLED;
		}
		dw_spi_mem_handle_irq(dws);
	}

	if (irq_status & SPI_INT_TXEI) {
		/* This is a spurious IRQ, should not happen */
		if (dws->mem_op->data.dir == SPI_MEM_DATA_IN) {
			pr_err("Unexpected TX empty irq\n");
			return IRQ_HANDLED;
		}
		dw_spi_mem_handle_irq(dws);
	}

	return IRQ_HANDLED;
}

static void dw_spi_mem_reset_xfer(struct dw_spi *dws,
				  const struct spi_mem_op *op)
{
	reinit_completion(&dws->comp);
	dws->mem_op = op;
	dws->cur_data_off = 0;
	dws->comp_status = -EIO;
	dws->enhanced_xfer = 0;
	dws->transfer_handler = dw_spi_mem_irq;
	dws->bytes_per_word = 1;
}

static void dw_spi_mem_start_std_op(struct dw_spi *dws,
				    const struct spi_mem_op *op)
{
	int i;
	u8 byte;
	u16 cmd_addr_dummy_len = 1 + op->addr.nbytes + op->dummy.nbytes;
	/*
	 * We adjusted the transfer size to ensure there will be no more than
	 * fifo_len bytes to send, so there is no need to chek the length
	 */
	unsigned int xfer_size = op->data.nbytes + cmd_addr_dummy_len;
	const u8 *buf = op->data.buf.out;

	/* This is the amount of data that will need to be read from the FIFO */
	dws->cur_xfer_size = xfer_size;

	spi_enable_chip(dws, 0);
	spi_mask_intr(dws, 0xff);

	/* When reading, we only care about the receive fifo being empty */
	if (op->data.dir == SPI_MEM_DATA_IN) {
		/* Set RX fifo level to trigger a rx fifo full interrupt */
		dw_writel(dws, DW_SPI_RXFTLR, (xfer_size - 1));

		spi_umask_intr(dws, SPI_INT_RXFI);
	} else {
		/* We will refill the tx on tx empty fifo interrupt */
		spi_umask_intr(dws, SPI_INT_TXEI);
	}

	/* Set TXFTL start fifo level*/
	dw_writel(dws, DW_SPI_TXFTLR, (xfer_size - 1) << SPI_TXFTL_FTHR);

	spi_enable_chip(dws, 1);

	dw_write_io_reg(dws, DW_SPI_DR, op->cmd.opcode);

	if (op->addr.nbytes) {
		/* Send address MSB first */
		for (i = op->addr.nbytes - 1; i >= 0; i--) {
			byte = (op->addr.val >> (i * 8)) & 0xff;
			dw_write_io_reg(dws, DW_SPI_DR, byte);
		}
	}

	for (i = 0; i < op->dummy.nbytes; i++)
		dw_write_io_reg(dws, DW_SPI_DR, 0xff);

	byte = 0xff;
	/* Then send all data up to data_size */
	for (i = 0; i < op->data.nbytes; i++) {
		if (op->data.dir == SPI_MEM_DATA_OUT)
			byte = buf[i];

		dw_write_io_reg(dws, DW_SPI_DR, byte);
	}
}

static void dw_spi_mem_exec_std(struct spi_device *spi,
			       const struct spi_mem_op *op)
{
	struct dw_spi *dws = spi_controller_get_devdata(spi->master);
	struct chip_data *chip = spi_get_ctldata(spi);

	dw_spi_mem_reset_xfer(dws, op);

	if (op->data.dir == SPI_MEM_DATA_IN)
		chip->tmode = SPI_TMOD_TR;
	else
		chip->tmode = SPI_TMOD_TO;

	chip->spi_frf = SPI_SPI_FRF_STANDARD;

	spi_enable_chip(dws, 0);
	dw_spi_setup_xfer(dws, spi, spi->max_speed_hz, 8);
	spi_enable_chip(dws, 1);

	dw_spi_mem_start_std_op(dws, op);
}

static void dw_spi_mem_setup_enhanced_xfer(struct dw_spi *dws,
				      struct spi_device *spi,
				      const struct spi_mem_op *op)
{
	struct chip_data *chip = spi_get_ctldata(spi);
	u32 spi_ctrl0 = 0;
	u32 wait_cycles = 0;
	u8 addr_l = 0;

	dw_spi_mem_reset_xfer(dws, op);
	dws->enhanced_xfer = 1;

	if (op->data.dir == SPI_MEM_DATA_IN)
		chip->tmode = SPI_TMOD_RO;
	else
		chip->tmode = SPI_TMOD_TO;

	if (op->data.buswidth == 8)
		chip->spi_frf = SPI_SPI_FRF_OCTAL;
	else if (op->data.buswidth == 4)
		chip->spi_frf = SPI_SPI_FRF_QUAD;
	else if (op->data.buswidth == 2)
		chip->spi_frf = SPI_SPI_FRF_DUAL;

	if (IS_ALIGNED(op->data.nbytes, 4))
		dws->bytes_per_word = 4;

	if (op->addr.nbytes == 1)
		addr_l = SPI_CTRL0_ADDR_L8;
	else if (op->addr.nbytes == 2)
		addr_l = SPI_CTRL0_ADDR_L16;
	else if (op->addr.nbytes == 3)
		addr_l = SPI_CTRL0_ADDR_L24;
	else if (op->addr.nbytes == 4)
		addr_l = SPI_CTRL0_ADDR_L32;

	if (op->dummy.nbytes && op->dummy.buswidth)
		wait_cycles = (op->dummy.nbytes * 8) / op->dummy.buswidth;

	spi_ctrl0 = (addr_l << SPI_CTRL0_ADDR_L_OFFSET) |
		    (1 << SPI_CTRL0_CLK_STRETCH_OFFSET) |
		    (wait_cycles << SPI_CTRL0_WAIT_CYCLES_OFFSET) |
		    (SPI_SPI_CTRL0_INST_L8 << SPI_CTRL0_INST_L_OFFSET);

	spi_enable_chip(dws, 0);
	dw_spi_setup_xfer(dws, spi, spi->max_speed_hz, dws->bytes_per_word * 8);
	dw_writel(dws, DW_SPI_SPI_CTRL0, spi_ctrl0);
	spi_enable_chip(dws, 1);
}

static void dw_spi_mem_start_enhanced_op(struct dw_spi *dws,
					const struct spi_mem_op *op)
{
	unsigned int thres;
	unsigned long flags;
	unsigned long fifo_count = op->data.nbytes;

	fifo_count >>= ffs(dws->bytes_per_word) - 1;
	dws->cur_xfer_size = op->data.nbytes;
	spi_enable_chip(dws, 0);
	spi_mask_intr(dws, 0xff);

	dw_writel(dws, DW_SPI_CTRLR1, fifo_count - 1);
	if (op->data.dir == SPI_MEM_DATA_IN) {
		thres = min_t(unsigned long, dws->fifo_len, fifo_count);
		if (thres < 1)
			thres = 1;
		dw_writel(dws, DW_SPI_RXFTLR, thres - 1);
		dw_writel(dws, DW_SPI_TXFTLR, 0);

		spi_umask_intr(dws, SPI_INT_RXFI);
	} else {
		/*
		 * Since we send a command + opcode, we need to set the start
		 * threshold to at least 2
		 */
		thres = 2;
		dw_writel(dws, DW_SPI_TXFTLR, thres << SPI_TXFTL_FTHR);
	}

	spi_enable_chip(dws, 1);

	spin_lock_irqsave(&dws->buf_lock, flags);
	dw_write_io_reg(dws, DW_SPI_DR, op->cmd.opcode);
	dw_write_io_reg(dws, DW_SPI_DR, op->addr.val);

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		dw_spi_mem_enhanced_write_tx_fifo(dws);
		/* Unmask tx empty interrupt after data have been pushed in the fifo */
		spi_umask_intr(dws, SPI_INT_TXEI);
	}

	spin_unlock_irqrestore(&dws->buf_lock, flags);
}

static void dw_spi_mem_exec_enhanced(struct spi_device *spi,
				    const struct spi_mem_op *op)
{
	struct dw_spi *dws = spi_controller_get_devdata(spi->master);

	dw_spi_mem_setup_enhanced_xfer(dws, spi, op);
	dw_spi_mem_start_enhanced_op(dws, op);
}

static bool dw_spi_mem_is_enhanced(const struct spi_mem_op *op)
{
	/*
	 * From a controller POV, an enhanced transfer is using more then 1 wire
	 * of data
	 */
	return op->data.buswidth > 1;
}

static int dw_spi_mem_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct dw_spi *dws = spi_controller_get_devdata(mem->spi->master);
	unsigned long timeout;
	u32 sr;

	/* Select the slave, it will only be asserted when clock starts */
	dw_spi_set_cs(mem->spi, true);

	if (dw_spi_mem_is_enhanced(op))
		dw_spi_mem_exec_enhanced(mem->spi, op);
	else
		dw_spi_mem_exec_std(mem->spi, op);

	timeout = wait_for_completion_timeout(&dws->comp, HZ);
	if (!timeout) {
		dev_err(&dws->master->dev, "completion timeout");
		goto err_io;
	}

	if (dws->comp_status != 0) {
		dev_err(&dws->master->dev, "completion error");
		goto err_io;
	}

	/* Wait for TFE bit to go up */
	timeout = readl_poll_timeout(dws->regs + DW_SPI_SR, sr,
				     sr & SR_TF_EMPT, 0, USEC_PER_SEC);
	if (timeout) {
		dev_err(&dws->master->dev, "wait for transmit fifo empty failed\n");
		goto err_io;
	}

	/* Wait for BUSY bit to go down */
	timeout = readl_poll_timeout(dws->regs + DW_SPI_SR, sr,
				     !(sr & SR_BUSY), 0, USEC_PER_SEC);
	if (timeout) {
		dev_err(&dws->master->dev, "wait for end of busy failed\n");
		goto err_io;
	}

	dw_spi_set_cs(mem->spi, false);

	return 0;

err_io:
	spi_reset_chip(dws);
	return -EIO;

}

static bool dw_spi_mem_supports_op(struct spi_mem *mem,
				   const struct spi_mem_op *op)
{
	const int max_wait_cyle = SPI_CTRL0_WAIT_CYCLES_MASK;

	if (op->addr.nbytes > 4)
		return false;

	/* We only support 1-1-X commands */
	if (op->addr.buswidth > 1 || op->addr.buswidth > 1)
		return false;

	/* Check maximum number of wait cycles */
	if (op->dummy.nbytes &&
	    (op->dummy.nbytes * 8 / op->dummy.buswidth > max_wait_cyle))
		return false;

	return spi_mem_default_supports_op(mem, op);
}

int dw_spi_mem_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	struct dw_spi *dws = spi_controller_get_devdata(mem->spi->master);
	unsigned int max_size;

	if (dw_spi_mem_is_enhanced(op)) {
		/*
		 * Reduce to maximum NDF * 4 in enhanced_xfer mode since we will
		 * read bytes 4 by 4.
		 */
		op->data.nbytes = min_t(unsigned int, op->data.nbytes,
					SPI_CTRL1_NDF_MASK * 4);

		/* Align on 4 to push 4 bytes at once in the fifo */
		if (op->data.nbytes > 3)
			op->data.nbytes = ALIGN_DOWN(op->data.nbytes, 4);
	} else {
		max_size = dws->fifo_len - 1 - op->addr.nbytes -
			   op->dummy.nbytes;
		op->data.nbytes = min(op->data.nbytes, max_size);
	}

	return 0;
}

static const struct spi_controller_mem_ops dw_spi_mem_ops = {
	.supports_op = dw_spi_mem_supports_op,
	.exec_op = dw_spi_mem_exec_op,
	.adjust_op_size = dw_spi_mem_adjust_op_size,
};

/* Restart the controller, disable all interrupts, clean rx fifo */
static void spi_hw_init(struct device *dev, struct dw_spi *dws)
{
	spi_reset_chip(dws);

	/*
	 * Try to detect the FIFO depth if not set by interface driver,
	 * the depth could be from 2 to 256 from HW spec
	 */
	if (!dws->fifo_len) {
		u32 fifo;

		for (fifo = 1; fifo < 256; fifo++) {
			dw_writel(dws, DW_SPI_TXFTLR, fifo);
			if (fifo != dw_readl(dws, DW_SPI_TXFTLR))
				break;
		}
		dw_writel(dws, DW_SPI_TXFTLR, 0);

		dws->fifo_len = (fifo == 1) ? 0 : fifo;
		dev_dbg(dev, "Detected FIFO size: %u bytes\n", dws->fifo_len);
	}

	/* enable HW fixup for explicit CS deselect for Amazon's alpine chip */
	if (dws->cs_override)
		dw_writel(dws, DW_SPI_CS_OVERRIDE, 0xF);
}

int dw_spi_add_host(struct device *dev, struct dw_spi *dws)
{
	struct spi_controller *master;
	int ret;

	if (!dws)
		return -EINVAL;

	master = spi_alloc_master(dev, 0);
	if (!master)
		return -ENOMEM;

	dws->master = master;
	dws->type = SSI_MOTO_SPI;
	dws->dma_addr = (dma_addr_t)(dws->paddr + DW_SPI_DR);
	spin_lock_init(&dws->buf_lock);

	spi_controller_set_devdata(master, dws);

	ret = request_irq(dws->irq, dw_spi_irq, IRQF_SHARED, dev_name(dev),
			  master);
	if (ret < 0) {
		dev_err(dev, "can not get IRQ\n");
		goto err_free_master;
	}

	master->use_gpio_descriptors = true;
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LOOP;
	master->bits_per_word_mask =  SPI_BPW_RANGE_MASK(4, 16);
	master->bus_num = dws->bus_num;
	master->num_chipselect = dws->num_cs;
	master->setup = dw_spi_setup;
	master->cleanup = dw_spi_cleanup;
	master->set_cs = dw_spi_set_cs;
	master->transfer_one = dw_spi_transfer_one;
	master->handle_err = dw_spi_handle_err;
	master->max_speed_hz = dws->max_freq;
	master->dev.of_node = dev->of_node;
	master->dev.fwnode = dev->fwnode;
	master->flags = SPI_MASTER_GPIO_SS;
	master->auto_runtime_pm = true;

	if (dws->needs_spi_mem) {
		master->mem_ops = &dw_spi_mem_ops;
		init_completion(&dws->comp);
	}

	if (dws->support_enhanced) {
		master->mode_bits |= SPI_RX_DUAL | SPI_TX_DUAL |
				     SPI_RX_QUAD | SPI_TX_QUAD |
				     SPI_RX_OCTAL | SPI_TX_OCTAL;
	}

	if (dws->bpw_mask)
		master->bits_per_word_mask = dws->bpw_mask;

	if (dws->set_cs)
		master->set_cs = dws->set_cs;

	/* Basic HW init */
	spi_hw_init(dev, dws);

	if (dws->dma_ops && dws->dma_ops->dma_init) {
		ret = dws->dma_ops->dma_init(dev, dws);
		if (ret) {
			dev_warn(dev, "DMA init failed\n");
		} else {
			master->can_dma = dws->dma_ops->can_dma;
			master->flags |= SPI_CONTROLLER_MUST_TX;
		}
	}

	ret = spi_register_controller(master);
	if (ret) {
		dev_err(&master->dev, "problem registering spi master\n");
		goto err_dma_exit;
	}

	dw_spi_debugfs_init(dws);
	return 0;

err_dma_exit:
	if (dws->dma_ops && dws->dma_ops->dma_exit)
		dws->dma_ops->dma_exit(dws);
	spi_enable_chip(dws, 0);
	free_irq(dws->irq, master);
err_free_master:
	spi_controller_put(master);
	return ret;
}
EXPORT_SYMBOL_GPL(dw_spi_add_host);

void dw_spi_remove_host(struct dw_spi *dws)
{
	dw_spi_debugfs_remove(dws);

	spi_unregister_controller(dws->master);

	if (dws->dma_ops && dws->dma_ops->dma_exit)
		dws->dma_ops->dma_exit(dws);

	spi_shutdown_chip(dws);

	free_irq(dws->irq, dws->master);
}
EXPORT_SYMBOL_GPL(dw_spi_remove_host);

int dw_spi_suspend_host(struct dw_spi *dws)
{
	int ret;

	ret = spi_controller_suspend(dws->master);
	if (ret)
		return ret;

	spi_shutdown_chip(dws);
	return 0;
}
EXPORT_SYMBOL_GPL(dw_spi_suspend_host);

int dw_spi_resume_host(struct dw_spi *dws)
{
	spi_hw_init(&dws->master->dev, dws);
	return spi_controller_resume(dws->master);
}
EXPORT_SYMBOL_GPL(dw_spi_resume_host);

MODULE_AUTHOR("Feng Tang <feng.tang@intel.com>");
MODULE_DESCRIPTION("Driver for DesignWare SPI controller core");
MODULE_LICENSE("GPL v2");
