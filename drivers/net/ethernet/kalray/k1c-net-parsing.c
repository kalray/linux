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

#include "k1c-net.h"
#include "k1c-net-hw.h"

#define HASH_SEED               (0xFFFU)
#define PARSER_DEFAULT_PRIORITY (0)  /* 0: lowest, 7: max */
#define PARSER_RR_PKT_NB        (10)

/* READ_DELAY < ~10us */
#define READ_DELAY         (10)
#define READ_TIMEOUT       (5000)

static int parser_check(unsigned int parser_id, unsigned int word_index)
{
	if (parser_id > K1C_ETH_PARSER_NB)
		return -EINVAL;

	if ((word_index & 0xf) >= PARSER_RAM_WORD_NB - 1) {
		pr_err("word_index: %d\n", word_index);
		return -EINVAL;
	}

	return 0;
}

#define RAM(p)      (PARSER_RAM_OFFSET + PARSER_RAM_ELEM_SIZE * (p))
#define RAM_LINE(l) (PARSER_RAM_LINE + (l) * PARSER_RAM_LINE_ELEM_SIZE)

void parser_disp(struct k1c_eth_hw *hw, unsigned int parser_id)
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
static int parser_commit_filter(struct k1c_eth_hw *hw,
				struct k1c_eth_lane_cfg *cfg,
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

	k1c_eth_writel(hw, PTYPE_END_OF_RULE, off + i * PARSER_RAM_WORD_SIZE);
	i++;

	off = PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * parser_id;
	val |= K1C_ETH_SETF(policy, PARSER_CTRL_DISPATCH_POLICY);
	val |= K1C_ETH_SETF(cfg->id, PARSER_CTRL_LANE_SRC);
	val |= K1C_ETH_SETF(prio, PARSER_CTRL_PRIO);
	val |= K1C_ETH_SETF(PARSER_RR_PKT_NB, PARSER_CTRL_RR_PKT_NB);
	val |= K1C_ETH_SETF(HASH_SEED, PARSER_CTRL_HASH_SEED);
	k1c_eth_writel(hw, val, off + PARSER_CTRL_CTL);

	return i;
}

/**
 * parser_add_skip_filter() - add skip rule
 * Always starts at the beginning of RAM line
 *
 * Return: next RAM write index on success, negative on failure
 */
int parser_add_skip_filter(struct k1c_eth_hw *hw, unsigned int parser_id,
			   unsigned int idx, union skip_filter_desc *desc)
{
	int j, i = idx;
	u32 off = RAM(parser_id) + RAM_LINE(0);

	k1c_eth_writel(hw, *(u32 *)&desc->word[0],
		       off + PARSER_RAM_WORD_SIZE * i);
	++i;
	for (j = 0; j < PARSER_RAM_WORD_NB - 1; ++j)
		k1c_eth_writel(hw, 0, off + PARSER_RAM_WORD_SIZE * (i + j));

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
static int write_ramline(struct k1c_eth_hw *hw, unsigned int parser_id,
			  unsigned int idx, u32 *data, size_t s)
{
	int j, i = idx;
	int size;
	u32 off = RAM(parser_id) + RAM_LINE(0);

	size = s / PARSER_RAM_WORD_SIZE;
	dev_dbg(hw->dev, "idx: %d array size: %d s: %d\n", idx, size, (int)s);
	for (j = 0; j < size; ++j) {
		k1c_eth_writel(hw, data[j], off + PARSER_RAM_WORD_SIZE * i);
		++i;
	}
	/* Fill in the rest of the line */
	for (; j < PARSER_RAM_WORD_NB; ++j) {
		k1c_eth_writel(hw, 0, off + PARSER_RAM_WORD_SIZE * i);
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
static int parser_add_ipv6_filter(struct k1c_eth_hw *hw, unsigned int parser_id,
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
 *
 * Return: next RAM write index on success, negative on failure
 */
static int parser_add_filter(struct k1c_eth_hw *hw, unsigned int parser_id,
		      unsigned int idx, union filter_desc *desc)
{
	int ret = -EINVAL;

	ret = parser_check(parser_id, idx);
	if (ret < 0) {
		dev_err(hw->dev, "Parser[%d] check failed\n", parser_id);
		return ret;
	}

	switch ((*(u32 *)desc) & PTYPE_MASK) {
	case PTYPE_MAC_VLAN:
		return write_ramline(hw, parser_id, idx,
				     &desc->mac_vlan.word[0],
				     sizeof(desc->mac_vlan));
	case PTYPE_VXLAN:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->vxlan.word[0],
				     sizeof(desc->vxlan));
	case PTYPE_IP_V4:
		return write_ramline(hw, parser_id, idx,
				     &desc->ipv4.word[0],
				     sizeof(desc->ipv4));
	case PTYPE_IP_V6:
		return parser_add_ipv6_filter(hw, parser_id,
					      idx, &desc->ipv6);
	case PTYPE_UDP:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->udp.word[0],
				     sizeof(desc->udp));
	case PTYPE_TCP:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->tcp.word[0],
				     sizeof(desc->tcp));
	case PTYPE_CUSTOM:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->custom.word[0],
				     sizeof(desc->custom));
	case PTYPE_NVME_TCP:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->nvme_tcp.word[0],
				     sizeof(desc->nvme_tcp));
	case PTYPE_ROCE:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->roce.word[0],
				     sizeof(desc->roce));
	case PTYPE_MPLS:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->mpls.word[0],
				     sizeof(desc->mpls));
	case PTYPE_GRE:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->gre.word[0],
				     sizeof(desc->gre));
	case PTYPE_SKIP:
		return write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->skip.word[0],
				     sizeof(desc->skip));
	default:
		break;
	}
	return ret;
}

static union filter_desc *get_default_rule(struct k1c_eth_hw *hw,
		enum k1c_eth_layer layer)
{
	switch (layer) {
	case K1C_NET_LAYER_2:
		return (union filter_desc *) &mac_filter_default;
	case K1C_NET_LAYER_3:
		return (union filter_desc *) &ipv4_filter_default;
	default:
		dev_err(hw->dev, "Default rules make no sense for layer superior than 3\n");
		return NULL;
	}
}

/** parser_disable() - Disable parser parser_id
 * Context: can not be called in interrupt context (readq_poll_timeout)
 *
 * Return: 0 on success, negative on failure
 */
int parser_disable(struct k1c_eth_hw *hw, int parser_id)
{
	u32 off = PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * parser_id;
	u32 val = K1C_ETH_SETF(PARSER_DISABLED,
			       PARSER_CTRL_DISPATCH_POLICY);
	int ret = 0;

	k1c_eth_writel(hw, val, off + PARSER_CTRL_CTL);
	ret = readl_poll_timeout(hw->res[K1C_ETH_RES_ETH].base + off +
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
int parser_config(struct k1c_eth_hw *hw, struct k1c_eth_lane_cfg *cfg,
		  int parser_id, enum parser_dispatch_policy policy)
{
	union filter_desc **rules = hw->parsing.parsers[parser_id].filters;
	int rules_len =  hw->parsing.parsers[parser_id].nb_layers;
	int ret, rule, word_index = 0;
	union filter_desc *filter_desc;

	ret = parser_disable(hw, parser_id);
	if (ret)
		return ret;

	for (rule = 0; rule < rules_len; ++rule) {
		filter_desc = rules[rule];
		if (filter_desc == NULL) {
			filter_desc = get_default_rule(hw, rule);
			if (filter_desc == NULL)
				return -EINVAL;
		}
		word_index = parser_add_filter(hw, parser_id,
				word_index, filter_desc);
	}
	if (word_index < 0) {
		dev_err(hw->dev, "Failed to add filter[%d] to parser[%d] (ret: %d)\n",
				rule, parser_id, word_index);
		return -EINVAL;
	}

	word_index = parser_commit_filter(hw, cfg, parser_id, word_index,
					  policy, PARSER_DEFAULT_PRIORITY);
	if (word_index < 0) {
		dev_err(hw->dev, "Failed to commit filters to parser[%d]\n",
			parser_id);
		return -EBUSY;
	}

	return 0;
}
