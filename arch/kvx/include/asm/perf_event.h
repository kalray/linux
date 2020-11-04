/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019-2020 Kalray Inc.
 * Author: Yann Sionneau
 */

#ifndef _ASM_KVX_PERF_EVENT_H
#define _ASM_KVX_PERF_EVENT_H

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

enum kvx_pmc_ie {
	PMC_IE_DISABLED,
	PMC_IE_ENABLED
};

enum kvx_pm_idx {
	KVX_PM_1,
	KVX_PM_2,
	KVX_PM_3
};

enum kvx_pm_event_code {
	KVX_PM_PCC,
	KVX_PM_ICC,
	KVX_PM_EBE,
	KVX_PM_ENIE,
	KVX_PM_ENSE,
	KVX_PM_ICHE,
	KVX_PM_ICME,
	KVX_PM_ICMABE,
	KVX_PM_MNGIC,
	KVX_PM_MIMHE,
	KVX_PM_MIMME,
	KVX_PM_IATSC,
	KVX_PM_FE,
	KVX_PM_PBSC,
	KVX_PM_PNVC,
	KVX_PM_PSC,
	KVX_PM_TADBE,
	KVX_PM_TABE,
	KVX_PM_TBE,
	KVX_PM_MDMHE,
	KVX_PM_MDMME,
	KVX_PM_DATSC,
	KVX_PM_DCLHE,
	KVX_PM_DCHE,
	KVX_PM_DCLME,
	KVX_PM_DCME,
	KVX_PM_DARSC,
	KVX_PM_LDSC,
	KVX_PM_DCNGC,
	KVX_PM_DMAE,
	KVX_PM_LCFSC,
	KVX_PM_MNGDC,
	KVX_PM_MACC,
	KVX_PM_TACC,
	KVX_PM_IWC,
	KVX_PM_WISC,
	KVX_PM_SISC,
	KVX_PM_DDSC,
	KVX_PM_SC,
	KVX_PM_ELE,
	KVX_PM_ELNBE,
	KVX_PM_ELUE,
	KVX_PM_ELUNBE,
	KVX_PM_ESE,
	KVX_PM_ESNBE,
	KVX_PM_EAE,
	KVX_PM_CIRE,
	KVX_PM_CIE,
	KVX_PM_SE,
	KVX_PM_RE,
	KVX_PM_FSC,
	KVX_PM_MAX,
	KVX_PM_UNSUPPORTED = KVX_PM_MAX,
};

#endif /* _ASM_KVX_PERF_EVENT_H */
