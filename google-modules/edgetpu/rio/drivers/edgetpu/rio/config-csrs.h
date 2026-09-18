/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Chip-dependent CSRs.
 *
 * Copyright (C) 2021 Google, Inc.
 */

/* Offsets from TPU TOP */

/* funcApbSlaves_cpuSecure_cpuSecure */
#define EDGETPU_REG_RESET_CONTROL                       0x190018 /* ResetControl */
#define EDGETPU_REG_SECURITY                            0x190060 /* Security */
#define EDGETPU_REG_INSTRUCTION_REMAP_CONTROL           0x190070 /* InstructionRemapControl */
#define EDGETPU_REG_INSTRUCTION_REMAP_LIMIT             0x190090 /* InstructionRemapLimit */
#define EDGETPU_REG_INSTRUCTION_REMAP_NEW_BASE          0x1900a0 /* InstructionRemapNewbase */
#define EDGETPU_REG_INSTRUCTION_REMAP_SECURITY          0x1900b0 /* InstructionRemapSecurity */

/* funcApbSlaves_lpm_lpm */
#define EDGETPU_PSM0_CFG				0x1c1700 /* psm_0_dmem_cfg */
#define EDGETPU_PSM0_START				0x1c1704 /* psm_0_dmem_start */
#define EDGETPU_PSM0_STATUS				0x1c1708 /* psm_0_dmem_status */

/* funcApbSlaves_tpuTop_tpuTop */
#define EDGETPU_REG_LPM_CONTROL                         0x1D0020 /* LpmControlCsr */
/* LpmControlCsr bits */
#define LPM_CTRL_LPMCTLPWRSTATE		BIT(0) /* lpmCtlPwrState */

#define EDGETPU_LPM_CORE_CSR				0x1d0028 /* LpmCoreCsr */
#define EDGETPU_LPM_CLUSTER_CSR0			0x1d0030 /* LpmClusterCsr0 */
#define EDGETPU_LPM_CLUSTER_CSR1			0x1d0038 /* LpmClusterCsr1 */
#define EDGETPU_TOP_CLOCK_GATE_CONTROL_CSR		0x1d0068 /* TopClockGateControlCsr */

/* funcApbSlaves_cpuNonSecure_cpuNonSecure */
#define EDGETPU_REG_CPUNS_TIMESTAMP			0x1a01c0 /* Timestamp */

/* funcApbSlaves_debugApbSlaves_apbaddr_dbg_0, 1 base addresses */
#define EDGETPU_REG_EXTERNAL_DEBUG_0_BASE		0x210000
#define EDGETPU_REG_EXTERNAL_DEBUG_1_BASE		0x310000

/* CSR offsets within external debug 0,1 */
#define EDGETPU_REG_EXTERNAL_DEBUG_PROGRAM_COUNTER      0x00a0 /* edpcsrlo */
#define EDGETPU_REG_EXTERNAL_DEBUG_OS_LOCK_ACCESS       0x0300 /* oslar_el1 */
#define EDGETPU_REG_EXTERNAL_DEBUG_PROCESSOR_STATUS     0x0314 /* edprsr */
#define EDGETPU_REG_EXTERNAL_DEBUG_LOCK_ACCESS          0x0fb0 /* edlar */
#define EDGETPU_REG_EXTERNAL_DEBUG_LOCK_STATUS          0x0fb4 /* edlsr */
#define EDGETPU_REG_EXTERNAL_DEBUG_AUTHSTATUS           0x0fb8 /* dbgauthstatus */

/* SYSREG TPU */
#define EDGETPU_SYSREG_TPU0_SHAREABILITY	0x700
#define EDGETPU_SYSREG_TPU1_SHAREABILITY	0x704
#define SHAREABLE_WRITE	(1 << 13)
#define SHAREABLE_READ	(1 << 12)
#define INNER_SHAREABLE	1
