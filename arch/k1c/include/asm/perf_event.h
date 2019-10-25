/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2019 Kalray Inc.
 */

#ifndef _ASM_K1C_PERF_EVENT_H
#define _ASM_K1C_PERF_EVENT_H

#include <linux/perf_event.h>

/**
 * struct cpu_hw_events - per-cpu structure to describe PM resource usage
 * @n_events:	number of events currently existing
 * @events:	events[i] is the event using PMi. NULL if PMi is not used.
 */
struct cpu_hw_events {
	unsigned int n_events;
	struct perf_event **events;
};

enum k1c_pmc_ie {
	PMC_IE_DISABLED,
	PMC_IE_ENABLED
};

enum k1c_pm_idx {
	K1C_PM_1,
	K1C_PM_2,
	K1C_PM_3
};

enum k1c_pm_event_code {
	K1C_PM_PCC,
	K1C_PM_ICC,
	K1C_PM_EBE,
	K1C_PM_ENIE,
	K1C_PM_ENSE,
	K1C_PM_ICHE,
	K1C_PM_ICME,
	K1C_PM_ICMABE,
	K1C_PM_MNGIC,
	K1C_PM_MIMHE,
	K1C_PM_MIMME,
	K1C_PM_IATSC,
	K1C_PM_FE,
	K1C_PM_PBSC,
	K1C_PM_PNVC,
	K1C_PM_PSC,
	K1C_PM_TADBE,
	K1C_PM_TABE,
	K1C_PM_TBE,
	K1C_PM_MDMHE,
	K1C_PM_MDMME,
	K1C_PM_DATSC,
	K1C_PM_DCLHE,
	K1C_PM_DCHE,
	K1C_PM_DCLME,
	K1C_PM_DCME,
	K1C_PM_DARSC,
	K1C_PM_LDSC,
	K1C_PM_DCNGC,
	K1C_PM_DMAE,
	K1C_PM_LCFSC,
	K1C_PM_MNGDC,
	K1C_PM_MACC,
	K1C_PM_TACC,
	K1C_PM_IWC,
	K1C_PM_WISC,
	K1C_PM_SISC,
	K1C_PM_DDSC,
	K1C_PM_SC,
	K1C_PM_ELE,
	K1C_PM_ELNBE,
	K1C_PM_ELUE,
	K1C_PM_ELUNBE,
	K1C_PM_ESE,
	K1C_PM_ESNBE,
	K1C_PM_EAE,
	K1C_PM_CIRE,
	K1C_PM_CIE,
	K1C_PM_SE,
	K1C_PM_RE,
	K1C_PM_FSC,
	K1C_PM_MAX,
	K1C_PM_UNSUPPORTED = K1C_PM_MAX,
};

#endif /* _ASM_K1C_PERF_EVENT_H */
