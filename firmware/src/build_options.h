/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared by the application and assembly startup. */
#ifndef CORE_FM1_BUILD_OPTIONS_H
#define CORE_FM1_BUILD_OPTIONS_H
#ifndef CFM1_DUAL_CORE
#define CFM1_DUAL_CORE 0
#endif
#ifndef MELODEE_DUAL_CORE
#define MELODEE_DUAL_CORE CFM1_DUAL_CORE
#endif
#ifndef MELODEE_CORE1_FALLBACK
#define MELODEE_CORE1_FALLBACK 0
#endif
#endif
