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
void clear_parser_f(struct kvx_eth_hw *hw, int parser_id)
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
	struct kvx_eth_dev *dev = KVX_HW2DEV(hw);

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
			if (dev->chip_rev_data->revision == COOLIDGE_V1) {
				add_metadata_index = desc->cv1_ipv4.add_metadata_index;
				check_header_checksum = desc->cv1_ipv4.check_header_checksum;
			} else {
				add_metadata_index = desc->cv2_ipv4.add_metadata_index;
				check_header_checksum = desc->cv2_ipv4.check_header_checksum;
			}
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

int parser_check(unsigned int parser_id, unsigned int word_index, enum coolidge_rev chip_rev)
{
	int parser_nb = (chip_rev == COOLIDGE_V1) ? KVX_ETH_PHYS_PARSER_NB_CV1 : KVX_ETH_PHYS_PARSER_NB_CV2;

	if (parser_id > parser_nb)
		return -EINVAL;

	if ((word_index & 0xf) >= PARSER_RAM_WORD_NB - 1) {
		pr_err("word_index: %d\n", word_index);
		return -EINVAL;
	}

	return 0;
}

#define RAM_CV2(p)      (KVX_ETH_LBA_PARSER_RAM_GRP_OFFSET + KVX_ETH_LBA_PARSER_RAM_GRP_ELEM_SIZE * (p))
#define RAM_LINE_CV2(l) (KVX_ETH_LBA_PARSER_RAM_LB_PARSER_RAM_LINE_GRP_OFFSET + (l) * KVX_ETH_LBA_PARSER_RAM_LB_PARSER_RAM_LINE_GRP_ELEM_SIZE)

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
	const struct kvx_eth_chip_rev_data *rev_d = kvx_eth_get_rev_data(hw);

	size = s / PARSER_RAM_WORD_SIZE;
	dev_dbg(hw->dev, "idx: %d array size: %d s: %d\n", idx, size, (int)s);
	for (j = 0; j < size; ++j) {
		rev_d->write_parser_ram_word(hw, data[j], parser_id, i);
		++i;
	}
	/* Fill in the rest of the line */
	for (; j < PARSER_RAM_WORD_NB; ++j) {
		rev_d->write_parser_ram_word(hw, 0, parser_id, i);
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
	struct kvx_eth_dev *dev = KVX_HW2DEV(hw);

	ret = parser_check(parser_id, idx, dev->chip_rev_data->revision);
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
		if (dev->chip_rev_data->revision == COOLIDGE_V1) {
			print_hex_dump_debug("filter ipv4: ", DUMP_PREFIX_NONE,
					16, 4, desc->cv1_ipv4.word, sizeof(desc->cv1_ipv4.word),
					false);
			add_metadata_index = desc->cv1_ipv4.add_metadata_index;
			check_header_checksum = desc->cv1_ipv4.check_header_checksum;
			*total_add_index += desc->cv1_ipv4.add_metadata_index;
			*total_check_checksum += desc->cv1_ipv4.check_header_checksum;
			ret = write_ramline(hw, parser_id, idx,
						&desc->cv1_ipv4.word[0],
						sizeof(desc->cv1_ipv4));
		} else {
			print_hex_dump_debug("filter ipv4: ", DUMP_PREFIX_NONE,
					16, 4, desc->cv2_ipv4.word, sizeof(desc->cv2_ipv4.word),
					false);
			add_metadata_index = desc->cv2_ipv4.add_metadata_index;
			check_header_checksum = desc->cv2_ipv4.check_header_checksum;
			*total_add_index += desc->cv2_ipv4.add_metadata_index;
			*total_check_checksum += desc->cv2_ipv4.check_header_checksum;
			ret = write_ramline(hw, parser_id, idx,
						&desc->cv2_ipv4.word[0],
						sizeof(desc->cv2_ipv4));
		}
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

/**
 * parser_disable_wrapper() - Disable a parser and its mirror
 * @hw: this hardware
 * @parser_id: physical parser id
 * Return: 0 on success, negative on failure
 */
int parser_disable_wrapper(struct kvx_eth_hw *hw, int parser_id)
{
	int ret;
	const struct kvx_eth_chip_rev_data *rev_d = kvx_eth_get_rev_data(hw);

	ret = rev_d->parser_disable(hw, parser_id);
	if (ret != 0)
		return ret;
	if (hw->parsers_tictoc)
		ret = rev_d->parser_disable(hw, parser_id + KVX_ETH_PARSER_NB);
	return ret;
}

/**
 * parser_config() - Configure n rules for parser parser_id
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
	const struct kvx_eth_chip_rev_data *rev_d = kvx_eth_get_rev_data(hw);

	ret = rev_d->parser_disable(hw, parser_id);
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

	word_index = rev_d->parser_commit_filter(hw, cfg, parser_id, word_index,
					  policy, prio);
	if (word_index < 0) {
		dev_err(hw->dev, "Failed to commit filters to parser[%d]\n",
			parser_id);
		return -EBUSY;
	}

	/* Update sysfs structure */
	if (update_parser_f(hw, filter_id, parser_id) < 0) {
		rev_d->parser_disable(hw, parser_id);
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
	const struct kvx_eth_chip_rev_data *rev_d = kvx_eth_get_rev_data(hw);

	if (parser_config(hw, cfg, parser_id, policy, prio) != 0)
		return -EBUSY;

	if (hw->parsers_tictoc && kvx_eth_speed_aggregated(cfg->speed)) {
		/* Mirror parser configuration to the top half */
		if (parser_config(hw, cfg, parser_id + KVX_ETH_PARSER_NB,
				policy, prio) != 0) {
			/* Attempt to disable first parser */
			rev_d->parser_disable(hw, parser_id);
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

	/* no parser tictoc on cv2 */
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
					parser_disable_cv1(hw, parser_id);
					return -EBUSY;
				}
			} else {
				parser_disable_cv1(hw, parser_id);
			}
		}
	}

	return 0;
}
