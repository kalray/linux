/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2018 Kalray Inc.
 */

#ifndef _ASM_K1C_CACHEFLUSH_H
#define _ASM_K1C_CACHEFLUSH_H

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0

/*
 * FIXME AUTO: implement flush_dcache_page correctly.
 */
#define flush_dcache_page(page)                 do { } while (0)

/*
 * FIXME AUTO: implement flush_icache_range correctly.
 */
#define flush_icache_range(start, end) \
	do { \
		__builtin_k1_iinval(); \
		__builtin_k1_barrier(); \
	} while (0)

#define flush_icache_user_range(vma, pg, adr, len) do { } while (0)

#define flush_dcache_mmap_lock(mapping)         do { } while (0)
#define flush_dcache_mmap_unlock(mapping)       do { } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	do { \
		memcpy(dst, src, len); \
		flush_icache_user_range(vma, page, vaddr, len); \
	} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif	/* _ASM_K1C_CACHEFLUSH_H */
