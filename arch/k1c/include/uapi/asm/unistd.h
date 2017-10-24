/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Kalray Inc.
 */

#ifndef _UAPI_ASM_K1C_UNISTD_H
#define _UAPI_ASM_K1C_UNISTD_H

#include <asm-generic/unistd.h>

#define __IGNORE_lseek
#define __IGNORE_fadvise64
#define __IGNORE_mmap
#define __IGNORE_fcntl
#define __IGNORE_truncate
#define __IGNORE_ftruncate
#define __IGNORE_statfs
#define __IGNORE_fstatfs
#define __IGNORE_fstat
#define __IGNORE_sendfile

#endif	/* _UAPI_ASM_K1C_UNISTD_H */
