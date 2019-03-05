// SPDX-License-Identifier: GPL-2.0
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */
unsigned long long mppa_dma_mem2eth[22] = {
0x0000002400018000ULL,  /* C_0: dcnt[0]=Rw[4];*/
0x0000002800019000ULL,  /* C_1: dcnt[1]=Rw[8];*/
0x0000003004011027ULL,  /* C_2: if(dcnt[1]!=0) goto C_9; rptr=Rd[0];*/
0x0000003004014016ULL,  /* C_3: if(dcnt[0]==0) goto C_5;dcnt[0]--;rptr=Rd[0];*/
0x0000000009414013ULL,  /* C_4: if(dcnt[0]!=0) goto C_4; dcnt[0]--;
			 * wptr_type=NOC_REL_OFFSET; SEND_16B(*rptr, *wptr);
			 * rptr+=16; wptr=0;
			 */
0x0000002600018000ULL,  /* C_5: dcnt[0]=Rw[6];*/
0x0000000000014022ULL,  /* C_6: if(dcnt[0]==0) goto C_8; dcnt[0]--;*/
0x000000000901401fULL,  /* C_7: if(dcnt[0]!=0) goto C_7; dcnt[0]--;
			 *  wptr_type=NOC_REL_OFFSET; SEND_1B(*rptr, *wptr);
			 *  rptr+=1; wptr=0;
			 */
0x0000000001810a00ULL,  /* C_8: STOP(); FLUSH(); NOTIFY();*/
0x0000002600019000ULL,  /* C_9: dcnt[1]=Rw[6];*/
0x000000000001403eULL,  /* C_10: if(dcnt[0]==0) goto C_15; dcnt[0]--;*/
0x0000000000011037ULL,  /* C_11: if(dcnt[1]!=0) goto C_13;*/
0x000000000001404eULL,  /* C_12: if(dcnt[0]==0) goto C_19; dcnt[0]--;*/
0x0000000009414037ULL,  /* C_13: if(dcnt[0]!=0) goto C_13; dcnt[0]--;
			 *  wptr_type=NOC_REL_OFFSET; SEND_16B(*rptr, *wptr);
			 * rptr+=16; wptr=0;
			 */
0x000000000001104eULL,  /* C_14: if(dcnt[1]==0) goto C_19;*/
0x0000000000015052ULL,  /* C_15: if(dcnt[1]==0) goto C_20; dcnt[1]--;*/
0x000000000001504aULL,  /* C_16: if(dcnt[1]==0) goto C_18; dcnt[1]--;*/
0x0000000009015047ULL,  /* C_17: if(dcnt[1]!=0) goto C_17; dcnt[1]--;
			 * wptr_type=NOC_REL_OFFSET; SEND_1B(*rptr, *wptr);
			 * rptr+=1; wptr=0;
			 */
0x0000000001010c55ULL,  /* C_18: goto C_21; SEND_EOT(); FLUSH();
			 * wptr_type=NOC_REL_OFFSET; SEND_1B(*rptr, *wptr);
			 * wptr=0;
			 */
0x0000000009410c55ULL,  /* C_19: goto C_21; SEND_EOT(); FLUSH();
			 * wptr_type=NOC_REL_OFFSET; SEND_16B(*rptr, *wptr);
			 * rptr+=16; wptr=0;
			 */
0x0000000000010c55ULL,  /* C_20: goto C_21; SEND_EOT(); FLUSH();*/
0x0000000001810a00ULL}; /* C_21: STOP(); FLUSH(); NOTIFY();*/
