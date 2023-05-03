// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2023 Kalray Inc.
 */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "../kvx-net.h"
#include "../kvx-net-hw.h"
#include "kvx-ethrx-regs-cv2.h"

#define RAM_CV2(p)      (KVX_ETH_LBA_PARSER_RAM_GRP_OFFSET + KVX_ETH_LBA_PARSER_RAM_GRP_ELEM_SIZE * (p))
#define RAM_LINE_CV2(l) (KVX_ETH_LBA_PARSER_RAM_LB_PARSER_RAM_LINE_GRP_OFFSET + (l) * KVX_ETH_LBA_PARSER_RAM_LB_PARSER_RAM_LINE_GRP_ELEM_SIZE)

/**
 * parser_commit_filter_cv2() - Enables filtering for parser_id
 *  Checks parser alignement and RAM address.
 *  Writes End of rule filter into a parser RAM.
 *  Enables filter on success.
 *
 * Return: next RAM write index on success, negative on failure
 */
int parser_commit_filter_cv2(struct kvx_eth_hw *hw,
				struct kvx_eth_lane_cfg *cfg,
				unsigned int parser_id, unsigned int word_index,
				enum parser_dispatch_policy policy, int prio)
{
	int i = word_index;
	u32 off = RAM_CV2(parser_id) + RAM_LINE_CV2(0);
	u32 val = 0;
	int ret = 0;
	struct kvx_eth_parser_cv2_f *parser = &hw->parser_cv2_f[parser_id];

	ret = parser_check(parser_id, word_index, COOLIDGE_V2);
	if (ret < 0) {
		dev_err(hw->dev, "Lane[%d] parser check failed\n", cfg->id);
		return ret;
	}
	kvx_lbana_writel(hw, PTYPE_END_OF_RULE, off + i * PARSER_RAM_WORD_SIZE);
	i++;
	off = KVX_ETH_LBA_PARSER_GRP_OFFSET + KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * parser_id;
	if (policy == PARSER_DROP) {
		parser->disp_info = DISPATCH_INFO_DROP;
		val = ((u32) parser->disp_info) << KVX_ETH_LBA_PARSER_DISPATCH_INFO_DROP_SHIFT;
		kvx_lbana_writel(hw, val, off + KVX_ETH_LBA_PARSER_DISPATCH_INFO_OFFSET);
		parser->disp_policy = (u32) POLICY_PARSER;
	} else {
		parser->disp_policy = (u32) POLICY_USE_RSS;
	}
	kvx_lbana_writel(hw, parser->disp_policy, off + KVX_ETH_LBA_PARSER_DISPATCH_POLICY_OFFSET);
	parser->ctrl = (KVX_ETH_RX_LBA_PARSER_CTRL_ENABLE |
			(1 << (KVX_ETH_LBA_PARSER_CTRL_LANE_SRC_SHIFT + (u32)cfg->id)) |
			((u32)prio << KVX_ETH_LBA_PARSER_CTRL_PRIORITY_SHIFT));
	kvx_lbana_writel(hw, parser->ctrl, off + KVX_ETH_LBA_PARSER_CTRL_OFFSET);
	kvx_eth_lb_rss_rfs_enable(hw);
	return i;
}

void write_parser_ram_word_cv2(struct kvx_eth_hw *hw, u32 data, unsigned int parser_id,
			  unsigned int word_idx)
{
	kvx_lbana_writel(hw, data, RAM_LINE_CV2(0) + RAM_CV2(parser_id) +
		word_idx * PARSER_RAM_WORD_SIZE);
}

/** parser_disable_cv2() - Disable parser parser_id
 * Context: can not be called in interrupt context (readq_poll_timeout)
 *
 * Return: 0 on success, negative on failure
 */
int parser_disable_cv2(struct kvx_eth_hw *hw, int parser_id)
{
	u32 val = 0;
	u32 off = KVX_ETH_LBA_PARSER_GRP_OFFSET + KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * parser_id;
	struct kvx_eth_parser_cv2_f *parser = &hw->parser_cv2_f[parser_id];
	int ret = 0;

	dev_dbg(hw->dev, "Disable parser[%d]\n", parser_id);
	parser->ctrl = KVX_ETH_RX_LBA_PARSER_CTRL_DISABLE;
	kvx_lbana_writel(hw, parser->ctrl, off + KVX_ETH_LBA_PARSER_CTRL_OFFSET);
	ret = readl_poll_timeout(hw->res[KVX_ETH_RES_ETH_RX_LB_ANA].base + off +
				 KVX_ETH_LBA_PARSER_STATUS_OFFSET, val,
				 (val == KVX_ETH_RX_LBA_PARSER_STATUS_STOPPED), PARSER_STATUS_RD_DELAY, PARSER_STATUS_RD_TOUT);
	if (unlikely(ret)) {
		dev_err(hw->dev, "Disable parser[%d] timeout\n", parser_id);
		return ret;
	}

	/* Reset hit_cnt */
	kvx_lbana_readl(hw, off + KVX_ETH_LBA_PARSER_HIT_CNT_LAC_OFFSET);
	clear_parser_f(hw, parser_id);
	return 0;
}
