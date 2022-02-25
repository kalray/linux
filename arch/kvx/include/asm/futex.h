/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2021 Kalray Inc.
 * Authors:
 *      Clement Leger <cleger@kalray.eu>
 *      Yann Sionneau <ysionneau@kalray.eu>
 *      Jonathan Borne <jborne@kalray.eu>
 *
 * Part of code is taken from RiscV port
 */

#ifndef _ASM_KVX_FUTEX_H
#define _ASM_KVX_FUTEX_H

#ifdef __KERNEL__

#include <linux/futex.h>
#include <linux/uaccess.h>


#define __futex_atomic_op(insn, ret, oldval, uaddr, oparg) \
{ \
	__enable_user_access();                                 \
	__asm__ __volatile__ (                                  \
	"       fence                                   \n"     \
	"       ;;\n                                      "     \
	"1:     lwz $r63 = 0[%[u]]                      \n"     \
	"       ;;\n                                      "     \
	"       " insn "                                \n"     \
	"       ;;\n                                      "     \
	"       acswapw 0[%[u]], $r62r63                \n"     \
	"       ;;\n                                      "     \
	"       cb.deqz $r62? 1b                        \n"     \
	"       ;;\n                                      "     \
	"       copyd %[ov] = $r63                      \n"     \
	"       ;;\n                                      "     \
	"2:                                             \n"     \
	"       .section .fixup,\"ax\"                  \n"     \
	"3:     make %[r] = 2b                          \n"     \
	"       ;;\n                                      "     \
	"       make %[r] = %[e]                        \n"     \
	"       igoto %[r]                              \n"     \
	"       ;;\n                                      "     \
	"       .previous                               \n"     \
	"       .section __ex_table,\"a\"               \n"     \
	"       .align 8                                \n"     \
	"       .dword 1b,3b                            \n"     \
	"       .dword 2b,3b                            \n"     \
	"       .previous                               \n"     \
	: [r] "+r" (ret), [ov] "+r" (oldval)                   \
	: [u] "r" (uaddr),                                      \
	  [op] "r" (oparg), [e] "i" (-EFAULT)                   \
	: "r62", "r63", "memory");                              \
	__disable_user_access();                                \
}


static inline int
arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval, u32 __user *uaddr)
{
	int oldval = 0, ret = 0;

	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;
	switch (op) {
	case FUTEX_OP_SET: /* *(int *)UADDR = OPARG; */
		__futex_atomic_op("copyd $r62 = %[op]",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ADD: /* *(int *)UADDR += OPARG; */
		__futex_atomic_op("addw $r62 = $r63, %[op]",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_OR: /* *(int *)UADDR |= OPARG; */
		__futex_atomic_op("orw $r62 = $r63, %[op]",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_ANDN: /* *(int *)UADDR &= ~OPARG; */
		__futex_atomic_op("andnw $r62 = %[op], $r63",
				  ret, oldval, uaddr, oparg);
		break;
	case FUTEX_OP_XOR:
		__futex_atomic_op("xorw $r62 = $r63, %[op]",
				  ret, oldval, uaddr, oparg);
		break;
	default:
		ret = -ENOSYS;
	}

	if (!ret)
		*oval = oldval;

	return ret;
}

static inline int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
						u32 oldval, u32 newval)
{
	int ret = 0;
	if (!access_ok(uaddr, sizeof(u32)))
		return -EFAULT;
	__enable_user_access();
	__asm__ __volatile__ (
	"      fence                           \n"/* commit previous stores  */
	"      copyd $r63 = %[ov]              \n"/* init "expect" with ov   */
	"      copyd $r62 = %[nv]              \n"/* init "update" with nv   */
	"      ;;\n                              "
	"1:    acswapw 0[%[u]], $r62r63        \n"
	"      ;;\n                              "
	"      cb.dnez $r62? 3f                \n"/* if acswap ok -> return  */
	"      ;;\n                              "
	"2:    lws $r63 = 0[%[u]]              \n"/* fail -> load old value  */
	"      ;;\n                              "
	"      compw.ne $r62 = $r63, %[ov]     \n"/* check if equal to "old" */
	"      ;;\n                              "
	"      cb.deqz $r62? 1b                \n"/* if not equal, try again */
	"      ;;\n                              "
	"3:                                    \n"
	"      .section .fixup,\"ax\"          \n"
	"4:    make %[r] = 3b                  \n"
	"      ;;\n                              "
	"      make %[r] = %[e]                \n"
	"      igoto %[r]                      \n"/* goto 3b                 */
	"      ;;\n                              "
	"      .previous                       \n"
	"      .section __ex_table,\"a\"       \n"
	"      .align 8                        \n"
	"      .dword 1b,4b                    \n"
	"      .dword 2b,4b                    \n"
	".previous                             \n"
	: [r] "+r" (ret)
	: [ov] "r" (oldval), [nv] "r" (newval),
	  [e] "i" (-EFAULT), [u] "r" (uaddr)
	: "r62", "r63", "memory");
	__disable_user_access();
	*uval = oldval;
	return ret;
}

#endif
#endif /* _ASM_KVX_FUTEX_H */
