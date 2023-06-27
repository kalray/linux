// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017-2023 Kalray Inc.
 * Author(s): Clement Leger
 *
 * KVX spi-mem operations
 */

#include <linux/clk.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spi/spi-mem.h>
#include <linux/spi/spi.h>

#include "spi-dw.h"


#define DW_SPI_SPI_CTRL0		0xf4

/* Bit fields in SPI_CTRLR0 based on DWC_ssi_databook.pdf v1.01a */
#define SPI_CTRL0_ADDR_L_OFFSET		2
#define SPI_CTRL0_ADDR_L8		0x2
#define SPI_CTRL0_ADDR_L16		0x4
#define SPI_CTRL0_ADDR_L24		0x6
#define SPI_CTRL0_ADDR_L32		0x8
#define SPI_CTRL0_ADDR_L40		0xa
#define SPI_CTRL0_ADDR_L48		0xc
#define SPI_CTRL0_ADDR_L56		0xe

#define SPI_CTRL0_INST_L_OFFSET		8
#define SPI_SPI_CTRL0_INST_L8		0x2

#define SPI_CTRL0_WAIT_CYCLES_OFFSET	11
#define SPI_CTRL0_WAIT_CYCLES_MASK	0x1f

#define SPI_CTRL0_CLK_STRETCH_OFFSET	30

/* Bit fields in TXFTLR */
#define SPI_TXFTLR_TFT_OFFSET		0
#define SPI_TXFTLR_FTHR_OFFSET		16

struct dw_spi_kvx {
	struct dw_spi		dws;
	struct clk		*clk;

	/* spi-mem related */
	const struct spi_mem_op *mem_op;
	struct completion	comp;
	int			comp_status;
	unsigned int		cur_data_off;
	unsigned int		cur_xfer_size;
	bool			enhanced_xfer;
	u8			bytes_per_word;
	spinlock_t		buf_lock;
	u32			fifo_count;
};

#define to_dw_spi_kvx(_ctlr) \
	container_of(spi_controller_get_devdata(_ctlr), struct dw_spi_kvx, dws)

static void dw_spi_mem_enhanced_read_rx_fifo(struct dw_spi_kvx *dws_kvx)
{
	struct dw_spi *dws = &dws_kvx->dws;
	const struct spi_mem_op *op = dws_kvx->mem_op;
	unsigned int max_data = dw_readl(dws, DW_SPI_RXFLR);
	unsigned int remaining;
	u8 *buf = op->data.buf.in;
	u32 val;

	while (max_data--) {
		val = dw_read_io_reg(dws, DW_SPI_DR);
		if (dws_kvx->bytes_per_word == 4) {
			val = swab32(val);
			memcpy(&buf[dws_kvx->cur_data_off], &val, 4);
		} else {
			buf[dws_kvx->cur_data_off] = val;
		}

		dws_kvx->cur_data_off += dws_kvx->bytes_per_word;
	}

	if (dws_kvx->cur_data_off == dws_kvx->cur_xfer_size)
		return;

	/*
	 * Transfer is not over, we want to trigger an interrupt for the
	 * remaining bytes to come
	 */
	remaining = dws_kvx->cur_xfer_size - dws_kvx->cur_data_off;
	remaining >>= ffs(dws_kvx->bytes_per_word) - 1;
	if (remaining < dws->fifo_len)
		dw_writel(dws, DW_SPI_RXFTLR, (remaining - 1));
}

static void dw_spi_mem_enhanced_write_tx_fifo(struct dw_spi_kvx *dws_kvx)
{
	struct dw_spi *dws = &dws_kvx->dws;
	const struct spi_mem_op *op = dws_kvx->mem_op;
	unsigned int max_data = dws->fifo_len - dw_readl(dws, DW_SPI_TXFLR);
	const u8 *buf = op->data.buf.out;
	u32 val;

	while (max_data--) {
		if (dws_kvx->cur_data_off == dws_kvx->cur_xfer_size)
			break;

		if (dws_kvx->bytes_per_word == 4) {
			memcpy(&val, &buf[dws_kvx->cur_data_off], 4);
			val = swab32(val);
		} else {
			val = buf[dws_kvx->cur_data_off];
		}

		dw_write_io_reg(dws, DW_SPI_DR, val);
		dws_kvx->cur_data_off += dws_kvx->bytes_per_word;
	}
}

static void dw_spi_mem_std_read_rx_fifo(struct dw_spi_kvx *dws_kvx)
{
	struct dw_spi *dws = &dws_kvx->dws;
	const struct spi_mem_op *op = dws_kvx->mem_op;
	u16 cmd_addr_dummy_len = 1 + op->addr.nbytes + op->dummy.nbytes;
	u8 byte;
	u8 *buf = op->data.buf.in;
	int i, off = 0;

	for (i = 0; i < dws_kvx->cur_xfer_size; i++) {
		byte = dw_read_io_reg(dws, DW_SPI_DR);
		/* First bytes read are only sampled data on TX for cmd */
		if (i < cmd_addr_dummy_len)
			continue;

		buf[off] = byte;
		off++;
	}
}

static void spi_mem_finish_transfer(struct dw_spi_kvx *dws_kvx)
{
	struct dw_spi *dws = &dws_kvx->dws;

	dw_spi_mask_intr(dws, 0xff);
	dws_kvx->comp_status = 0;
	complete(&dws_kvx->comp);
}


static void dw_spi_mem_handle_irq(struct dw_spi_kvx *dws_kvx)
{
	if (!dws_kvx->enhanced_xfer) {
		/* We were expecting data, read the rx fifo */
		if (dws_kvx->mem_op->data.dir == SPI_MEM_DATA_IN)
			dw_spi_mem_std_read_rx_fifo(dws_kvx);

		spi_mem_finish_transfer(dws_kvx);
	} else {
		spin_lock(&dws_kvx->buf_lock);
		if (dws_kvx->mem_op->data.dir == SPI_MEM_DATA_IN)
			dw_spi_mem_enhanced_read_rx_fifo(dws_kvx);

		if (dws_kvx->cur_data_off == dws_kvx->cur_xfer_size) {
			spi_mem_finish_transfer(dws_kvx);
			spin_unlock(&dws_kvx->buf_lock);
			return;
		}
		if (dws_kvx->mem_op->data.dir == SPI_MEM_DATA_OUT)
			dw_spi_mem_enhanced_write_tx_fifo(dws_kvx);

		spin_unlock(&dws_kvx->buf_lock);
	}
}

static irqreturn_t dw_spi_mem_irq(struct dw_spi *dws)
{
	struct dw_spi_kvx *dws_kvx = container_of(dws,
						  struct dw_spi_kvx, dws);
	u16 irq_status = dw_readl(dws, DW_SPI_ISR);

	if (dw_spi_check_status(dws, false)) {
		complete(&dws_kvx->comp);
		return IRQ_HANDLED;
	}

	if (irq_status & DW_SPI_INT_RXFI) {
		/* This is a spurious IRQ, should not happen */
		if (dws_kvx->mem_op->data.dir != SPI_MEM_DATA_IN) {
			pr_err("Unexpected RX full irq\n");
			return IRQ_HANDLED;
		}
		dw_spi_mem_handle_irq(dws_kvx);
	}

	if (irq_status & DW_SPI_INT_TXEI) {
		/* This is a spurious IRQ, should not happen */
		if (dws_kvx->mem_op->data.dir == SPI_MEM_DATA_IN) {
			pr_err("Unexpected TX empty irq\n");
			return IRQ_HANDLED;
		}
		dw_spi_mem_handle_irq(dws_kvx);
	}

	return IRQ_HANDLED;
}

static void dw_spi_mem_reset_xfer(struct dw_spi_kvx *dws_kvx,
				  const struct spi_mem_op *op)
{
	struct dw_spi *dws = &dws_kvx->dws;

	dws->transfer_handler = dw_spi_mem_irq;

	reinit_completion(&dws_kvx->comp);
	dws_kvx->mem_op = op;
	dws_kvx->cur_data_off = 0;
	dws_kvx->comp_status = -EIO;
	dws_kvx->enhanced_xfer = 0;
	dws_kvx->bytes_per_word = 1;
}

static void dw_spi_mem_start_std_op(struct dw_spi_kvx *dws_kvx,
				    const struct spi_mem_op *op)
{
	struct dw_spi *dws = &dws_kvx->dws;
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
	dws_kvx->cur_xfer_size = xfer_size;

	dw_spi_enable_chip(dws, 0);
	dw_spi_mask_intr(dws, 0xff);

	/* When reading, we only care about the receive fifo being empty */
	if (op->data.dir == SPI_MEM_DATA_IN) {
		/* Set RX fifo level to trigger a rx fifo full interrupt */
		dw_writel(dws, DW_SPI_RXFTLR, (xfer_size - 1));

		dw_spi_umask_intr(dws, DW_SPI_INT_RXFI);
	} else {
		/* We will refill the tx on tx empty fifo interrupt */
		dw_spi_umask_intr(dws, DW_SPI_INT_TXEI);
	}

	/* Set TXFTL start fifo level*/
	dw_writel(dws, DW_SPI_TXFTLR, (xfer_size - 1) << SPI_TXFTLR_FTHR_OFFSET);

	dw_spi_enable_chip(dws, 1);

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

static void dw_spi_mem_exec_std(struct dw_spi_kvx *dws_kvx,
				struct spi_device *spi,
				const struct spi_mem_op *op)
{
	struct dw_spi *dws = &dws_kvx->dws;
	struct dw_spi_cfg cfg = {0};

	dw_spi_mem_reset_xfer(dws_kvx, op);

	if (op->data.dir == SPI_MEM_DATA_IN)
		cfg.tmode = DW_SPI_CTRLR0_TMOD_TR;
	else
		cfg.tmode = DW_SPI_CTRLR0_TMOD_TO;

	cfg.spi_frf = SPI_SPI_FRF_STANDARD;
	cfg.freq = spi->max_speed_hz;

	dws_kvx->bytes_per_word = 1;
	cfg.dfs = dws_kvx->bytes_per_word * 8;

	dw_spi_enable_chip(dws, 0);
	dw_spi_update_config(dws, spi, &cfg);
	dw_spi_enable_chip(dws, 1);

	dw_spi_mem_start_std_op(dws_kvx, op);
}

static void dw_spi_mem_setup_enhanced_xfer(struct dw_spi_kvx *dws_kvx,
				      struct spi_device *spi,
				      const struct spi_mem_op *op)
{
	struct dw_spi *dws = &dws_kvx->dws;
	struct dw_spi_cfg cfg = {0};
	unsigned long fifo_count = op->data.nbytes;
	u32 spi_ctrl0 = 0;
	u32 wait_cycles = 0;
	u8 addr_l = 0;

	dw_spi_mem_reset_xfer(dws_kvx, op);
	dws_kvx->enhanced_xfer = 1;

	if (op->data.dir == SPI_MEM_DATA_IN)
		cfg.tmode = DW_SPI_CTRLR0_TMOD_RO;
	else
		cfg.tmode = DW_SPI_CTRLR0_TMOD_TO;

	if (op->data.buswidth == 8)
		cfg.spi_frf = SPI_SPI_FRF_OCTAL;
	else if (op->data.buswidth == 4)
		cfg.spi_frf = SPI_SPI_FRF_QUAD;
	else if (op->data.buswidth == 2)
		cfg.spi_frf = SPI_SPI_FRF_DUAL;

	if (IS_ALIGNED(op->data.nbytes, 4))
		dws_kvx->bytes_per_word = 4;
	else
		dws_kvx->bytes_per_word = 1;

	fifo_count >>= ffs(dws_kvx->bytes_per_word) - 1;
	dws_kvx->fifo_count = fifo_count;

	cfg.dfs = dws_kvx->bytes_per_word * 8;
	cfg.freq = spi->max_speed_hz;
	cfg.ndf = dws_kvx->fifo_count;

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

	dw_spi_enable_chip(dws, 0);

	dw_spi_update_config(dws, spi, &cfg);

	dw_writel(dws, DW_SPI_SPI_CTRL0, spi_ctrl0);

	dw_spi_enable_chip(dws, 1);
}

static void dw_spi_mem_start_enhanced_op(struct dw_spi_kvx *dws_kvx,
					const struct spi_mem_op *op)
{
	struct dw_spi *dws = &dws_kvx->dws;
	unsigned int thres;
	unsigned long flags;

	dws_kvx->cur_xfer_size = op->data.nbytes;
	dw_spi_enable_chip(dws, 0);
	dw_spi_mask_intr(dws, 0xff);

	if (op->data.dir == SPI_MEM_DATA_IN) {
		thres = min_t(unsigned long, dws->fifo_len,
			      dws_kvx->fifo_count);
		if (thres < 1)
			thres = 1;
		dw_writel(dws, DW_SPI_RXFTLR, thres - 1);
		dw_writel(dws, DW_SPI_TXFTLR, 0);

		dw_spi_umask_intr(dws, DW_SPI_INT_RXFI);
	} else {
		/*
		 * Since we send a command + opcode, we need to set the start
		 * threshold to at least 2
		 */
		thres = 2;
		dw_writel(dws, DW_SPI_TXFTLR, thres << SPI_TXFTLR_FTHR_OFFSET);
	}

	dw_spi_enable_chip(dws, 1);

	spin_lock_irqsave(&dws_kvx->buf_lock, flags);
	dw_write_io_reg(dws, DW_SPI_DR, op->cmd.opcode);
	dw_write_io_reg(dws, DW_SPI_DR, op->addr.val);

	if (op->data.dir == SPI_MEM_DATA_OUT) {
		dw_spi_mem_enhanced_write_tx_fifo(dws_kvx);
		/* Unmask tx empty interrupt after data have been pushed in the fifo */
		dw_spi_umask_intr(dws, DW_SPI_INT_TXEI);
	}

	spin_unlock_irqrestore(&dws_kvx->buf_lock, flags);
}

static void dw_spi_mem_exec_enhanced(struct dw_spi_kvx *dws_kvx,
				     struct spi_device *spi,
				     const struct spi_mem_op *op)
{
	dw_spi_mem_setup_enhanced_xfer(dws_kvx, spi, op);
	dw_spi_mem_start_enhanced_op(dws_kvx, op);
}

static bool dw_spi_mem_is_enhanced(const struct spi_mem_op *op)
{
	/*
	 * From a controller POV, an enhanced transfer is using more than 1 wire
	 * of data
	 */
	return op->data.buswidth > 1;
}

static int dw_spi_enhanced_exec_mem_op(struct spi_mem *mem,
				       const struct spi_mem_op *op)
{
	struct dw_spi_kvx *dws_kvx = to_dw_spi_kvx(mem->spi->master);
	struct dw_spi *dws = &dws_kvx->dws;
	unsigned long timeout;
	u32 sr;

	/* Select the slave, it will only be asserted when clock starts */
	dw_spi_set_cs(mem->spi, false);

	if (dw_spi_mem_is_enhanced(op))
		dw_spi_mem_exec_enhanced(dws_kvx, mem->spi, op);
	else
		dw_spi_mem_exec_std(dws_kvx, mem->spi, op);

	timeout = wait_for_completion_timeout(&dws_kvx->comp, HZ);
	if (!timeout) {
		dev_err(&dws->master->dev, "completion timeout");
		goto err_io;
	}

	if (dws_kvx->comp_status != 0) {
		dev_err(&dws->master->dev, "completion error");
		goto err_io;
	}

	/* Wait for TFE bit to go up */
	timeout = readl_poll_timeout(dws->regs + DW_SPI_SR, sr,
				     sr & DW_SPI_SR_TF_EMPT, 0, USEC_PER_SEC);
	if (timeout) {
		dev_err(&dws->master->dev, "wait for transmit fifo empty failed\n");
		goto err_io;
	}

	/* Wait for BUSY bit to go down */
	timeout = readl_poll_timeout(dws->regs + DW_SPI_SR, sr,
				     !(sr & DW_SPI_SR_BUSY), 0, USEC_PER_SEC);
	if (timeout) {
		dev_err(&dws->master->dev, "wait for end of busy failed\n");
		goto err_io;
	}

	dw_spi_set_cs(mem->spi, true);

	return 0;

err_io:
	dw_spi_reset_chip(dws);
	return -EIO;

}

static bool dw_spi_enhanced_supports_mem_op(struct spi_mem *mem,
				   const struct spi_mem_op *op)
{
	const int max_wait_cyle = SPI_CTRL0_WAIT_CYCLES_MASK;

	if (op->addr.nbytes > 4)
		return false;

	/* We only support 1-1-X commands */
	if (op->cmd.buswidth > 1 || op->addr.buswidth > 1)
		return false;

	/* Check maximum number of wait cycles */
	if (op->dummy.nbytes &&
	    (op->dummy.nbytes * 8 / op->dummy.buswidth > max_wait_cyle))
		return false;

	return spi_mem_default_supports_op(mem, op);
}

static int dw_spi_enhanced_adjust_mem_op_size(struct spi_mem *mem,
					      struct spi_mem_op *op)
{
	unsigned int max_size;
	struct dw_spi_kvx *dws_kvx = to_dw_spi_kvx(mem->spi->master);
	struct dw_spi *dws = &dws_kvx->dws;

	if (dw_spi_mem_is_enhanced(op)) {
		/*
		 * Reduce to maximum NDF * 4 in enhanced_xfer mode since we will
		 * read bytes 4 by 4.
		 */
		op->data.nbytes = min_t(unsigned int, op->data.nbytes,
					DW_SPI_NDF_MASK * 4);

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

static int dw_spi_kvx_probe(struct platform_device *pdev)
{
	struct dw_spi_kvx *dws_kvx;
	struct resource *mem;
	struct dw_spi *dws;
	int ret;

	dws_kvx = devm_kzalloc(&pdev->dev, sizeof(struct dw_spi_kvx),
			GFP_KERNEL);
	if (!dws_kvx)
		return -ENOMEM;

	dws = &dws_kvx->dws;

	/* Get basic io resource and map it */
	dws->regs = devm_platform_get_and_ioremap_resource(pdev, 0, &mem);
	if (IS_ERR(dws->regs))
		return PTR_ERR(dws->regs);

	dws->paddr = mem->start;

	dws->irq = platform_get_irq(pdev, 0);
	if (dws->irq < 0)
		return dws->irq; /* -ENXIO */

	dws_kvx->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dws_kvx->clk))
		return PTR_ERR(dws_kvx->clk);

	dws->bus_num = pdev->id;
	init_completion(&dws_kvx->comp);
	spin_lock_init(&dws_kvx->buf_lock);
	dws->mode_bits |= SPI_RX_DUAL | SPI_TX_DUAL |
			  SPI_RX_QUAD | SPI_TX_QUAD |
			  SPI_RX_OCTAL | SPI_TX_OCTAL;

	dws->max_freq = clk_get_rate(dws_kvx->clk);

	/* On AHB-SSI, the registers are always 32 bits wide */
	dws->reg_io_width = 4;
	dws->caps = DW_SPI_CAP_DWC_SSI | DW_SPI_CAP_ENHANCED;

	dw_spi_dma_setup_generic(dws);

	dws->num_cs = 4;

	dws->mem_ops.adjust_op_size = dw_spi_enhanced_adjust_mem_op_size;
	dws->mem_ops.supports_op = dw_spi_enhanced_supports_mem_op;
	dws->mem_ops.exec_op = dw_spi_enhanced_exec_mem_op;

	ret = dw_spi_add_host(&pdev->dev, dws);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, dws_kvx);
	return 0;
}

static int dw_spi_kvx_remove(struct platform_device *pdev)
{
	struct dw_spi_kvx *dws_kvx = platform_get_drvdata(pdev);

	dw_spi_remove_host(&dws_kvx->dws);

	return 0;
}

static const struct of_device_id dw_spi_kvx_of_match[] = {
	{ .compatible = "snps,dw-ahb-ssi"},
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, dw_spi_kvx_of_match);

static struct platform_driver dw_spi_kvx_driver = {
	.probe		= dw_spi_kvx_probe,
	.remove		= dw_spi_kvx_remove,
	.driver		= {
		.name	= "kvx-dw-ssi",
		.of_match_table = dw_spi_kvx_of_match,
	}
};
module_platform_driver(dw_spi_kvx_driver);

MODULE_AUTHOR("Clement Leger <clement.leger@kalray.eu>");
MODULE_DESCRIPTION("kalray KVX SSI Controller driver");
MODULE_LICENSE("GPL v2");
