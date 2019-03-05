// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

unsigned long long mppa_dma_mem2mem_stride2stride[12] = {
0x0000002800028000ULL,  /* C_0: dcnt[0]=Rw[8];*/
0x000000300402002eULL,  /* C_1: if(dcnt[0]==0) goto C_11; rptr=Rd[0];*/
0x0000003200060000ULL,  /* C_2: wptr=Rd[1];*/
0x0000002400029000ULL,  /* C_3: dcnt[1]=Rw[4];*/
0x000000000002501aULL,  /* C_4: if(dcnt[1]==0) goto C_6; dcnt[1]--;*/
0x00000000094a5017ULL,  /* C_5: if(dcnt[1]!=0) goto C_5; dcnt[1]--;
			 * wptr_type=STORE_ADDR; SEND_16B(*rptr, *wptr);
			 * rptr+=16; wptr+=16;
			 */
0x0000002600029000ULL,  /* C_6: dcnt[1]=Rw[6];*/
0x0000000000025026ULL,  /* C_7: if(dcnt[1]==0) goto C_9; dcnt[1]--;*/
0x00000000090a5023ULL,  /* C_8: if(dcnt[1]!=0) goto C_8; dcnt[1]--;
			 * wptr_type=STORE_ADDR; SEND_1B(*rptr, *wptr);
			 * rptr+=1; wptr+=1;
			 */
0x0000003c000e4000ULL,  /* C_9: dcnt[0]--; wptr+=Rd[6];*/
0x0000003a0c02000fULL,  /* C_10: if(dcnt[0]!=0) goto C_3; rptr+=Rd[5];*/
0x0000000001820a00ULL}; /* C_11: STOP(); FLUSH(); NOTIFY();*/
