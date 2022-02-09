/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef ASM_KVX_DMA_REGS_H
#define ASM_KVX_DMA_REGS_H

#define KVX_DMA_RX_CHANNEL_NUMBER            (64)
#define KVX_DMA_RX_JOB_QUEUE_NUMBER          (8)
#define KVX_DMA_RX_JOB_CACHE_NUMBER          (4)
#define KVX_DMA_TX_THREAD_NUMBER             (4)
#define KVX_DMA_TX_PGRM_MEM_NUMBER           (128)
#define KVX_DMA_TX_PGRM_TABLE_NUMBER         (16)
#define KVX_DMA_NOC_ROUTE_TABLE_NUMBER       (512)
#define KVX_DMA_BW_LIMITER_TABLE_NUMBER      (16)
#define KVX_DMA_TX_JOB_QUEUE_NUMBER          (64)
#define KVX_DMA_TX_COMPLETION_QUEUE_NUMBER   (64)
#define KVX_DMA_TX_PGRM_MEM_NUMBER           (128)
#define KVX_DMA_TX_PGRM_TAB_NUMBER           (16)
#define KVX_DMA_ASN_MASK                     (0x1FF)

/* RX channels */
#define KVX_DMA_RX_CHAN_OFFSET                     0x0
#define KVX_DMA_RX_CHAN_ELEM_SIZE                  0x1000
#define KVX_DMA_RX_CHAN_BUF_SA_OFFSET              0x0
#define KVX_DMA_RX_CHAN_BUF_SIZE_OFFSET            0x8
#define KVX_DMA_RX_CHAN_BUF_EN_OFFSET              0x10
#define KVX_DMA_RX_CHAN_CUR_OFFSET                 0x18
#define KVX_DMA_RX_CHAN_CUR_OFFSET_SHIFT           0x0
#define KVX_DMA_RX_CHAN_CUR_OFFSET_MASK            0x1FFFFFFFFFF
#define KVX_DMA_RX_CHAN_JOB_Q_CFG_OFFSET           0x20
#define KVX_DMA_RX_CHAN_ACTIVATED_OFFSET           0x28
#define KVX_DMA_RX_CHAN_BYTE_CNT_OFFSET            0x30
#define KVX_DMA_RX_CHAN_NOTIF_CNT_OFFSET           0x38
#define KVX_DMA_RX_CHAN_CNT_CLEAR_MODE_OFFSET      0x40
#define KVX_DMA_RX_CHAN_COMP_Q_CFG_OFFSET          0x58
#define KVX_DMA_RX_CHAN_COMP_Q_MODE_OFFSET         0x60
#define KVX_DMA_RX_CHAN_COMP_Q_SA_OFFSET           0x68
#define KVX_DMA_RX_CHAN_COMP_Q_SLOT_NB_LOG2_OFFSET 0x70
#define KVX_DMA_RX_CHAN_COMP_Q_WP_OFFSET           0x78
#define KVX_DMA_RX_CHAN_COMP_Q_RP_OFFSET           0x80
#define KVX_DMA_RX_CHAN_COMP_Q_LOAD_INCR_RP_OFFSET 0x88
#define KVX_DMA_RX_CHAN_COMP_Q_VALID_RP_OFFSET     0x90
#define KVX_DMA_RX_CHAN_COMP_Q_NOTIF_ADDR_OFFSET   0xA0
#define KVX_DMA_RX_CHAN_COMP_Q_FULL_NOTIF_ADDR_OFFSET 0xA8
#define KVX_DMA_RX_CHAN_COMP_Q_NOTIF_ARG_OFFSET    0xB0
#define KVX_DMA_RX_CHAN_COMP_Q_ASN_OFFSET          0xB8

/* RX job queues */
#define KVX_DMA_RX_JOB_Q_OFFSET                    0x40000
#define KVX_DMA_RX_JOB_Q_ELEM_SIZE                 0x1000
#define KVX_DMA_RX_JOB_Q_SA_OFFSET                 0x0
#define KVX_DMA_RX_JOB_Q_NB_LOG2_OFFSET            0x8
#define KVX_DMA_RX_JOB_Q_WP_OFFSET                 0x10
#define KVX_DMA_RX_JOB_Q_LOAD_INCR_WP_OFFSET       0x18
#define KVX_DMA_RX_JOB_Q_VALID_WP_OFFSET           0x20
#define KVX_DMA_RX_JOB_Q_LOAD_INCR_VALID_WP_OFFSET 0x28
#define KVX_DMA_RX_JOB_Q_RP_OFFSET                 0x30
#define KVX_DMA_RX_JOB_Q_NOTIF_ADDR_OFFSET         0x38
#define KVX_DMA_RX_JOB_Q_NOTIF_ARG_OFFSET          0x40
#define KVX_DMA_RX_JOB_Q_NOTIF_MODE_OFFSET         0x48
#define KVX_DMA_RX_JOB_Q_STOP_OFFSET               0x58
#define KVX_DMA_RX_JOB_Q_ACTIVATE_OFFSET           0x50
#define KVX_DMA_RX_JOB_Q_STATUS_OFFSET             0x60
#define KVX_DMA_RX_JOB_Q_CACHE_ID_OFFSET           0x70
#define KVX_DMA_RX_JOB_Q_CACHE_ID_CACHE_ID_SHIFT   0
#define KVX_DMA_RX_JOB_Q_CACHE_ID_PRIO_SHIFT       8

#define KVX_DMA_RX_JOB_Q_ASN_OFFSET                0x78

/* RX job cache */
#define RX_JOB_CACHE_OFFSET                        0x48000
#define RX_JOB_CACHE_ELEM_SIZE                     0x1000
#define RX_JOB_CACHE_JOB_NB                        0x0
#define RX_JOB_CACHE_EMPTY_NOTIF_ADDR_OFFSET       0x8
#define RX_JOB_CACHE_EMPTY_NOTIF_ADDR_SHIFT        0x0
#define RX_JOB_CACHE_EMPTY_NOTIF_ADDR_MASK         0x1FFFFFFFFFF
#define RX_JOB_CACHE_EMPTY_NOTIF_ARG               0x10
#define RX_JOB_CACHE_EMPTY_NOTIF_ARG_INDEX_SHIFT   0x0
#define RX_JOB_CACHE_EMPTY_NOTIF_ARG_INDEX_MASK    0x3F
#define RX_JOB_CACHE_EMPTY_NOTIF_EN                0x18
#define RX_JOB_CACHE_EMPTY_NOTIF_ASN               0x20

/* TX job queues */
#define KVX_DMA_TX_JOB_Q_OFFSET                    0x80000
#define KVX_DMA_TX_JOB_Q_ELEM_SIZE                 0x1000
#define KVX_DMA_TX_JOB_Q_SA_OFFSET                 0x0
#define KVX_DMA_TX_JOB_Q_NB_LOG2_OFFSET            0x8
#define KVX_DMA_TX_JOB_Q_WP_OFFSET                 0x10
#define KVX_DMA_TX_JOB_Q_LOAD_INCR_WP_OFFSET       0x18
#define KVX_DMA_TX_JOB_Q_VALID_WP_OFFSET           0x20
#define KVX_DMA_TX_JOB_Q_RP_OFFSET                 0x30
#define KVX_DMA_TX_JOB_Q_NOTIF_ADDR_OFFSET         0x38
#define KVX_DMA_TX_JOB_Q_NOTIF_ARG_OFFSET          0x40
#define KVX_DMA_TX_JOB_Q_ASN_OFFSET                0x48
#define KVX_DMA_TX_JOB_Q_STATUS_OFFSET             0x50
#define KVX_DMA_TX_JOB_Q_STOP_OFFSET               0x68
#define KVX_DMA_TX_JOB_Q_ACTIVATE_OFFSET           0x60
#define KVX_DMA_TX_JOB_Q_THREAD_ID_OFFSET          0x70
#define KVX_DMA_TX_JOB_Q_ON_GOING_JOB_CNT_OFFSET   0x78

/* TX completion queues */
#define KVX_DMA_TX_COMP_Q_OFFSET                   0xC0000
#define KVX_DMA_TX_COMP_Q_ELEM_SIZE                0x1000
#define KVX_DMA_TX_COMP_Q_MODE_OFFSET              0x0
#define KVX_DMA_TX_COMP_Q_SA_OFFSET                0x8
#define KVX_DMA_TX_COMP_Q_NB_LOG2_OFFSET           0x10
#define KVX_DMA_TX_COMP_Q_GLOBAL_OFFSET            0x18
#define KVX_DMA_TX_COMP_Q_ASN_OFFSET               0x20
#define KVX_DMA_TX_COMP_Q_FIELD_EN_OFFSET          0x28
#define KVX_DMA_TX_COMP_Q_WP_OFFSET                0x30
#define KVX_DMA_TX_COMP_Q_RP_OFFSET                0x40
#define KVX_DMA_TX_COMP_Q_LOAD_INCR_RP_OFFSET      0x48
#define KVX_DMA_TX_COMP_Q_VALID_RP_OFFSET          0x50
#define KVX_DMA_TX_COMP_Q_LOAD_INCR_VALID_RP_OFFSET 0x58
#define KVX_DMA_TX_COMP_Q_NOTIF_ADDR_OFFSET        0x60
#define KVX_DMA_TX_COMP_Q_NOTIF_ARG_OFFSET         0x68
#define KVX_DMA_TX_COMP_Q_ACTIVATE_OFFSET          0x70
#define KVX_DMA_TX_COMP_Q_STOP_OFFSET              0x78
#define KVX_DMA_TX_COMP_Q_STATUS_OFFSET            0x80

/* DMA IT vector */
#define KVX_DMA_IT_OFFSET                          0x50000
#define KVX_DMA_IT_EN_OFFSET                       0x0
#define KVX_DMA_IT_VECTOR_OFFSET                   0x10
#define KVX_DMA_IT_VECTOR_LAC_OFFSET               0x18

/* DMA error status */
#define KVX_DMA_ERROR_OFFSET                       0x51000
#define KVX_DMA_ERROR_RX_CHAN_STATUS_OFFSET        0x0
#define KVX_DMA_ERROR_RX_JOB_STATUS_OFFSET         0x10
#define KVX_DMA_ERROR_TX_JOB_STATUS_OFFSET         0x20
#define KVX_DMA_ERROR_TX_THREAD_STATUS_OFFSET      0x30
#define KVX_DMA_ERROR_TX_COMP_STATUS_OFFSET        0x40

/* TX thread */
#define KVX_DMA_TX_THREAD_OFFSET                   0x60000
#define KVX_DMA_TX_THREAD_ELEM_SIZE                0x1000
#define KVX_DMA_TX_THREAD_ERROR_OFFSET             0x70
#define KVX_DMA_TX_THREAD_ASN_OFFSET               0x80

/* NOC route table */
#define KVX_DMA_NOC_RT_OFFSET                      0x66000
#define KVX_DMA_NOC_RT_ELEM_SIZE                   0x8
#define KVX_DMA_NOC_RT_NOC_ROUTE_SHIFT             0x0
#define KVX_DMA_NOC_RT_NOC_ROUTE_MASK              0xFFFFFFFFFF
#define KVX_DMA_NOC_RT_RX_TAG_SHIFT                0x28
#define KVX_DMA_NOC_RT_RX_TAG_MASK                 0x3F0000000000
#define KVX_DMA_NOC_RT_QOS_ID_SHIFT                0x2e
#define KVX_DMA_NOC_RT_QOS_ID_MASK                 0x3C00000000000
#define KVX_DMA_NOC_RT_GLOBAL_SHIFT                0x32
#define KVX_DMA_NOC_RT_GLOBAL_MASK                 0x4000000000000
#define KVX_DMA_NOC_RT_ASN_SHIFT                   0x33
#define KVX_DMA_NOC_RT_ASN_MASK                    0xFF8000000000000
#define KVX_DMA_NOC_RT_VCHAN_SHIFT                 0x3c
#define KVX_DMA_NOC_RT_VCHAN_MASK                  0x1000000000000000
#define KVX_DMA_NOC_RT_VALID_SHIFT                 0x3d
#define KVX_DMA_NOC_RT_VALID_MASK                  0x2000000000000000

/* Program table */
#define KVX_DMA_TX_PGRM_TAB_OFFSET                 0x65000
#define KVX_DMA_TX_PGRM_TAB_TRANSFER_MODE_SHIFT    0x7
#define KVX_DMA_TX_PGRM_TAB_TRANSFER_MODE_MASK     0x80
#define KVX_DMA_TX_PGRM_TAB_PM_START_ADDR_SHIFT    0x0
#define KVX_DMA_TX_PGRM_TAB_PM_START_ADDR_MASK     0x7F
#define KVX_DMA_TX_PGRM_TAB_GLOBAL_SHIFT           0x8
#define KVX_DMA_TX_PGRM_TAB_GLOBAL_MASK            0x100
#define KVX_DMA_TX_PGRM_TAB_ASN_SHIFT              0x9
#define KVX_DMA_TX_PGRM_TAB_ASN_MASK               0x3FE00
#define KVX_DMA_TX_PGRM_TAB_VALID_SHIFT            0x12
#define KVX_DMA_TX_PGRM_TAB_VALID_MASK             0x40000

/* Program memory */
#define KVX_DMA_TX_PGRM_MEM_OFFSET                 0x64000

#endif /* ASM_KVX_DMA_REGS_H */
