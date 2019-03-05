// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

unsigned long long mppa_dma_mem2noc_stride2stride[13] = {
0x0000002800008000ULL,  /* C_0: dcnt[0]=Rw[8];*/
0x0000003004000032ULL,  /* C_1: if(dcnt[0]==0) goto C_12; rptr=Rd[0];*/
0x0000003200040000ULL,  /* C_2: wptr=Rd[1];*/
0x0000002400009000ULL,  /* C_3: dcnt[1]=Rw[4];*/
0x000000000000501aULL,  /* C_4: if(dcnt[1]==0) goto C_6; dcnt[1]--;*/
0x0000000009485017ULL,  /* C_5: if(dcnt[1]!=0) goto C_5; dcnt[1]--;
			 * wptr_type=NOC_ABS_OFFSET; SEND_16B(*rptr, *wptr);
			 * rptr+=16; wptr+=16;
			 */
0x0000002600009000ULL,  /* C_6: dcnt[1]=Rw[6];*/
0x0000000000005026ULL,  /* C_7: if(dcnt[1]==0) goto C_9; dcnt[1]--;*/
0x0000000009085023ULL,  /* C_8: if(dcnt[1]!=0) goto C_8; dcnt[1]--;
			 * wptr_type=NOC_ABS_OFFSET; SEND_1B(*rptr, *wptr);
			 * rptr+=1; wptr+=1;
			 */
0x0000003c000c4000ULL,  /* C_9: dcnt[0]--; wptr+=Rd[6];*/
0x0000003a0c00000fULL,  /* C_10: if(dcnt[0]!=0) goto C_3; rptr+=Rd[5];*/
0x0000003200040000ULL,  /* C_11: wptr=Rd[1];*/
0x0000000001800e00ULL}; /* C_12: STOP(); SEND_EOT(); FLUSH(); NOTIFY();*/
