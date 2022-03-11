/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017-2022 Kalray Inc.
 * Authors: Clement Leger
 *          Julien Hascoet
 */

#ifndef _ASM_KVX_L2_CACHE_DEFS_H
#define _ASM_KVX_L2_CACHE_DEFS_H

/* Error registers */
enum l2_api_error {
	L2_STA_ERR_CFG = 1,
	L2_STA_ERR_ADDR,
	L2_STA_ERR_STOP,
	L2_STA_ERR_NO_MEM,
	L2_STA_ERR_DIS,
	L2_STA_ERR_DT
};

/* L2 Cache status registers definition */
#define L2_STATUS_OFFSET                           0x0
#define L2_STATUS_ERROR_MASK                       0x4
#define L2_STATUS_ERROR_SHIFT                      0x2
#define L2_STATUS_READY_MASK                       0x2
#define L2_STATUS_READY_SHIFT                      0x1
#define L2_STATUS_VALID_MASK                       0x1
#define L2_STATUS_VALID_SHIFT                      0x0
#define L2_STATUS_ACK_MASK                         0x80
#define L2_STATUS_ACK_SHIFT                        0x7

/* L2 Cache error registers definition */
#define L2_ERROR_OFFSET                            0x20
#define L2_ERROR_SETUP_ERR_MASK                    0x1
#define L2_ERROR_SETUP_ERR_SHIFT                   0x0
#define L2_ERROR_API_ERR_MASK                      0x20
#define L2_ERROR_API_ERR_SHIFT                     0x5
#define L2_ERROR_ERROR_CODE_MASK                   0xFF00
#define L2_ERROR_ERROR_CODE_SHIFT                  0x8

/* L2 Cache instance registers definition */
#define L2_INSTANCE_OFFSET                         0x8
#define L2_INSTANCE_CMD_QUEUE_SIZE_SHIFT           0x20
#define L2_INSTANCE_CMD_QUEUE_SIZE_MASK            0x7F00000000UL

/* L2 Cache commands fifo registers definition */
#define L2_CMD_OFFSET                              0x400
#define L2_CMD_READ_IDX_OFFSET                     0x0
#define L2_CMD_WRITE_IDX_OFFSET                    0x8
#define L2_CMD_DOORBELL_READ_ADDR_OFFSET           0x10
#define L2_CMD_DOORBELL_WRITE_ADDR_OFFSET          0x18
#define L2_CMD_FIFO_OFFSET                         0x20
#define L2_CMD_FIFO_ELEM_SIZE                      0x20
#define L2_CMD_OP_CMD_PURGE_LINE                   0x4
#define L2_CMD_OP_CMD_PURGE_AREA                   0x8
#define L2_CMD_OP_CMD_FLUSH_AREA                   0x9
#define L2_CMD_OP_CMD_INVAL_AREA                   0xA
#define L2_CMD_OP_VALID_SHIFT                      0x0
#define L2_CMD_OP_SYNC_SHIFT                       0x1
#define L2_CMD_OP_CMD_SHIFT                        0x2
#define L2_CMD_OP_ARG_COUNT                        0x3


#endif /* _ASM_KVX_L2_CACHE_DEFS_H */
