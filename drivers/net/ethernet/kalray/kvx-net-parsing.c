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

static void update_parser_desc(struct kvx_eth_hw *hw,
		unsigned int parser)
{
	struct kvx_eth_parser_f *parser_f = &hw->parser_f[parser];
	char *buf = parser_f->desc;
	unsigned int rule;

	for (rule = 0; rule < ARRAY_SIZE(parser_f->rules); ++rule) {
		buf += sprintf(buf, PARSER_RULE_FMT,
				parser_f->rules[rule].enable,
				parser_f->rules[rule].type,
				parser_f->rules[rule].add_metadata_index,
				parser_f->rules[rule].check_header_checksum);
	}
	BUG_ON(buf >= parser_f->desc + sizeof(parser_f->desc));
}

/**
 * clear_parser_f() - Clear a sysfs parser structure, use this when you delete a
 *   parser to replicate the change on the sysfs
 * @hw: this ethernet hw device
 * @parser_id: the parser physical id
 */
static void clear_parser_f(struct kvx_eth_hw *hw, int parser_id)
{
	struct kvx_eth_parser_f *parser_f = &hw->parser_f[parser_id];
	int i;

	parser_f->enable = false;
	for (i = 0; i < ARRAY_SIZE(parser_f->rules); ++i) {
		parser_f->rules[i].enable = false;
		parser_f->rules[i].type = 0;
		parser_f->rules[i].add_metadata_index = 0;
		parser_f->rules[i].check_header_checksum = 0;
	}
	update_parser_desc(hw, parser_id);
}

/**
 * update_parser_f() - Fill the sysfs structure from a parser rule, use this
 *   when you modify a parser to reflect the change
 * @hw: this ethernet hw device
 * @filter_id: the parser virtual id
 * @parser_id: the parser physical id
 * Return: Error if the rule is malformed
 */
static int update_parser_f(struct kvx_eth_hw *hw,
		int filter_id, int parser_id)
{
	union filter_desc **rules;
	int rule, rules_len;
	int add_metadata_index, check_header_checksum;
	struct kvx_eth_rule_f *rule_f;

	rules = hw->parsing.parsers[filter_id].filters;
	rules_len =  hw->parsing.parsers[filter_id].nb_layers;

	for (rule = 0; rule < rules_len; ++rule) {
		union filter_desc *desc = rules[rule];
		u32 ptype = (*(u32 *)desc) & PTYPE_MASK;

		switch (ptype) {
		case PTYPE_MAC_VLAN:
			add_metadata_index = desc->mac_vlan.add_metadata_index;
			check_header_checksum = 0;
			break;
		case PTYPE_VXLAN:
			add_metadata_index = desc->vxlan.add_metadata_index;
			check_header_checksum = 0;
			break;
		case PTYPE_IP_V4:
			add_metadata_index = desc->ipv4.add_metadata_index;
			check_header_checksum = desc->ipv4.check_header_checksum;
			break;
		case PTYPE_IP_V6:
			add_metadata_index = desc->ipv6.d0.add_metadata_index;
			check_header_checksum = 0;
			break;
		case PTYPE_UDP:
			add_metadata_index = desc->udp.add_metadata_index;
			check_header_checksum = desc->udp.check_header_checksum;
			break;
		case PTYPE_TCP:
			add_metadata_index = desc->tcp.add_metadata_index;
			check_header_checksum = desc->tcp.check_header_checksum;
			break;
		case PTYPE_CUSTOM:
			add_metadata_index = desc->custom.add_metadata_index;
			check_header_checksum = 0;
			break;
		case PTYPE_ROCE:
			add_metadata_index = desc->roce.add_metadata_index;
			check_header_checksum = desc->roce.check_icrc;
			break;
		case PTYPE_MPLS:
			add_metadata_index = desc->mpls.add_metadata_index;
			check_header_checksum = 0;
			break;
		case PTYPE_SKIP:
			add_metadata_index = 0;
			check_header_checksum = 0;
			break;
		default:
			return -EINVAL;
		}

		rule_f = &hw->parser_f[parser_id].rules[rule];
		rule_f->enable = true;
		rule_f->type = ptype;
		rule_f->add_metadata_index = add_metadata_index;
		rule_f->check_header_checksum = check_header_checksum;
	}
	hw->parser_f[parser_id].enable = true;
	update_parser_desc(hw, parser_id);
	return 0;
}


static int parser_check(unsigned int parser_id, unsigned int word_index)
{
	if (parser_id > KVX_ETH_PHYS_PARSER_NB)
		return -EINVAL;

	if ((word_index & 0xf) >= PARSER_RAM_WORD_NB - 1) {
		pr_err("word_index: %d\n", word_index);
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_KVX_SUBARCH_KV3_1
#define RAM(p)      (PARSER_RAM_OFFSET + PARSER_RAM_ELEM_SIZE * (p))
#define RAM_LINE(l) (PARSER_RAM_LINE + (l) * PARSER_RAM_LINE_ELEM_SIZE)
#else
#define RAM(p)      (KVX_ETH_LBA_PARSER_RAM_GRP_OFFSET + KVX_ETH_LBA_PARSER_RAM_GRP_ELEM_SIZE * (p))
#define RAM_LINE(l) (KVX_ETH_LBA_PARSER_RAM_LB_PARSER_RAM_LINE_GRP_OFFSET + (l) * KVX_ETH_LBA_PARSER_RAM_LB_PARSER_RAM_LINE_GRP_ELEM_SIZE)
#endif


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
#ifdef CONFIG_KVX_SUBARCH_KV3_2
	struct kvx_eth_parser_f *parser = &hw->parser_f[parser_id];
#endif

	ret = parser_check(parser_id, word_index);
	if (ret < 0) {
		dev_err(hw->dev, "Lane[%d] parser check failed\n", cfg->id);
		return ret;
	}
#ifdef CONFIG_KVX_SUBARCH_KV3_1
	kvx_eth_writel(hw, PTYPE_END_OF_RULE, off + i * PARSER_RAM_WORD_SIZE);
#else
	kvx_eth_rxlbana_writel(hw, PTYPE_END_OF_RULE, off + i * PARSER_RAM_WORD_SIZE);
#endif
	i++;
#ifdef CONFIG_KVX_SUBARCH_KV3_1
	off = PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * parser_id;
	val = ((u32)policy << PARSER_CTRL_DISPATCH_POLICY_SHIFT) |
		((u32)cfg->id << PARSER_CTRL_LANE_SRC_SHIFT) |
		((u32)prio << PARSER_CTRL_PRIO_SHIFT) |
		((u32)PARSER_RR_PKT_NB << PARSER_CTRL_RR_PKT_NB_SHIFT) |
		((u32)HASH_SEED << PARSER_CTRL_HASH_SEED_SHIFT);
	kvx_eth_writel(hw, val, off + PARSER_CTRL_CTL);
#else
	off = KVX_ETH_LBA_PARSER_GRP_OFFSET + KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * parser_id;
	if (policy == PARSER_DROP) {
		parser->disp_info = DISPATCH_INFO_DROP;
		val = ((u32) parser->disp_info) << KVX_ETH_LBA_PARSER_DISPATCH_INFO_DROP_SHIFT;
		kvx_eth_rxlbana_writel(hw, val, off + KVX_ETH_LBA_PARSER_DISPATCH_INFO_OFFSET);
		parser->disp_policy = (u32) POLICY_PARSER;
	} else {
		parser->disp_policy = (u32) POLICY_USE_RSS;
	}
	kvx_eth_rxlbana_writel(hw, parser->disp_policy, off + KVX_ETH_LBA_PARSER_DISPATCH_POLICY_OFFSET);
	parser->ctrl = (KVX_ETH_RX_LBA_PARSER_CTRL_ENABLE |
			(1 << (KVX_ETH_LBA_PARSER_CTRL_LANE_SRC_SHIFT + (u32)cfg->id)) |
			((u32)prio << KVX_ETH_LBA_PARSER_CTRL_PRIORITY_SHIFT));
	kvx_eth_rxlbana_writel(hw, parser->ctrl, off + KVX_ETH_LBA_PARSER_CTRL_OFFSET);
#endif

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

#ifdef CONFIG_KVX_SUBARCH_KV3_1
	kvx_eth_writel(hw, *(u32 *)&desc->word[0],
		       off + PARSER_RAM_WORD_SIZE * i);
#else
	kvx_eth_rxlbana_writel(hw, *(u32 *)&desc->word[0],
		       off + PARSER_RAM_WORD_SIZE * i);
#endif
	++i;
	for (j = 0; j < PARSER_RAM_WORD_NB - 1; ++j)
#ifdef CONFIG_KVX_SUBARCH_KV3_1
		kvx_eth_writel(hw, 0, off + PARSER_RAM_WORD_SIZE * (i + j));
#else
		kvx_eth_rxlbana_writel(hw, 0, off + PARSER_RAM_WORD_SIZE * (i + j));
#endif
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
#ifdef CONFIG_KVX_SUBARCH_KV3_1
		kvx_eth_writel(hw, data[j], off + PARSER_RAM_WORD_SIZE * i);
#else
		kvx_eth_rxlbana_writel(hw, data[j], off + PARSER_RAM_WORD_SIZE * i);
#endif
		++i;
	}
	/* Fill in the rest of the line */
	for (; j < PARSER_RAM_WORD_NB; ++j) {
#ifdef CONFIG_KVX_SUBARCH_KV3_1
		kvx_eth_writel(hw, 0, off + PARSER_RAM_WORD_SIZE * i);
#else
		kvx_eth_rxlbana_writel(hw, 0, off + PARSER_RAM_WORD_SIZE * i);
#endif
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
 * @rule_id: current rule index
 * @total_add_index: accumulation of rules setting an index in the selected
 *                   parser
 * @total_check_checksum: accumulation of rules setting a checksum offload in
 *                        the selected parser
 *
 * Return: next RAM write index on success, negative on failure
 */
static int parser_add_filter(struct kvx_eth_hw *hw, unsigned int parser_id,
		      unsigned int idx, union filter_desc *desc,
		      unsigned int rule_id, u32 *total_add_index,
		      u32 *total_check_checksum)
{
	int ret = -EINVAL;
	u32 ptype;
	int add_metadata_index, check_header_checksum;

	ret = parser_check(parser_id, idx);
	if (ret < 0) {
		dev_err(hw->dev, "Parser[%d] check failed\n", parser_id);
		return ret;
	}
	ptype = (*(u32 *)desc) & PTYPE_MASK;
	switch (ptype) {
	case PTYPE_MAC_VLAN:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter mac\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter mac: ", DUMP_PREFIX_NONE,
				16, 4, desc->mac_vlan.word,
				sizeof(desc->mac_vlan.word), false);
		add_metadata_index = desc->mac_vlan.add_metadata_index;
		check_header_checksum = 0;
		*total_add_index += desc->mac_vlan.add_metadata_index;
		ret = write_ramline(hw, parser_id, idx,
				     &desc->mac_vlan.word[0],
				     sizeof(desc->mac_vlan));
		break;
	case PTYPE_VXLAN:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter vxlan\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter vxlan: ", DUMP_PREFIX_NONE,
				16, 4, desc->vxlan.word, sizeof(desc->vxlan.word),
				false);
		add_metadata_index = desc->vxlan.add_metadata_index;
		check_header_checksum = 0;
		*total_add_index += desc->vxlan.add_metadata_index;
		ret = write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->vxlan.word[0],
				     sizeof(desc->vxlan));
		break;
	case PTYPE_IP_V4:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter ipv4\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter ipv4: ", DUMP_PREFIX_NONE,
				16, 4, desc->ipv4.word, sizeof(desc->ipv4.word),
				false);
		add_metadata_index = desc->ipv4.add_metadata_index;
		check_header_checksum = desc->ipv4.check_header_checksum;
		*total_add_index += desc->ipv4.add_metadata_index;
		*total_check_checksum += desc->ipv4.check_header_checksum;
		ret = write_ramline(hw, parser_id, idx,
				     &desc->ipv4.word[0],
				     sizeof(desc->ipv4));
		break;
	case PTYPE_IP_V6:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter ipv6\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter ipv6: ", DUMP_PREFIX_NONE,
				16, 4, desc->ipv6.d0.word, sizeof(desc->ipv6.d0.word),
				false);
		print_hex_dump_debug("filter ipv6: ", DUMP_PREFIX_NONE,
				16, 4, desc->ipv6.d1.word, sizeof(desc->ipv6.d1.word),
				false);
		print_hex_dump_debug("filter ipv6: ", DUMP_PREFIX_NONE,
				16, 4, desc->ipv6.d2.word, sizeof(desc->ipv6.d2.word),
				false);
		add_metadata_index = desc->ipv6.d0.add_metadata_index;
		check_header_checksum = 0;
		*total_add_index += desc->ipv6.d0.add_metadata_index;
		ret = parser_add_ipv6_filter(hw, parser_id,
					      idx, &desc->ipv6);
		break;
	case PTYPE_UDP:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter udp\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter udp: ", DUMP_PREFIX_NONE,
				16, 4, desc->udp.word, sizeof(desc->udp.word),
				false);
		add_metadata_index = desc->udp.add_metadata_index;
		check_header_checksum = desc->udp.check_header_checksum;
		*total_add_index += desc->udp.add_metadata_index;
		*total_check_checksum += desc->udp.check_header_checksum;
		ret = write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->udp.word[0],
				     sizeof(desc->udp));
		break;
	case PTYPE_TCP:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter tcp\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter tcp: ", DUMP_PREFIX_NONE,
				16, 4, desc->tcp.word, sizeof(desc->tcp.word),
				false);
		add_metadata_index = desc->tcp.add_metadata_index;
		check_header_checksum = desc->tcp.check_header_checksum;
		*total_add_index += desc->tcp.add_metadata_index;
		*total_check_checksum += desc->tcp.check_header_checksum;
		ret = write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->tcp.word[0],
				     sizeof(desc->tcp));
		break;
	case PTYPE_CUSTOM:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter custom\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter custom: ", DUMP_PREFIX_NONE,
				16, 4, desc->custom.word, sizeof(desc->custom.word),
				false);
		add_metadata_index = desc->custom.add_metadata_index;
		check_header_checksum = 0;
		*total_add_index += desc->custom.add_metadata_index;
		ret = write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->custom.word[0],
				     sizeof(desc->custom));
		break;
	case PTYPE_ROCE:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter roce\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter roce: ", DUMP_PREFIX_NONE,
				16, 4, desc->roce.word, sizeof(desc->roce.word),
				false);
		add_metadata_index = desc->roce.add_metadata_index;
		check_header_checksum = desc->roce.check_icrc;
		*total_add_index += desc->roce.add_metadata_index;
		*total_check_checksum += desc->roce.check_icrc;
		ret = write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->roce.word[0],
				     sizeof(desc->roce));
		break;
	case PTYPE_MPLS:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter mpls\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter mpls: ", DUMP_PREFIX_NONE,
				16, 4, desc->mpls.word, sizeof(desc->mpls.word),
				false);
		add_metadata_index = desc->mpls.add_metadata_index;
		check_header_checksum = 0;
		*total_add_index += desc->mpls.add_metadata_index;
		ret = write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->mpls.word[0],
				     sizeof(desc->mpls));
		break;
	case PTYPE_SKIP:
		dev_dbg(hw->dev, "Parser[%d] rule[%d] filter skip\n",
				parser_id, rule_id);
		print_hex_dump_debug("filter skip: ", DUMP_PREFIX_NONE,
				16, 4, desc->skip.word, sizeof(desc->skip.word),
				false);
		add_metadata_index = 0;
		check_header_checksum = 0;
		ret = write_ramline(hw, parser_id, idx,
				     (u32 *)&desc->skip.word[0],
				     sizeof(desc->skip));
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

/** parser_disable() - Disable parser parser_id
 * Context: can not be called in interrupt context (readq_poll_timeout)
 *
 * Return: 0 on success, negative on failure
 */
static int parser_disable(struct kvx_eth_hw *hw, int parser_id)
{
#ifdef CONFIG_KVX_SUBARCH_KV3_1
	u32 off = PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * parser_id;
	u32 val = (u32)PARSER_DISABLED << PARSER_CTRL_DISPATCH_POLICY_SHIFT;
#else
	u32 val = 0;
	u32 off = KVX_ETH_LBA_PARSER_GRP_OFFSET + KVX_ETH_LBA_PARSER_GRP_ELEM_SIZE * parser_id;
	struct kvx_eth_parser_f *parser = &hw->parser_f[parser_id];
#endif
	int ret = 0;

	dev_dbg(hw->dev, "Disable parser[%d]\n", parser_id);
#ifdef CONFIG_KVX_SUBARCH_KV3_1
	kvx_eth_writel(hw, val, off + PARSER_CTRL_CTL);
#else
	parser->ctrl = KVX_ETH_RX_LBA_PARSER_CTRL_DISABLE;
	kvx_eth_rxlbana_writel(hw, parser->ctrl, off + KVX_ETH_LBA_PARSER_CTRL_OFFSET);
#endif
#ifdef CONFIG_KVX_SUBARCH_KV3_1
	ret = readl_poll_timeout(hw->res[KVX_ETH_RES_ETH].base + off +
				 PARSER_CTRL_STATUS, val, !val,
				 READ_DELAY, READ_TIMEOUT);
#else
	ret = readl_poll_timeout(hw->res[KVX_ETH_RES_ETH_RX_LB_ANA].base + off +
				 KVX_ETH_LBA_PARSER_STATUS_OFFSET, val,
				 (val == KVX_ETH_RX_LBA_PARSER_STATUS_STOPPED), READ_DELAY, READ_TIMEOUT);
#endif
	if (unlikely(ret)) {
		dev_err(hw->dev, "Disable parser[%d] timeout\n", parser_id);
		return ret;
	}

	/* Reset hit_cnt */
#ifdef CONFIG_KVX_SUBARCH_KV3_1
	kvx_eth_readl(hw, off + PARSER_CTRL_HIT_CNT + 4);
#else
	kvx_eth_rxlbana_readl(hw, off + KVX_ETH_LBA_PARSER_HIT_CNT_LAC_OFFSET);
#endif
	clear_parser_f(hw, parser_id);
	return 0;
}

/**
 * parser_disable_wrapper() - Disable a parser and its mirror
 * @hw: this hardware
 * @parser_id: physical parser id
 * Return: 0 on success, negative on failure
 */
int parser_disable_wrapper(struct kvx_eth_hw *hw, int parser_id)
{
	int ret;

	ret = parser_disable(hw, parser_id);
	if (ret != 0)
		return ret;
	if (hw->parsers_tictoc)
		ret = parser_disable(hw, parser_id + KVX_ETH_PARSER_NB);
	return ret;
}

/**
 * eth_config_parser() - Configure n rules for parser parser_id
 * Context: can not be called in interrupt context (readq_poll_timeout)
 *
 * Return: 0 on success, negative on failure
 */
static int parser_config(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg,
		  int parser_id, enum parser_dispatch_policy policy, int prio)
{
	int filter_id = parser_id % KVX_ETH_PARSER_NB;
	union filter_desc **rules = hw->parsing.parsers[filter_id].filters;
	int rules_len =  hw->parsing.parsers[filter_id].nb_layers;

	int ret, rule, word_index = 0;
	union filter_desc *filter_desc;
	u32 total_add_index = 0;
	u32 total_check_checksum = 0;

	ret = parser_disable(hw, parser_id);
	if (ret)
		return ret;

	dev_dbg(hw->dev, "Enable parser[%d] with prio %d\n", parser_id, prio);
	for (rule = 0; rule < rules_len; ++rule) {
		filter_desc = rules[rule];
		word_index = parser_add_filter(hw, parser_id,
				word_index, filter_desc, rule,
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

	/* Update sysfs structure */
	if (update_parser_f(hw, filter_id, parser_id) < 0) {
		parser_disable(hw, parser_id);
		return -EINVAL;
	}

	return 0;
}

/**
 * parser_config_wrapper() - Configure a parser and its mirror
 * @hw: this hardware
 * @cfg: lane configuration
 * @parser_id: physical parser id
 * @policy: dispatch policy
 * @prio: parser priority
 * Return: 0 on success, negative on failure
 */
int parser_config_wrapper(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg,
		  int parser_id, enum parser_dispatch_policy policy, int prio)
{
	if (parser_config(hw, cfg, parser_id, policy, prio) != 0)
		return -EBUSY;

	if (hw->parsers_tictoc && kvx_eth_speed_aggregated(cfg->speed)) {
		/* Mirror parser configuration to the top half */
		if (parser_config(hw, cfg, parser_id + KVX_ETH_PARSER_NB,
				policy, prio) != 0) {
			/* Attempt to disable first parser */
			parser_disable(hw, parser_id);
			return -EBUSY;
		}
	}

	return 0;
}

/**
 * parser_config_update() - Enable/Disable parser mirror dep. on speed
 * @hw: this hardware
 * @cfg: lane configuration
 *
 * Return: 0 on success, negative on failure
 */
int parser_config_update(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg)
{
	bool aggregated = kvx_eth_speed_aggregated(cfg->speed);
	u32 prio, policy, reg, val;
	int i, parser_id;

	if (!hw->parsers_tictoc)
		return 0;

	for (i = 0; i < KVX_ETH_PARSER_NB; i++) {
		if (hw->parsing.parsers[i].enabled) {
			reg = PARSER_CTRL_OFFSET + PARSER_CTRL_ELEM_SIZE * i;
			val = kvx_eth_readl(hw, reg + PARSER_CTRL_CTL);
			prio = GETF(val, PARSER_CTRL_PRIO);
			policy = GETF(val, PARSER_CTRL_DISPATCH_POLICY);
			parser_id = i + KVX_ETH_PARSER_NB;
			if (aggregated) {
				/* Mirror parser configuration to the top half */
				if (parser_config(hw, cfg, parser_id,
						  policy, prio) != 0) {
					parser_disable(hw, parser_id);
					return -EBUSY;
				}
			} else {
				parser_disable(hw, parser_id);
			}
		}
	}

	return 0;
}
