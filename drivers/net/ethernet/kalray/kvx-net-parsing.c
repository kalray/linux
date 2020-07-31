// SPDX-License-Identifier: GPL-2.0
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/iopoll.h>

#include "kvx-net.h"
#include "kvx-net-hw.h"

#define HASH_SEED               (0xFFFU)
#define PARSER_DEFAULT_PRIORITY (0)  /* 0: lowest, 7: max */
#define PARSER_RR_PKT_NB        (10)

/* READ_DELAY < ~10us */
#define READ_DELAY         (10)
#define READ_TIMEOUT       (5000)

static int parser_check(unsigned int parser_id, unsigned int word_index)
{
	if (parser_id > KVX_ETH_PARSER_NB)
		return -EINVAL;

	if ((word_index & 0xf) >= PARSER_RAM_WORD_NB - 1) {
		pr_err("word_index: %d\n", word_index);
		return -EINVAL;
	}

	return 0;
}

#define RAM(p)      (PARSER_RAM_OFFSET + PARSER_RAM_ELEM_SIZE * (p))
#define RAM_LINE(l) (PARSER_RAM_LINE + (l) * PARSER_RAM_LINE_ELEM_SIZE)

void parser_disp(struct kvx_eth_hw *hw, unsigned int parser_id)
{
	u32 off = PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * parser_id;

	dev_dbg(hw->dev, "Parser[%d]\n", parser_id);
	DUMP_REG(hw, ETH, off + PARSER_CTRL_CTL);
	DUMP_REG(hw, ETH, off + PARSER_CTRL_STATUS);
	DUMP_REG(hw, ETH, off + PARSER_CTRL_HIT_CNT);
}

/**
 * parser_commit_filter() - Enables filtering for parser_id
 *  Checks parser alignement and RAM address.
 *  Writes End of rule filter into a parser RAM.
 *  Enables filter on success.
 *
 * Return: next RAM write index on success, negative on failure
 */
static int parser_commit_filter(struct kvx_eth_hw *hw,
				struct kvx_eth_lane_cfg *cfg,
				unsigned int parser_id, unsigned int word_index,
				enum parser_dispatch_policy policy, int prio)
{
	int i = word_index;
	u32 off = RAM(parser_id) + RAM_LINE(0);
	u32 val = 0;
	int ret = 0;

	ret = parser_check(parser_id, word_index);
	if (ret < 0) {
		dev_dbg(hw->dev, "Lane[%d] parser check failed\n", cfg->id);
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
	u32 off = RAM(parser_id) + RAM_LINE(0);

	kvx_eth_writel(hw, *(u32 *)&desc->word[0],
		       off + PARSER_RAM_WORD_SIZE * i);
	++i;
	for (j = 0; j < PARSER_RAM_WORD_NB - 1; ++j)
		kvx_eth_writel(hw, 0, off + PARSER_RAM_WORD_SIZE * (i + j));

	/* Considers desc->skip_length == 3 to start next rule on next line) */
	i += PARSER_RAM_WORD_NB - 1;
	return i;
}

/**
 * write_ramline - writes array of u32 to RAM
 * Always starts at the beginning of RAM line
 *
 * Return: next RAM write index on success, negative on failure
 */
static int write_ramline(struct kvx_eth_hw *hw, unsigned int parser_id,
			  unsigned int idx, u32 *data, size_t s)
{
	int j, i = idx;
	int size;
	u32 off = RAM(parser_id) + RAM_LINE(0);

	size = s / PARSER_RAM_WORD_SIZE;
	dev_dbg(hw->dev, "idx: %d array size: %d s: %d\n", idx, size, (int)s);
	for (j = 0; j < size; ++j) {
		kvx_eth_writel(hw, data[j], off + PARSER_RAM_WORD_SIZE * i);
		++i;
	}
	/* Fill in the rest of the line */
	for (; j < PARSER_RAM_WORD_NB; ++j) {
		kvx_eth_writel(hw, 0, off + PARSER_RAM_WORD_SIZE * i);
		++i;
	}

	i += 3;
	return i;
}

/**
 * parser_add_ipv6_filter() - add IPV6 rule
 * Always starts at the beginning of RAM line
 * Do not take into account skip_length field!
 *
 * Return: next RAM write index on success, negative on failure
 */
static int parser_add_ipv6_filter(struct kvx_eth_hw *hw, unsigned int parser_id,
			  unsigned int idx, struct ipv6_filter_desc *desc)
{
	int i = idx;

	i = write_ramline(hw, parser_id, i, (u32 *)desc->d0.word,
				sizeof(desc->d0));
	i = write_ramline(hw, parser_id, i, (u32 *)desc->d1.word,
				sizeof(desc->d1));
	i = write_ramline(hw, parser_id, i, (u32 *)desc->d2.word,
				sizeof(desc->d2));

	return i;
}

/**
 * parser_add_filter() - Adds a new rule to parser_id
 * @hw: HW description
 * @parser_id: id of selected parser
 * @idx: current word index for parser RAM
 * @desc: rule description
 * @total_add_index: accumulation of rules setting an index in the selected
 *                   parser
 * @total_check_checksum: accumulation of rules setting a checksum offload in
 *                        the selected parser
 *
 * Return: next RAM write index on success, negative on failure
 */
static int parser_add_filter(struct kvx_eth_hw *hw, unsigned int parser_id,
		      unsigned int idx, union filter_desc *desc,
		      u32 *total_add_index, u32 *total_check_checksum)
{
	int ret = -EINVAL;

	ret = parser_check(parser_id, idx);
	if (ret < 0) {
		dev_err(hw->dev, "Parser[%d] check failed\n", parser_id);
		return ret;
	}

	switch ((*(u32 *)desc) & PTYPE_MASK) {
	case PTYPE_MAC_VLAN:
		*total_add_index += desc->mac_vlan.add_metadata_index;
		return write_ramline(hw, parser_id, idx,
				     &desc->mac_vlan.word[0],
				     sizeof(desc->mac_vlan));
	case PTYPE_VXLAN:
		*total_add_index += desc->vxlan.add_metadata_index;
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->vxlan.word[0],
				     sizeof(desc->vxlan));
	case PTYPE_IP_V4:
		*total_add_index += desc->ipv4.add_metadata_index;
		*total_check_checksum += desc->ipv4.check_header_checksum;
		return write_ramline(hw, parser_id, idx,
				     &desc->ipv4.word[0],
				     sizeof(desc->ipv4));
	case PTYPE_IP_V6:
		*total_add_index += desc->ipv6.d0.add_metadata_index;
		return parser_add_ipv6_filter(hw, parser_id,
					      idx, &desc->ipv6);
	case PTYPE_UDP:
		*total_add_index += desc->udp.add_metadata_index;
		*total_check_checksum += desc->udp.check_header_checksum;
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->udp.word[0],
				     sizeof(desc->udp));
	case PTYPE_TCP:
		*total_add_index += desc->tcp.add_metadata_index;
		*total_check_checksum += desc->tcp.check_header_checksum;
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->tcp.word[0],
				     sizeof(desc->tcp));
	case PTYPE_CUSTOM:
		*total_add_index += desc->custom.add_metadata_index;
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->custom.word[0],
				     sizeof(desc->custom));
	case PTYPE_ROCE:
		*total_add_index += desc->roce.add_metadata_index;
		*total_check_checksum += desc->roce.check_icrc;
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->roce.word[0],
				     sizeof(desc->roce));
	case PTYPE_MPLS:
		*total_add_index += desc->mpls.add_metadata_index;
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->mpls.word[0],
				     sizeof(desc->mpls));
	case PTYPE_SKIP:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->skip.word[0],
				     sizeof(desc->skip));
	default:
		break;
	}
	return ret;
}

/** parser_disable() - Disable parser parser_id
 * Context: can not be called in interrupt context (readq_poll_timeout)
 *
 * Return: 0 on success, negative on failure
 */
int parser_disable(struct kvx_eth_hw *hw, int parser_id)
{
	u32 off = PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * parser_id;
	u32 val = (u32)PARSER_DISABLED << PARSER_CTRL_DISPATCH_POLICY_SHIFT;
	int ret = 0;

	kvx_eth_writel(hw, val, off + PARSER_CTRL_CTL);
	ret = readl_poll_timeout(hw->res[KVX_ETH_RES_ETH].base + off +
				 PARSER_CTRL_STATUS, val, !val,
				 READ_DELAY, READ_TIMEOUT);
	if (unlikely(ret)) {
		dev_err(hw->dev, "Disable parser[%d] timeout\n", parser_id);
		return ret;
	}

	return 0;
}

/**
 * eth_config_parser() - Configure n rules for parser parser_id
 * Context: can not be called in interrupt context (readq_poll_timeout)
 *
 * Return: 0 on success, negative on failure
 */
int parser_config(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg,
		  int parser_id, enum parser_dispatch_policy policy, int prio)
{
	union filter_desc **rules = hw->parsing.parsers[parser_id].filters;
	int rules_len =  hw->parsing.parsers[parser_id].nb_layers;
	int ret, rule, word_index = 0;
	union filter_desc *filter_desc;
	u32 total_add_index = 0;
	u32 total_check_checksum = 0;

	ret = parser_disable(hw, parser_id);
	if (ret)
		return ret;

	for (rule = 0; rule < rules_len; ++rule) {
		filter_desc = rules[rule];
		word_index = parser_add_filter(hw, parser_id,
				word_index, filter_desc,
				&total_add_index, &total_check_checksum);
	}
	if (word_index < 0) {
		dev_err(hw->dev, "Failed to add filter[%d] to parser[%d] (ret: %d)\n",
				rule, parser_id, word_index);
		return -EINVAL;
	}
	/* Rx metadata only has 4 indexes and 4 CRC error flags */
	if (total_add_index > 4 || total_check_checksum > 4) {
		dev_err(hw->dev, "Failed to add filter[%d] to parser[%d] (ret: %d)\n",
				rule, parser_id, word_index);
		return -EINVAL;
	}

	word_index = parser_commit_filter(hw, cfg, parser_id, word_index,
					  policy, prio);
	if (word_index < 0) {
		dev_err(hw->dev, "Failed to commit filters to parser[%d]\n",
			parser_id);
		return -EBUSY;
	}

	return 0;
}
