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
#endif
