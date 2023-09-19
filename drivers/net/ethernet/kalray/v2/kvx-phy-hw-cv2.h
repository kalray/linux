/* SPDX-License-Identifier: GPL-2.0 */
/**
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2020 Kalray Inc.
 */

#include <linux/firmware.h>

#ifndef KVX_PHY_HW_CV2_H
#define KVX_PHY_HW_CV2_H

#define ROPLL_WORD_CLK    0
#define ROPLL_DWORD_CLK   1
#define ROPLL_QWORD_CLK   2
#define ROPLL_OWORD_CLK   3
#define ROPLL_DIV_CLK     4
#define ROPLL_DIV33_CLK   5
#define ROPLL_DIV66_CLK   6
#define ROPLL_DIV16p5_CLK 7

int kvx_phy_init_sequence_cv2(struct kvx_eth_hw *hw, const struct firmware *fw);
int kvx_phy_enable_serdes_cv2(struct kvx_eth_hw *hw, int fst_lane, int lane_nb, int lane_speed);
int kvx_phy_disable_serdes_cv2(struct kvx_eth_hw *hw, int fst_lane, int lane_nb);
int kvx_phy_lane_rx_serdes_data_enable_cv2(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
void kvx_phy_get_tx_eq_coef_cv2(struct kvx_eth_hw *hw, int lane_id, struct tx_coefs *coef);
void kvx_phy_set_tx_eq_coef_cv2(struct kvx_eth_hw *hw, int lane_id, struct tx_coefs *coef);
void kvx_phy_set_tx_default_eq_coef_cv2(struct kvx_eth_hw *hw, struct kvx_eth_lane_cfg *cfg);
int kvx_phy_rx_adapt_v1_cv2(struct kvx_eth_hw *hw, int lane_id);
int kvx_phy_rx_adapt_v2_cv2(struct kvx_eth_hw *hw, int lane_id);

#endif
