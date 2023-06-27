// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017-2023 Kalray Inc.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "../kvx-net.h"
#include "../kvx-net-hw.h"

#define RAM_CV1(p)      (PARSER_RAM_OFFSET + PARSER_RAM_ELEM_SIZE * (p))
#define RAM_LINE_CV1(l) (PARSER_RAM_LINE + (l) * PARSER_RAM_LINE_ELEM_SIZE)

#define PARSER_RR_PKT_NB        (10)
#define HASH_SEED               (0xFFFU)

/**
 * parser_commit_filter_cv1() - Enables filtering for parser_id
 *  Checks parser alignement and RAM address.
 *  Writes End of rule filter into a parser RAM.
 *  Enables filter on success.
 *
 * Return: next RAM write index on success, negative on failure
 */
int parser_commit_filter_cv1(struct kvx_eth_hw *hw,
				struct kvx_eth_lane_cfg *cfg,
				unsigned int parser_id, unsigned int word_index,
				enum parser_dispatch_policy policy, int prio)
{
	int i = word_index;
	u32 off = RAM_CV1(parser_id) + RAM_LINE_CV1(0);
	u32 val = 0;
	int ret = 0;

	ret = parser_check(parser_id, word_index, COOLIDGE_V1);
	if (ret < 0) {
		dev_err(hw->dev, "Lane[%d] parser check failed\n", cfg->id);
		return ret;
	}
	kvx_eth_writel(hw, PTYPE_END_OF_RULE, off + i * PARSER_RAM_WORD_SIZE);
	i++;
	off = PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * parser_id;
	val = ((u32)policy << PARSER_CTRL_DISPATCH_POLICY_SHIFT) |
		((u32)cfg->id << PARSER_CTRL_LANE_SRC_SHIFT) |
		((u32)prio << PARSER_CTRL_PRIO_SHIFT) |
		((u32)PARSER_RR_PKT_NB << PARSER_CTRL_RR_PKT_NB_SHIFT) |
		((u32)HASH_SEED << PARSER_CTRL_HASH_SEED_SHIFT);
	kvx_eth_writel(hw, val, off + PARSER_CTRL_CTL);

	return i;
}

void write_parser_ram_word_cv1(struct kvx_eth_hw *hw, u32 data, unsigned int parser_id,
			  unsigned int word_idx)
{
	kvx_eth_writel(hw, data, RAM_LINE_CV1(0) + RAM_CV1(parser_id) +
		word_idx * PARSER_RAM_WORD_SIZE);
}

/** parser_disable_cv1() - Disable parser parser_id
 * Context: can not be called in interrupt context (readq_poll_timeout)
 *
 * Return: 0 on success, negative on failure
 */
int parser_disable_cv1(struct kvx_eth_hw *hw, int parser_id)
{
	u32 off = PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * parser_id;
	u32 val = (u32)PARSER_DISABLED << PARSER_CTRL_DISPATCH_POLICY_SHIFT;
	int ret = 0;

	dev_dbg(hw->dev, "Disable parser[%d]\n", parser_id);
	kvx_eth_writel(hw, val, off + PARSER_CTRL_CTL);
	ret = readl_poll_timeout(hw->res[KVX_ETH_RES_ETH].base + off +
				 PARSER_CTRL_STATUS, val, !val,
				 PARSER_STATUS_RD_DELAY, PARSER_STATUS_RD_TOUT);
	if (unlikely(ret)) {
		dev_err(hw->dev, "Disable parser[%d] timeout\n", parser_id);
		return ret;
	}

	/* Reset hit_cnt */
	kvx_eth_readl(hw, off + PARSER_CTRL_HIT_CNT + 4);
	clear_parser_f(hw, parser_id);
	return 0;
}

/**
 * parser_add_skip_filter() - add skip rule
 * Always starts at the beginning of RAM line
 *
 * Return: next RAM write index on success, negative on failure
 */
int parser_add_skip_filter(struct kvx_eth_hw *hw, unsigned int parser_id,
			   unsigned int idx, union skip_filter_desc *desc)
{
	int j, i = idx;
	u32 off = RAM_CV1(parser_id) + RAM_LINE_CV1(0);

	kvx_eth_writel(hw, *(u32 *)&desc->word[0],
		       off + PARSER_RAM_WORD_SIZE * i);
	++i;
	for (j = 0; j < PARSER_RAM_WORD_NB - 1; ++j)
		kvx_eth_writel(hw, 0, off + PARSER_RAM_WORD_SIZE * (i + j));
	/* Considers desc->skip_length == 3 to start next rule on next line) */
	i += PARSER_RAM_WORD_NB - 1;
	return i;
}
