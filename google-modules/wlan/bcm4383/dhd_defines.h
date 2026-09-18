/*
* Broadcom Dongle Host Driver (DHD),
* Linux-specific network interface for transmit(tx) path
*
* Copyright (C) 2026, Broadcom.
*
*      Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2 (the "GPL"),
* available at http://www.broadcom.com/licenses/GPLv2.php, with the
* following added to such license:
*
*      As a special exception, the copyright holders of this software give you
* permission to link this software with independent modules, and to copy and
* distribute the resulting executable under terms of your choice, provided that
* you also meet, for each linked independent module, the terms and conditions of
* the license of that module.  An independent module is a module which is not
* derived from this software.  The special exception does not apply to any
* modifications of the software.
*
*
* <<Broadcom-WL-IPTag/Dual:>>
*
*/
/* FILE-CSTYLED */

#ifndef _DHD_DEFINES_H_
#define _DHD_DEFINES_H_

/* For pixel build, unset SOC related configs which are defined in DHD */
#if IS_ENABLED(CONFIG_SOC_GOOGLE) || IS_ENABLED(CONFIG_SYNAPTICS_SOC)
	#undef CONFIG_ARCH_HISI
	#undef CONFIG_ARCH_BRCMSTB
#endif

/* Force disable CONFIG_BCMDHD for Syna SDK */
#if IS_ENABLED(CONFIG_SYNAPTICS_SOC)
	#undef CONFIG_BCMDHD
	#undef CONFIG_BCMDHD_MODULE
#endif

#define BCMDHD_MODULE 1
#define BCMDHD_BUILTIN 2

/* #################### */
/* SDIO/PCIe Basic feature */
/* #################### */
/* For inbuilt module, below configs will be provided via defconfig */
/* But for out-of-tree module, explicitly define them here and add */
/* them as cflags */
#if !IS_ENABLED(CONFIG_BCMDHD)
	#define CONFIG_BCMDHD BCMDHD_MODULE
	#define CONFIG_BCMDHD_PCIE 1
	#define CONFIG_DHD_OF_SUPPORT 1
	#if defined(GG_REF_PLATFORM)
		#define CONFIG_BCM4390 1
		#define CONFIG_BCM4398 1
		#define CONFIG_BCM4383 1
		#define CONFIG_BCM4389 1
	#else
		#if IS_ENABLED(CONFIG_SOC_GOOGLE)
			#if defined(BCMDHD) && (BCMDHD == 4383)
				#define CONFIG_BCM4383 1
			#elif defined(BCMDHD) && (BCMDHD == 4390)
				#define CONFIG_BCM4390 1
				#undef CONFIG_GOOGLE_DAL_CORE
				#undef CONFIG_GOOGLE_DAL_NOA_MODE
			#else
				#define CONFIG_BCM4398 1
			#endif
		#else
			#define CONFIG_BCM4390 1
			#define CONFIG_BCM4398 1
			#define CONFIG_BCM4383 1
			#define CONFIG_BCM4389 1
		#endif
	#endif
	#define CONFIG_BROADCOM_WIFI_RESERVED_MEM 1
	#define CONFIG_DHD_USE_STATIC_BUF 1
	#define CONFIG_DHD_USE_SCHED_SCAN 1
	#define CONFIG_DHD_SET_RANDOM_MAC_VAL 0x001A11
	#define CONFIG_WLAN_REGION_CODE 100
	#define CONFIG_WLAIBSS 1
	#define CONFIG_WL_RELMCAST 1
	#define CONFIG_BCMDHD_PREALLOC_MEMDUMP 1
	#define CONFIG_BCMDHD_OOB_HOST_WAKE 1
	#define CONFIG_BCMDHD_GET_OOB_STATE 1
	#if IS_ENABLED(CONFIG_GOOGLE_DAL_CORE)
		#define GOOGLE_DAL_CORE
		#if IS_ENABLED(CONFIG_GOOGLE_DAL_NOA_MODE)
			#define GOOGLE_DAL_NOA_MODE
		#endif
	#endif
#endif /* CONFIG_BCMDHD */
#define BCMUTILS_ERR_CODES
#define BCM_FLEX_ARRAY // to do needs review? how does = translates to
#define USE_NEW_RSPEC_DEFS
#if IS_ENABLED(CONFIG_DEBUG_KMEMLEAK)
	#define DHD_DUMP_RXPKTIDMAP
	/* #define DHD_PRINT_RXPKTS_TRACE */
#endif
#define CHIPC_NEW_ACCESS_MACROS
#define PCIE_NEW_ACCESS_MACROS
#define PMU_NEW_ACCESS_MACROS
#define SR_NEW_ACCESS_MACROS
#define ARP_OFFLOAD_SUPPORT
#define BCMDMA32
#define BCMDONGLEHOST
#define BCMDRIVER
#define BCMFILEIMAGE
#define DHDTHREAD
#define DHD_DUMP_FILE_WRITE_FROM_KERNEL
#define DHD_FW_COREDUMP
#define DHD_USE_RANDMAC
#define EMBEDDED_PLATFORM
#define GET_CUSTOM_MAC_ENABLE
#define KEEP_ALIVE
#define LINUX
#define PNO_SUPPORT
#define SEC_ENHANCEMENT
#define SHOW_EVENTS
#define SHOW_LOGTRACE
#define WIFI_ACT_FRAME
#define WLP2P
#define WL_BW160MHZ
#define WL_BW320MHZ
#define WL_P2P_USE_RANDMAC
#define OEM_ANDROID
#define DHD_COREDUMP
/* ################ */
/* Common feature */
/* ################ */
#define WL_VIRTUAL_APSTA
#define DHD_EXPORT_CNTL_FILE
#define EWP_ECNTRS_LOGGING
#define EWP_RTT_LOGGING
#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
	/* Resume delay WAR not required for P26 platform */
	#if !(IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_BCM4390))
		#define DHD_PCIE_RESUME_DELAY_WAR
	#endif /* CONFIG_SOC_MBU */
	#define DHD_LINUX_STD_FW_API
	#define FW_SIGNATURE
	#define BL_HEAP_START_GAP_SIZE 0x1000
	#define BL_HEAP_SIZE 0x10000
	#define EWP_EDL
	#define EWP_DACS
	#define EWP_EVENTTS_LOG
	#define EVENT_LOG_RATE_HC
#endif
#if IS_ENABLED(CONFIG_ARCH_MESON)
        #undef DHD_LINUX_STD_FW_API
        #define DHD_SUPPORT_VFS_CALL
        #define CONFIG_BCMDHD_FW_PATH "/vendor/etc/wifi/fw_bcmdhd.bin"
        #define CONFIG_BCMDHD_NVRAM_PATH "/vendor/etc/wifi/bcmdhd.cal"
        #define CONFIG_BCMDHD_CLM_PATH "/vendor/etc/wifi/bcmdhd_clm.blob"
        #define CONFIG_BCMDHD_TXCAP_PATH "/vendor/etc/wifi/bcmdhd_txcap.blob"
        #define CONFIG_BCMDHD_MAP_PATH "/vendor/etc/wifi/fw_bcmdhd.map"
#else
        /* if DHD_LINUX_STD_FW_API defined add only file names else add full path */
        #if defined(DHD_LINUX_STD_FW_API)
                #define DHD_FW_NAME "fw_bcmdhd.bin"
                #define DHD_NVRAM_NAME "bcmdhd.cal"
                #define DHD_CLM_NAME "bcmdhd_clm.blob"
                #define DHD_MAP_NAME "fw_bcmdhd.map"
                #define DHD_TXCAP_NAME "bcmdhd_txcap.blob"
        #else
                #define CONFIG_BCMDHD_FW_PATH "/vendor/firmware/fw_bcmdhd.bin"
                #define CONFIG_BCMDHD_NVRAM_PATH "/vendor/firmware/bcmdhd.cal"
                #define CONFIG_BCMDHD_CLM_PATH "/vendor/firmware/bcmdhd_clm.blob"
                #define CONFIG_BCMDHD_TXCAP_PATH "/vendor/firmware/bcmdhd_txcap.blob"
                #define CONFIG_BCMDHD_MAP_PATH "/vendor/firmware/fw_bcmdhd.map"
        #endif
#endif
/* Enable wakelock for legacy scan */
#define DHD_USE_SCAN_WAKELOCK
/* Enable wakelock debug function */
#define DHD_TRACE_WAKE_LOCK
/* Support of power stats in sysfs */
#define PWRSTATS_SYSFS
/* Enable SBN feature */
#define DHD_SBN
/* Enable inband device wake feature */
#define PCIE_INB_DW
/* Prioritize ARP */
#define PRIORITIZE_ARP
/* Debug check for PCIe read latency */
/* #define DBG_DW_CHK_PCIE_READ_LATENCY */
/* Hikey sched is not so optimized and hence need a higher timeout */
#define WAIT_FOR_DISCONNECT_MAX 20
/* static if */
#define WL_STATIC_IF
/* static NMI interface support */
/* #define WL_STATIC_NMI_IF */
/* Wapi */
/* #define BCMWAPI_WPI */
/* #define BCMWAPI_WAI */
/* FBT */
#define WLFBT
/* OKC */
#define OKC_SUPPORT
#define WL_CFG80211
/* Android iface management */
#define WL_IFACE_MGMT
/* Debug flag */
#define WL_IFACE_MGMT_CONF
/* Enable MLO related code. MLO functionality will be exercised */
/* only if chip supports it. */
#define WL_MLO
/* #define WL_MLO_AP */
/* Debug flag */
#define RTT_GEOFENCE_INTERVAL
/* Debug flag */
#define RTT_GEOFENCE_CONT
/* Debug flag */
#if IS_ENABLED(CONFIG_FIB_RULES)
#define HAL_DEBUGABILITY
	#if IS_ENABLED(CONFIG_SOC_GOOGLE)
		#define DEBUGABILITY
		#define DEBUGABILITY_DISABLE_MEMDUMP
		#define DHD_DEBUGABILITY_LOG_DUMP_RING
		#define DHD_PKT_LOGGING_DBGRING
		#define DHD_ECNTRS_EXPOSED_DBGRING
	#endif
#else
	#define DHD_FW_COREDUMP
#endif
#define DHD_DUMP_BUF_KVMALLOC
#define CUSTOMER_DBG_SYSTEM_TIME
#define CUSTOMER_DBG_PREFIX_ENABLE
#define LOG_CUSTOM_PREFIX_AND_RTC "[dhd][wlan]"
#define DHD_LOGLEVEL
/* SCAN TYPES, if kernel < 4.17 ..back port support required */
#if IS_ENABLED(CONFIG_CFG80211_SCANTYPE_BKPORT)
	#define WL_SCAN_TYPE
#endif
/* Print out kernel panic point of file and line info when assertion happened */
#define BCMASSERT_LOG
/* Enable Log Dump */
#define DHD_LOG_DUMP
/* Enable log print rate limit */
#define DHD_LOG_PRINT_RATE_LIMIT
/* Block ARP during DHCP on STA/SoftAP concurrent mode */
#define APSTA_BLOCK_ARP_DURING_DHCP
/* Bypass wpa_supplicant's BSSID selection */
#define WL_SKIP_CONNECT_HINTS
/* Dynamic indoor, DFS policy */
#define WL_DYNAMIC_CHAN_POLICY
#define WL_DYNAMIC_CHAN_POLICY_INDOOR
#define WL_DYNAMIC_CHAN_POLICY_DFS
/* Keep P2P DFS Skip logic disabled for using dynamic DFS policy */
/* #define P2P_SKIP_DFS */
#if IS_ENABLED(CONFIG_SOC_LGA)
	#if IS_ENABLED(CONFIG_BCM4383)
		/* allow SCC for AP/GO in 165/20 */
		#define WL_UNII4_CHAN_SCC
	#endif
#endif

#if IS_ENABLED(CONFIG_ARCH_MESON)
	#define BOARD_MODULAR_INIT
#endif

#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
	#define WLAN_ACCEL_BOOT
	/* Enable Tx checksum offloads */
	#define TX_CSO
	/* Enable Rx checksum offloads */
	#define RX_CSO
	/* Aggregated H2D Doorbell */
	#define AGG_H2D_DB
	/* Use spin_lock_bh locks */
	#define DHD_USE_SPIN_LOCK_BH
	/* Enable SSSR Dump */
	#define DHD_SSSR_DUMP
	/* Enable System Debug Trace Controller, Embedded Trace Buffer */
	#define DHD_SDTC_ETB_DUMP
	/* Enable SMD/Minidump collection */
	#define D2H_MINIDUMP
	/* ROT and Scan timeout debugging due to Kernel scheduling problem */
	#define DHD_KERNEL_SCHED_DEBUG
	/* Enable CTO Recovery */
	#define BCMPCIE_CTO_PREVENTION
	/* no reset during dhd attach */
	#define DHD_SKIP_DONGLE_RESET_IN_ATTACH
	/* Dongle Isolation will ensure no resets devreset ON/OFF */
	#define DONGLE_ENABLE_ISOLATION
	/* Quiesce dongle using DB7 trap */
	#define DHD_DONGLE_TRAP_IN_DETACH
	/* Dongle reset during Wifi ON to keep in sane state */
	#define FORCE_DONGLE_RESET_IN_DEVRESET_ON
	/* Perform Backplane Reset else FLR will happen */
	/* #define DHD_USE_BP_RESET_SS_CTRL */
	#define DBG_PRINT_AMNI
	/* Memory consumed by DHD */
	#define DHD_MEM_STATS
	/* Check trap in the case of ROT */
	#define CHECK_TRAP_ROT
	/* Enable Host SFH LLC insertion in Tx pkts */
	#define HOST_SFH_LLC
	/* Enable PKTID AUDIT */
	/* Not required for the customer platform due to overhead */
	/* Not required for STB platform also, Enabled only for hikey. */
	#if IS_ENABLED(CONFIG_ARCH_HISI)
		#define DHD_PKTID_AUDIT_ENABLED
	#elif IS_ENABLED(CONFIG_ARCH_MESON)
		/* Enable MSI support for vim3 */
		#define DHD_MSI_SUPPORT
		#if IS_ENABLED(CONFIG_FIB_RULES)
			/* Debugability */
			/* HAL File dump is supported only for iptable builds(brcm_wlan_iptables_defconfig) */
			#define DHD_FILE_DUMP_EVENT
			#undef DHD_DUMP_FILE_WRITE_FROM_KERNEL /* Removed by filter-out */
		#endif
	#endif
	/* Disable MONITOR and ART for Astra */
	#if !IS_ENABLED(CONFIG_SYNAPTICS_SOC)
		/* Support Monitor Mode */
		#define WL_MONITOR
		#define WL_CFG80211_MONITOR
		/* Active Radio tap */
		#define DHD_ART
		#if IS_ENABLED(CONFIG_SOC_MBU) || IS_ENABLED(CONFIG_BCM4383)
			#define WONDERTAP
		#endif
		/* Undef WONDERTAP if wonder SDK dir is absent as per Kbuild */
		#ifdef FORCE_DISABLE_WONDERTAP
			#undef WONDERTAP
		#endif
	#endif
	/* Not enabled for the platform due to overhead */
	#if !(IS_ENABLED(CONFIG_ARCH_BRCMSTB) || IS_ENABLED(CONFIG_SYNAPTICS_SOC))
		/* Enable pktid logging */
		#define DHD_MAP_PKTID_LOGGING
		/* Flow ring status trace in ISR and DPC */
		#define DHD_FLOW_RING_STATUS_TRACE
		#define DHD_MMIO_TRACE
	#endif
	/* Enable internal Rx packet pool */
	#define RX_PKT_POOL
	/* Enable Load Balancing support by default. */
	/* DHD_LB_RXP - Perform RX Packet processing in parallel, default enabled */
	/* DHD_LB_TXP - Perform TX Packet processing in parallel, default disabled, */
	/* enabled using DHD_LB_TXP_DEFAULT_ENAB */
	/* DHD_LB_STATS - To display the Load Blancing statistics */
	#define DHD_LB
	#define DHD_LB_RXP
	#define DHD_LB_TXP
	#define DHD_LB_STATS
	#if IS_ENABLED(CONFIG_ARCH_BRCMSTB)
		#define DHD_LB_CPU_SET8 0x000
		#define DHD_LB_CPU_SET4 0x000
		#define DHD_LB_CPU_SET0 0x00E
		#define DHD_LB_RXPOST
		#define DHD_LB_CANDIDACY_OVERRIDE
	#elif IS_ENABLED(CONFIG_SYNAPTICS_SOC)
		#define DHD_LB_CPU_SET8 0x000
		#define DHD_LB_CPU_SET4 0x000
		#define DHD_LB_CPU_SET0 0x00E
		#define DHD_LB_CANDIDACY_OVERRIDE
		#define DHD_TCP_LIMIT_OUTPUT
		#define DHD_TCP_PACING_SHIFT
	#elif IS_ENABLED(CONFIG_SOC_LGA)
		#if IS_ENABLED(CONFIG_BCM4390)
			#define DHD_LB_CPU_SET8 0x100
			#define DHD_LB_CPU_SET4 0x0F0
			#define DHD_LB_CPU_SET0 0x00E
		#elif IS_ENABLED(CONFIG_BCM4383)
			#define DHD_LB_CPU_SET8 0x100
			#define DHD_LB_CPU_SET4 0x07C
			#define DHD_LB_CPU_SET0 0x002
		#endif
	#elif IS_ENABLED(CONFIG_SOC_ZUMAPRO)
		#if IS_ENABLED(CONFIG_BCM4390)
			#define DHD_LB_CPU_SET8 0x100
			#define DHD_LB_CPU_SET4 0x0F0
			#define DHD_LB_CPU_SET0 0x00E
		#elif IS_ENABLED(CONFIG_BCM4383)
			#define DHD_LB_CPU_SET8 0x100
			#define DHD_LB_CPU_SET4 0x070
			#define DHD_LB_CPU_SET0 0x00E
		#endif
	#elif IS_ENABLED(CONFIG_PCI_EXYNOS_GS)
		#if IS_ENABLED(CONFIG_BCM4383)
			#define DHD_LB_CPU_SET8 0x100
			#define DHD_LB_CPU_SET4 0x0F0
			#define DHD_LB_CPU_SET0 0x00E
		#endif
	#else
		#define DHD_LB_CPU_SET8 0x100
		#define DHD_LB_CPU_SET4 0x0F0
		#define DHD_LB_CPU_SET0 0x00E
	#endif
	/* GRO (Generic Receive Offload) feature */
	#define ENABLE_DHD_GRO
	/* WLAN-BT Regon coordinator */
	#define WBRC
	#define WBRC_WLAN_ON_FIRST_ALWAYS
	#if IS_ENABLED(CONFIG_BCM4390)
		#define WBRC_HW_QUIRKS
		#define COEX_CPU
	#endif
	/* FW, NVRAM, CLM load based on VID module string, chipid and chiprev */
	#define SUPPORT_MULTIPLE_REVISION
	#define SUPPORT_MULTIPLE_REVISION_MAP
	#define SUPPORT_MIXED_MODULES
	#define USE_CID_CHECK
	#define SUPPORT_MULTIPLE_CHIPS
	#define CONCAT_DEF_REV_FOR_NOMATCH_VID
	#if IS_ENABLED(CONFIG_SOC_GOOGLE)
		/* Tasklet load detection and balancing */
		#define RESCHED_CNT_CHECK_PERIOD_SEC 2
		#define AFFINITY_UPDATE_MIN_PERIOD_SEC 6
		#if IS_ENABLED(CONFIG_SOC_MBU)
			#if IS_ENABLED(CONFIG_BCM4390)
				#define PKT_COUNT_HIGH 60000
				#define PKT_COUNT_MID 5000
				#define PKT_COUNT_LOW 3000
			#elif IS_ENABLED(CONFIG_BCM4383)
				#define DHD_CUSTOM_PKT_COUNT_ENABLE
				#define PKT_COUNT_HIGH 50000
				#define PKT_COUNT_MID 5000
				#define PKT_COUNT_LOW 3000
				#define DHD_CPUFREQ_MID 2u
				#define DHD_CPUFREQ_BIG 6u
				#define DHD_LITTLE_CORE_PERF_FREQ 1920000u
				#define DHD_MID_CORE_PERF_FREQ 2649000u
				#define DHD_BIG_CORE_PERF_FREQ 3340000u
			#endif
		#elif IS_ENABLED(CONFIG_SOC_LGA)
			#if IS_ENABLED(CONFIG_BCM4390)
				#define PKT_COUNT_HIGH 60000
				#define PKT_COUNT_MID 5000
				#define PKT_COUNT_LOW 3000
			#elif IS_ENABLED(CONFIG_BCM4383)
				#define DHD_CUSTOM_PKT_COUNT_ENABLE
				#define PKT_COUNT_HIGH 50000
				#define PKT_COUNT_MID 5000
				#define PKT_COUNT_LOW 3000
				#define DHD_CPUFREQ_MID 2u
				#define DHD_CPUFREQ_MID2 5u
				#define DHD_CPUFREQ_BIG 7u
				#define DHD_LITTLE_CORE_PERF_FREQ 1881600u
				#define DHD_MID_CORE_PERF_FREQ 2515200u
				#define DHD_BIG_CORE_PERF_FREQ 3052800u
			#endif
		#elif IS_ENABLED(CONFIG_SOC_ZUMAPRO)
			#if IS_ENABLED(CONFIG_BCM4390)
				#define PKT_COUNT_HIGH 60000
				#define PKT_COUNT_MID 5000
				#define PKT_COUNT_LOW 3000
			#elif IS_ENABLED(CONFIG_BCM4383)
				#define DHD_CUSTOM_PKT_COUNT_ENABLE
				#define PKT_COUNT_HIGH 50000
				#define PKT_COUNT_MID 5000
				#define PKT_COUNT_LOW 3000
				#define DHD_CPUFREQ_MID 4u
				#define DHD_CPUFREQ_BIG 7u
				#define DHD_LITTLE_CORE_PERF_FREQ 1849000u
				#define DHD_MID_CORE_PERF_FREQ 2450000u
				#define DHD_BIG_CORE_PERF_FREQ 3015000u
			#endif
		#elif IS_ENABLED(CONFIG_PCI_EXYNOS_GS)
			#if IS_ENABLED(CONFIG_BCM4390)
				#define PKT_COUNT_HIGH 60000
				#define PKT_COUNT_MID 5000
				#define PKT_COUNT_LOW 3000
			#elif IS_ENABLED(CONFIG_BCM4383)
				#define RESCHED_STREAK_MAX_HIGH 20
				#define RESCHED_STREAK_MAX_LOW 2
				#define DHD_CPUFREQ_MID 4u
				#define DHD_CPUFREQ_BIG 8u
				#define DHD_LITTLE_CORE_PERF_FREQ 0u
				#define DHD_MID_CORE_PERF_FREQ 0u
				#define DHD_BIG_CORE_PERF_FREQ 0u
			#elif IS_ENABLED(CONFIG_BCM4398)
				/* Boost host cpufreq to max for peak tput. default is false */
				#define DHD_HOST_CPUFREQ_BOOST
				/* Boost host cpufreq to max for peak tput. default is true */
				#define DHD_HOST_CPUFREQ_BOOST_DEFAULT_ENAB
				#define PKT_COUNT_HIGH 60000
				#define PKT_COUNT_MID 5000
				#define PKT_COUNT_LOW 3000
				#define RESCHED_STREAK_MAX_HIGH 20
				#define RESCHED_STREAK_MAX_LOW 2
			#endif
		#endif
		#define CLEAN_IRQ_AFFINITY_HINT
		#define WAKEUP_KSOFTIRQD_POST_NAPI_SCHEDULE
		#if IS_ENABLED(CONFIG_SOC_ZUMAPRO) || IS_ENABLED(CONFIG_SOC_LGA)
			#define IRQ_AFFINITY_SMALL_CORE 6
			#define IRQ_AFFINITY_BIG_CORE 7
		#elif IS_ENABLED(CONFIG_PCI_EXYNOS_GS)
			#define IRQ_AFFINITY_SMALL_CORE 7
			#define IRQ_AFFINITY_BIG_CORE 8
		#else
			#define IRQ_AFFINITY_SMALL_CORE 0
			#define IRQ_AFFINITY_BIG_CORE 0
		#endif
		#if IS_ENABLED(CONFIG_BCM4390)
			#define CPU_IRQ_AFFINITY
		#endif
		#define DHD_BUS_BUSY_TIMEOUT 5000
		/* MSI supported in GOOGLE SOC */
		#define DHD_MSI_SUPPORT
		/* Tx/Rx tasklet bounds */
		/* Currently these bounds will be taken default value from the code */
		/* These need to be tuned per platform to reduce DPC time without */
		/* tput regression */
		#if IS_ENABLED(CONFIG_BCM4398)
			#define DHD_TX_CPL_BOUND 64
		#elif IS_ENABLED(CONFIG_BCM4383)
			#define DHD_TX_CPL_BOUND 64
			#define DHD_CHECK_CTO_FROM_ISR
		#else
			#define DHD_TX_CPL_BOUND 1024
		#endif
		#define DHD_TX_POST_BOUND 128
		#define DHD_RX_CPL_POST_BOUND 96
		#define DHD_CTRL_CPL_POST_BOUND 16
		#define DHD_LB_TXBOUND 32
		/* Detect NON DMA M2M corruption (MFG only) */
		#define DHD_NON_DMA_M2M_CORRUPTION
		/* Detect FW Memory Corruption (MFG only) */
		#define DHD_FW_MEM_CORRUPTION
		/* Recover timeouts */
		#define DHD_RECOVER_TIMEOUT
		#if defined(BCMDHD) && (BCMDHD == 4398)
			/* PCIE CPL TIMEOUT WAR */
			/* #define DHD_TREAT_D3ACKTO_AS_LINKDWN */
		#endif
		#if IS_ENABLED(CONFIG_SOC_LGA)
			#if defined(BCMDHD) && (BCMDHD == 4383)
				#define DHD_TREAT_D2H_CTO_AS_LINKDOWN
				#define DHD_DEFER_L1SS_ENABLE_IN_RESUME
			#endif
		#endif
		/* Skip xorcsum for high throughput case */
		#define DHD_SKIP_XORCSUM_HIGH_TPUT
		/* Schedule NAPI directly on same cpu for Low TPUT */
		/* Enable this only after next 4390 project tput is stabilized */
		/* #define DHD_SCHED_NAPI_DIRECTLY_LOW_TPUT */
		/* Skip coredump for certain health check traps */
		#define DHD_SKIP_COREDUMP_ON_HC
		/* Skip coredump for older chip revs */
		#define DHD_SKIP_COREDUMP_OLDER_CHIPS
		/* Skip coredump for continousy pkt drop health check */
		#define SKIP_COREDUMP_PKTDROP_RXHC
		#if IS_ENABLED(CONFIG_PCI_EXYNOS_GS) || IS_ENABLED(CONFIG_SOC_LGA)
			/* Boost host cpufreq to max for peak tput. default is false */
			#define DHD_HOST_CPUFREQ_BOOST
			/* Boost host cpufreq to max for peak tput. default is true */
			#define DHD_HOST_CPUFREQ_BOOST_DEFAULT_ENAB
		#endif
		/* Force all CPUs to run at MAX frequencies */
		/* #define DHD_FORCE_MAX_CPU_FREQ */
					/* Support L1SS */
		#if IS_ENABLED(CONFIG_BCM4390)
			#define DHD_SUPPORT_L1SS
		#elif IS_ENABLED(CONFIG_BCM4383)
			/* not enable l1ss for older 4383 products same as production branch */
			#if !IS_ENABLED(CONFIG_PCI_EXYNOS_GS)
				#define DHD_SUPPORT_L1SS
			#endif
		#endif
	#endif
#endif /* # CONFIG_BCMDHD_PCIE */
#if IS_ENABLED(CONFIG_SOC_GOOGLE)
	#define DHD_FILE_DUMP_EVENT
	#define DHD_HAL_RING_DUMP
	/* Pixel platform only, to support ring data flushing properly */
	#define DHD_DUMP_START_COMMAND
	/* MLO related back port changes */
	#define WL_MLO_BKPORT
	/* TDI policy kernel back port changes */
	#define WL_MLO_BKPORT_NEW_PORT_AUTH
	/* CROSS AKM related back port changes */
	#define WL_CROSS_AKM_BKPORT
	#if IS_ENABLED(CONFIG_SOC_LGA)
		/* Avoid SSR dump on state mismatch */
		#define DHD_AVOID_SSR_ON_STATE_MISMATCH
	#endif
	/* Disable NAN pairing for rel builds till associated sec is supported */
	#define DISABLE_NAN_PAIRING
	/* ch_switch_notify back port changes */
	#define WL_CH_SWITCH_BKPORT
	/* External auth request back port changes */
	/* #define WL_EXT_AUTH_BKPORT */
	#undef DHD_DUMP_FILE_WRITE_FROM_KERNEL /* Removed by filter-out */
#else
	/* internal platform only required below */
	#define DHD_SSSR_DUMP_BEFORE_SR
	#define SUPPORT_WL_TXPOWER
#endif
/* CUSTOMER2 flags */
/* Basic / Common Feature */
#define USE_WL_FRAMEBURST
#define USE_WL_TXBF
#define SOFTAP_UAPSD_OFF
#define VSDB
#define WL_CFG80211_STA_EVENT
#if IS_ENABLED(CONFIG_CFG80211_FILS_BKPORT)
	#define WL_FILS
#endif
	#if IS_ENABLED(CONFIG_CFG80211_FILS_ROAM_BKPORT)
	#define WL_FILS_ROAM_OFFLD
#endif
/* Android Feature */
#define APF
#define WL_APF_PROGRAM_MAX_SIZE 4096
#define DHD_GET_VALID_CHANNELS
#define LINKSTAT_SUPPORT
#define LINKSTAT_EXT_SUPPORT
#define PFN_SCANRESULT_2
#define WL_IFACE_COMB_NUM_CHANNELS
/* Scheduled scan (PNO) */
#define WL_SCHED_SCAN
/* FW ROAM control */
#define ROAMEXP_SUPPORT
/* Skip supplicant bssid and channel hints */
#define WL_SKIP_CONNECT_HINTS
/* Phy / System */
#define CUSTOM_SET_OCLOFF
#define DHD_ENABLE_LPC
#define DISABLE_PM_BCNRX
#define FCC_PWR_LIMIT_2G
#define SUPPORT_2G_VHT
#define SUPPORT_5G_1024QAM_VHT
#define SUPPORT_LTECX
#define SUPPORT_LQCM
#define SUPPORT_SET_CAC
/* Roaming feature */
#define DHD_LOSSLESS_ROAMING
#define ENABLE_FW_ROAM_SUSPEND
#define ROAM_API
#define ROAM_AP_ENV_DETECTION
#define ROAM_CHANNEL_CACHE
#define ROAM_ENABLE
#define SKIP_ROAM_TRIGGER_RESET
#define WBTEXT
#define WBTEXT_BTMDELTA 0
#define WBTEXT_SCORE_V2
#define RRM_BCNREQ_MAX_CHAN_TIME 12
#define WL_LASTEVT
#define ROAM_EVT_DISABLE
/* SOFTAP ACS */
#define WL_SOFTAP_ACS
/* Wake */
#define CONFIG_HAS_WAKELOCK
#define DHD_WAKE_EVENT_STATUS
#define DHD_WAKE_RX_STATUS
#define CUSTOM_WAKE_REASON_STATS
#define DHD_WAKEPKT_SET_MARK
/* Android Q */
#define WL_USE_RANDOMIZED_SCAN
#define STA_RANDMAC_ENFORCED
/* Connected MAC randomization */
#define WL_STA_ASSOC_RAND
/* Soft AP MAC randomization */
#define WL_SOFTAP_RAND
/* p2p MAC randomization */
#define WL_P2P_RAND
/* Custom Mapping of DSCP to User Priority */
#define WL_CUSTOM_MAPPING_OF_DSCP
#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
	#define DHD_WAKE_STATUS
#endif
#define ENABLE_BCN_LI_BCN_WAKEUP
/* Hang */
#define DHD_HANG_SEND_UP_TEST
#define DHD_USE_EXTENDED_HANG_REASON
#define PREVENT_REOPEN_DURING_HANG
#define SUPPORT_LINKDOWN_RECOVERY
#define SUPPORT_TRIGGER_HANG_EVENT
/* Logging */
#define BCMASSERT_LOG
#define DHD_8021X_DUMP
#define DHD_DHCP_DUMP
#define DHD_ICMP_DUMP
#define DHD_ARP_DUMP
#define DHD_DNS_DUMP
#define DHD_PKT_LOGGING
#define DHD_PKTDUMP_ROAM
#define DHD_RANDMAC_LOGGING
#define DHD_STATUS_LOGGING
#define DHD_WAKEPKT_DUMP
#define RSSI_MONITOR_SUPPORT
#define SET_SSID_FAIL_CUSTOM_RC 100
#define DHD_EVENT_LOG_FILTER
#define WL_CFGVENDOR_SEND_HANG_EVENT
/* Packet */
#define BLOCK_IPV6_PACKET
/* #define DHD_DONOT_FORWARD_BCMEVENT_AS_NETWORK_PKT # NAN test failure */
#define PASS_ALL_MCAST_PKTS
#define PKTPRIO_OVERRIDE
#define NDO_CONFIG_SUPPORT
/* Kernel/Platform Related Feature */
/* #define DHD_LB_TXP_DEFAULT_ENAB # Not needed for Brix */
/* #define DHD_RECOVER_TIMEOUT # Not needed for Brix */
/* #define DHD_USE_ATOMIC_PKTGET # Not needed for Brix */
/* #define DHD_USE_COHERENT_MEM_FOR_RING # Not needed for Brix */
/* #define DONGLE_ENABLE_ISOLATION # Not compatible with Brix platform */
/* #define KEEP_WIFION_OPTION # Not compatible with Brix platform */
#define WAIT_DEQUEUE
#define WL_SUPPORT_BACKPORTED_KPATCHES
/* SoftAP */
#define SUPPORT_AP_HIGHER_BEACONRATE
#define SUPPORT_AP_RADIO_PWRSAVE
#define SUPPORT_HIDDEN_AP
#define SUPPORT_SOFTAP_SINGL_DISASSOC
#define WL_SUPPORT_AUTO_CHANNEL
#define SUPPORT_SOFTAP_WPAWPA2_MIXED
/* P2P */
#define P2P_LISTEN_OFFLOADING
/* SCAN */
#define CUSTOMER_SCAN_TIMEOUT_SETTING
#define DISABLE_PRUNED_SCAN
#define ESCAN_BUF_OVERFLOW_MGMT
#define SUPPORT_RANDOM_MAC_SCAN
#define USE_INITIAL_SHORT_DWELL_TIME
#define WL_CFG80211_VSDB_PRIORITIZE_SCAN_REQUEST
#define CUSTOM_SCAN_UNASSOC_ACTIVE_TIME 40
#define CUSTOM_SCAN_PASSIVE_TIME 110
/* Suspend/Resume */
#define ENABLE_MAX_DTIM_IN_SUSPEND
#define SUPPORT_DEEP_SLEEP
/* Misc Features */
#define DHD_BLOB_EXISTENCE_CHECK
#define SUPPORT_PM2_ONLY
#define SUPPORT_AMPDU_MPDU_CMD
#define WL_RELMCAST
#define WL_SUPP_EVENT
#define DISABLE_WL_FRAMEBURST_SOFTAP
#define FILTER_IE
#define CUSTOM_LONG_RETRY_LIMIT 12
#define DHD_POST_EAPOL_M1_AFTER_ROAM_EVT
#define ROAMEXP_SUPPORT
#define CUSTOM_BSSID_BLACKLIST_NUM 16
#define CUSTOM_SSID_WHITELIST_NUM 16
#if IS_ENABLED(CONFIG_SOC_LGA)
	#define DHD_FORCE_WLREGON_CTRL
#endif
/* Kind of WAR */
#define ENABLE_TDLS_AUTO_MODE
#define EXPLICIT_DISCIF_CLEANUP
#define SKIP_WLFC_ON_CONCURRENT
#define CUSTOM_BLOCK_DEAUTH_AT_EAP_FAILURE
#define TDLS_MSG_ONLY_WFD
#define CUSTOM_EVENT_PM_WAKE_MEMDUMP_DISABLED
#define DHD_RESET_FEM_5G_RFFE_VI0
/* Custom tuning value */
#define CUSTOM_ROAM_TIME_THRESH_IN_SUSPEND 6000
#define CUSTOM_EVENT_PM_WAKE 30
#define CUSTOM_EVENT_PM_PERCENT 70
#define CUSTOM_KEEP_ALIVE_SETTING 30000
#define CUSTOM_PNO_EVENT_LOCK_xTIME 10
#define CUSTOM_OCL_RSSI_VAL -75
#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
	#define CUSTOM_DHD_WATCHDOG_MS 0
#endif
#define CUSTOM_TDLS_IDLE_MODE_SETTING 10000
#define CUSTOM_TDLS_RSSI_THRESHOLD_HIGH -80
#define CUSTOM_TDLS_RSSI_THRESHOLD_LOW -85
#define D3_ACK_RESP_TIMEOUT 4000
#define IOCTL_RESP_TIMEOUT 5000
#define MAX_DTIM_ALLOWED_INTERVAL 925
#define ENABLE_MAX_DTIM_IN_SUSPEND
#define NUM_SCB_MAX_PROBE 3
#define WL_SCB_TIMEOUT 10
#define WIFI_TURNOFF_DELAY 10
#define WIFI_TURNON_USE_HALINIT
/* Static preallocated buffers */
#define DHD_USE_STATIC_MEMDUMP
/* OCE/MBO */
#define WL_MBO
#define WL_OCE
#define WL_MBO_HOST
/* CELLULAR CHANNEL AVOIDANCE */
#define WL_CELLULAR_CHAN_AVOID
#define WL_CELLULAR_CHAN_AVOID_DUMP
/* USABLE CHANNEL */
#define WL_USABLE_CHAN
/* ACS check scc in active channels */
#define DHD_ACS_CHECK_SCC_2G_ACTIVE_CH
/* Latency Mode */
#define WL_LATENCY_MODE
#define SUPPORT_LATENCY_CRITICAL_DATA
/* Enable GONEG collision resolution */
#define WL_CFG80211_GON_COLLISION
/* HANG send due to private command errors */
#define DHD_SEND_HANG_PRIVCMD_ERRORS
/* HANG trigger support on escan syncid mismatch */
#define DHD_SEND_HANG_ESCAN_SYNCID_MISMATCH
/* Required for scanning the non-continue channel */
#define WFC_NON_CONT_CHAN
/* Context Hub Runtime Environment (CHRE) */
#define CHRE
/* HANG trigger support on escan syncid mismatch */
/* #define DHD_SEND_HANG_ESCAN_SYNCID_MISMATCH */
#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
	#define DHD_USE_STATIC_CTRLBUF
	/* Use coherent pool */
	#define DHD_USE_COHERENT_MEM_FOR_RING
	/* Runtime PM feature */
	#define DHD_PCIE_RUNTIMEPM
	#define MAX_IDLE_COUNT 5
	/* AXI error logging */
	#define DNGL_AXI_ERROR_LOGGING
	/* #define DHD_USE_WQ_FOR_DNGL_AXI_ERROR */
	/* 4way handshake disconnection feature */
	#define DHD_4WAYM4_FAIL_DISCONNECT
	#if IS_ENABLED(CONFIG_ARCH_MESON)
		#undef CONFIG_BCMDHD_OOB_HOST_WAKE
		/* disable runtime PM for vim3 */
		#undef DHD_PCIE_RUNTIMEPM /* Removed by filter-out */
		#undef MAX_IDLE_COUNT /* Removed by filter-out */
	#endif
			/* # OOB */
	#if IS_ENABLED(CONFIG_BCMDHD_OOB_HOST_WAKE)
		#define BCMPCIE_OOB_HOST_WAKE
		#define DHD_USE_PCIE_OOB_THREADED_IRQ
	#endif
#endif
	/* DMA64 suppports on 64bit Architecture */
#if IS_ENABLED(CONFIG_64BIT)
	#undef BCMDMA32 /* Removed by filter-out */
	#define BCMDMA64OSL
#endif
#define VSDB
/* TDLS enable */
#define WLTDLS
#define WLTDLS_AUTO_ENABLE
/* For TDLS tear down inactive time 40 sec */
#define CUSTOM_TDLS_IDLE_MODE_SETTING 10000
/* for TDLS RSSI HIGH for establishing TDLS link */
#define CUSTOM_TDLS_RSSI_THRESHOLD_HIGH -80
/* for TDLS RSSI HIGH for tearing down TDLS link */
#define CUSTOM_TDLS_RSSI_THRESHOLD_LOW -85
	/* Roaming trigger */
#define CUSTOM_ROAM_TRIGGER_SETTING -75
#define CUSTOM_ROAM_DELTA_SETTING 10
/* Set PM 2 always regardless suspend/resume */
#define SUPPORT_PM2_ONLY
	/* For special PNO Event keep wake lock for 10sec */
#define CUSTOM_PNO_EVENT_LOCK_xTIME 10
#define MIRACAST_AMPDU_SIZE 8
/* Vendor Extension */
#define WL_VENDOR_EXT_SUPPORT
/* RSSI Monitor */
#define RSSI_MONITOR_SUPPORT
/* RTT */
#define RTT_SUPPORT
#define RTT_DEBUG
/* NDOffload */
#define NDO_CONFIG_SUPPORT
#define IPV6_NDO_SUPPORT
/* Debugaility */
#define DBG_PKT_MON
#define DBG_PKT_MON_INIT_DEFAULT
#define DHD_PKT_MON_DUAL_STA
#define DNGL_EVENT_SUPPORT
#define PARSE_DONGLE_HOST_EVENT
#define WL_CFGVENDOR_SEND_ALERT_EVENT
/* Early suspend */
#define DHD_USE_EARLYSUSPEND
/* For Scan result patch */
#define ESCAN_RESULT_PATCH
#define DUAL_ESCAN_RESULT_BUFFER
#define CONFIG_ROAM_MIN_DELTA
/* NAN */
#define WL_NAN
#define WL_NAN_DISC_CACHE
#define WL_NANP2P
#define NAN_DAM_ANDROID
#define WL_NAN_6G
/* NAN 3.1 specific */
#define WL_NAN_INSTANT_MODE
/* NAN r5 GROUP security */
#define WL_NAN_GAF_PROTECT
#define FTM
#define DHD_RTT_USE_FTM_RANGE
/* Separate passphrase_DB for FTM */
#define DHD_RTT_USE_SEPARATE_PASSPHRASE_DB
#define QOS_MAP_SET
#define DHD_DSCP_POLICY
/* Thermal mitigation flag */
#define WL_THERMAL_MITIGATION
/* SAR Tx power scenario */
#define WL_SAR_TX_POWER
#define WL_SAR_TX_POWER_CONFIG
/* GET USABLE Channel */
#define WL_USABLE_CHAN
/* Silent roam */
/* GG build uses higher band roam feature */
/* #define CONFIG_SILENT_ROAM */
/* Get ROAM Channel Cache */
#define WL_GET_RCC
/* ROAM candidatae RSSI limit */
#define CONFIG_ROAM_RSSI_LIMIT
#define CUSTOM_ROAMRSSI_2G -80
#define CUSTOM_ROAMRSSI_5G -77
#define WL_GCMP_SUPPORT
/* Disable HE on P2P based on peer support */
#define WL_DISABLE_HE_P2P
/* Advertise HE capabilities */
#define WL_CAP_HE
/* Advertise OCE_STA capability */
#define WL_CAP_OCE_STA
/* Enable RTT LCI/LCR info support */
#define WL_RTT_LCI
#define WL_RTT_ONE_WAY
#define WL_RTT_BW160
/* For Static Buffer */
#if IS_ENABLED(CONFIG_DHD_USE_STATIC_BUF)
	#define ENHANCED_STATIC_BUF
	#define STATIC_WL_PRIV_STRUCT
#endif
/* Ioctl timeout 5000ms */
#define IOCTL_RESP_TIMEOUT 5000
/* Prevent rx thread monopolize */
#define WAIT_DEQUEUE
/* idle count */
#define DHD_USE_IDLECOUNT
/* SKB TAILPAD to avoid out of boundary memory access */
#define DHDENABLE_TAILPAD
/* SCAN time */
#define CUSTOM_SET_SHORT_DWELL_TIME
/* Disable FRAMEBURST on VSDB */
/* #define DISABLE_FRAMEBURST_VSDB */
/* WPS */
#define WL_WPS_SYNC
#define BCMCRYPTO_COMPONENT
/* Path name to store the FW Debug symbol files */
#define PLATFORM_PATH "/vendor/etc/wifi/"
#define SIMPLE_MAC_PRINT
#define DHD_CLEANUP_KEEP_ALIVE
/* Interface Concurrency */
#define WL_DUAL_STA
#define WL_DUAL_APSTA
/* Support to update clm/nvram through downloading OTA */
#define SUPPORT_OTA_UPDATE
/* RNR INCLUSION */
#define DHD_SCAN_INC_RNR
/* debug code to identify root cause of scan timeout due to syncid mismatch */
#define SYNCID_MISMATCH_DEBUG
/* MSCS OFFLOAD */
#define WL_RAV_MSCS_NEG_IN_ASSOC
/* MAX_PFN_LIST_COUNT is defined as 64 in wlioctl_defs.h */
#define MAX_PFN_LIST_COUNT 16
/* Enable idsup for 4-way HS offload */
/* #define BCMSUP_4WAY_HANDSHAKE */
/* #define WL_ENABLE_IDSUP */
/* Enable idauth for AP 4-way HS offload */
/* #define WL_IDAUTH */
/* Enable SAE offload - standard kernel path */
/* #define WL_SAE_STD_API */
/* Using extenal supplicant SAE */
/* #define WL_CLIENT_SAE */
/* SAE-FT */
/* #define WL_SAE_FT */
/* OWE host/offload common path code */
/* #define WL_OWE */
/* OWE offload - Kernel should >= 6.7 */
/* #define WL_OWE_OFFLD */
/* Enable backports for AP port auth and owe cap for GG SOC kernel */
#if IS_ENABLED(CONFIG_SOC_GOOGLE)
	#define WL_OWE_OFFLD_BKPORT
	#define WL_AP_PORT_AUTH_BKPORT
	#define WL_OWE_OFFLD_FEAT_ADV_BKPORT
#endif
/* In-dongle WPAIE/RSNIE/RSNXE support */
/* #define WL_WSEC_IE_OFFLD */
/* STA DUMP */
#define WL_BSS_STA_INFO
	#if IS_ENABLED(CONFIG_PORT_AUTH_BKPORT)
	/* Support for TDI, P2P GC. */
	#define WL_MLO_BKPORT_NEW_PORT_AUTH
	/* Enable AP port auth support */
	#define WL_AP_PORT_AUTH_BKPORT
	/* warning "AUTH Backported kernel" */
#endif
#if IS_ENABLED(CONFIG_AP_4WAY_HS_BKPORT)
	/* Enable AP_PSK support */
	#define WL_AP_4WAY_HS_BKPORT
	/* warning "AP 4WAY HS Backported kernel" */
#endif
#if IS_ENABLED(CONFIG_SAE_AP_CAP_BKPORT)
	/* Enable SAE AP capability backport */
	#define WL_SAE_AP_CAP_BKPORT
	/* warning "SAE AP Backported kernel" */
#endif
#if IS_ENABLED(CONFIG_SAE_PWE_BKPORT)
	/* Enable SAE PWE standard path backport */
	#define WL_SAE_PWE_BKPORT
	/* warning "AP PWE Backported kernel" */
#endif
#if IS_ENABLED(CONFIG_OWE_CAP_BKPORT)
	#define WL_OWE_OFFLD_BKPORT
	/* warning "OWE Backported kernel" */
#endif
/* Target wake time android v */
#define WL_TWT_HAL_IF
/* ######################### */
/* driver type */
/* m: module type driver */
/* y: built-in type driver */
/* ######################### */
#define DRIVER_TYPE CONFIG_BCMDHD
/* ######################## */
/* Chip dependent feature */
/* ######################## */
#if IS_ENABLED(CONFIG_BCM4389) || IS_ENABLED(CONFIG_BCM4398) || IS_ENABLED(CONFIG_BCM4390) || IS_ENABLED(CONFIG_BCM4383)
	/* 6GHz support */
	#define WL_6G_BAND
	/* UNII4 channel support */
	#define WL_5P9G
	/* UNII-4 channel filter for non-sta roles */
	#define WL_UNII4_CHAN
#endif
/* Newer chips support multi interface support for APF */
#if IS_ENABLED(CONFIG_SOC_GOOGLE)
	#if IS_ENABLED(CONFIG_BCM4389) || IS_ENABLED(CONFIG_BCM4398) || IS_ENABLED(CONFIG_BCM4383)
		#define APF_SINGLE_IF_SUPPORT
	#endif
#endif
/* For 4389 and 43752 */
#if IS_ENABLED(CONFIG_BCM4389) || IS_ENABLED(CONFIG_BCM4398) || IS_ENABLED(CONFIG_BCM4390) || IS_ENABLED(CONFIG_BCM4383) \
	|| IS_ENABLED(CONFIG_BCM43752) || IS_ENABLED(CONFIG_BCM4375) || IS_ENABLED(CONFIG_BCM4385)
	#define USE_WL_TXBF
	#define CUSTOM_DPC_CPUCORE 0
	/* New Features */
	#define WL11U
	#define MFP
	#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
		/* debug info */
		#define DHD_WAKE_STATUS
		#define DHD_WAKE_RX_STATUS
		#define DHD_WAKE_EVENT_STATUS
		#define DHD_WAKE_STATUS_PRINT
	#endif
	#if IS_ENABLED(CONFIG_BCMDHD_SDIO)
		#define BDC
		#define DHD_BCMEVENTS
		#define MMC_SDIO_ABORT
					#define OOB_INTR_ONLY
		#define HW_OOB
					#define BCMSDIO
		#define BCMLXSDMMC
		#define USE_SDIOFIFO_IOVAR
					#define PROP_TXSTATUS
		#define CUSTOM_AMPDU_MPDU 16
		#define CUSTOM_AMPDU_BA_WSIZE 64
					/* tput enhancement */
		#define CUSTOM_GLOM_SETTING 8
		#define CUSTOM_RXCHAIN
		#define DYNAMIC_F2_BLKSIZE_FOR_NONLEGACY 128
		#define CUSTOM_TXGLOM 1
		#define BCMSDIOH_TXGLOM_HIGHSPEED
		#define DHDTCPACK_SUPPRESS
		#define CUSTOM_TCPACK_SUPP_RATIO 15
		#define CUSTOM_TCPACK_DELAY_TIME 10
		#define RXFRAME_THREAD
		#define REPEAT_READFRAME
		#define CUSTOM_MAX_TXGLOM_SIZE 40
		#define MAX_HDR_READ 128
		#define DHD_FIRSTREAD 128
		#define WLFC_STATE_PREALLOC
	#endif
	/* Expand TCP tx queue to 10 times of default size */
	#define TSQ_MULTIPLIER 10
	#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
		#define PCIE_FULL_DONGLE
		#define BCMPCIE
		#define CUSTOM_DPC_PRIO_SETTING -1
		/* NCI */
		#define SOCI_NCI_BUS
		/* tput enhancement */
		#define CUSTOM_AMPDU_BA_WSIZE 64
		#define PROP_TXSTATUS_VSDB
		/* HEAP ASLR */
		#define BCM_ASLR_HEAP
		/* STBTPUT: Disabled for tput in STB platform */
		#if !(IS_ENABLED(CONFIG_ARCH_BRCMSTB) || IS_ENABLED(CONFIG_SYNAPTICS_SOC))
			/* Enable workitem aggregation */
			#define DHD_AGGR_WI
			/* Bits in DHD_AGGR_WI_EN : 0 = TXPOST | 1 = RXPOST | 2 = TXCPL | 3 = RXCPL */
			#define DHD_AGGR_WI_EN 0xE
		#endif
		#define MAX_CNTL_TX_TIMEOUT 1
		#if IS_ENABLED(CONFIG_ARCH_MSM)
			#define MSM_PCIE_LINKDOWN_RECOVERY
		#endif
		#if IS_ENABLED(CONFIG_DHD_USE_STATIC_BUF)
			#define DHD_USE_STATIC_IOCTLBUF
		#endif
		/* Enable health check event handling */
		#define DNGL_EVENT_SUPPORT
		#define HCHK_COMMON_SW_EVENT
	#endif
	#if IS_ENABLED(CONFIG_DHD_OF_SUPPORT)
		#define DHD_OF_SUPPORT
	#endif
	/* Print 802.1X packets */
	#define DHD_8021X_DUMP
	/* prioritize 802.1x packet */
	#define EAPOL_PKT_PRIO
	/* Update Tx/Rx rate info */
	#define WL_RATE_INFO
#endif /* # Multiple Chip specific defines CONFIG_BCMxxxx */
	#define ENABLE_INSMOD_NO_FW_LOAD
	#if defined(DRIVER_TYPE) && (DRIVER_TYPE == BCMDHD_BUILTIN)
		#define USE_LATE_INITCALL_SYNC
		/* Use kernel strlcpy() implementation instead of one, defined in bcmstdlib_s.c */
		#define BCM_USE_PLATFORM_STRLCPY
#endif

#if defined(DRIVER_TYPE) && (DRIVER_TYPE == BCMDHD_MODULE)
	#define BCMDHD_MODULAR
#endif

/* Collect dumps upon init time failures */
#define DEBUG_DNGL_INIT_FAIL
#define DHD_CAP_CUSTOMER "hw2 "
#if IS_ENABLED(CONFIG_SOC_GOOGLE)
	/* The flag will be enabled only on customer platform */
	#if IS_ENABLED(CONFIG_BCM4383)
		#define POWERUP_MAX_RETRY 0
	#else
		#define POWERUP_MAX_RETRY 1
	#endif
	#define CUSTOMER_HW2
	#define CUSTOMER_HW2_DEBUG
	#define DHD_SET_PCIE_DMA_MASK_FOR_GS101
	#define CUSTOM_CONTROL_LOGTRACE 1
	#define DHD_CAP_PLATFORM "exynos "
	#define CONFIG_ARCH_EXYNOS
	#define DHD_MODULE_INIT_FORCE_SUCCESS
	#define SUPPORT_MULTIPLE_NVRAM
	#define SUPPORT_MULTIPLE_CLMBLOB
	#define DHD_LB_TXP_DEFAULT_ENAB
	#define DHD_SSSR_COREDUMP
	#define DHD_REDUCE_PM_LOG
	/* LB RXP Flow control to avoid OOM */
	#define LB_RXP_STOP_THR 500
	#define LB_RXP_STRT_THR 499
	#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
		/* Enable FIS Dump (without common subcore) to collect on special cases */
		#define DHD_FIS_DUMP
		#define FIS_WITHOUT_CMN
	#endif
	/* Dongle init fail */
	/* Increase assoc beacon wait time */
	#define DEFAULT_RECREATE_BI_TIMEOUT 40
	#if !defined(GG_REF_PLATFORM)
		/* Add chip specific suffix to the output on customer release */
		#if IS_ENABLED(CONFIG_BCM4389)
			#define BCMPCI_DEV_ID 0x4441
			#define BCMPCI_NOOTP_DEV_ID 0x4389
			#define BCM4389_CHIP_DEF
		#endif
		#if IS_ENABLED(CONFIG_BCM4398)
			#define BCMPCI_DEV_ID 0x4444
			#define BCMPCI_NOOTP_DEV_ID 0x4398
			#define BCM4398_CHIP_DEF
			#define ML_AWARE_SUP_SUPPORT
		#endif
		#if IS_ENABLED(CONFIG_BCM4390)
			#define BCMPCI_DEV_ID 0x4438
			#define BCMPCI_NOOTP_DEV_ID 0x4390
			#define BCM4390_CHIP_DEF
		#endif
		#if IS_ENABLED(CONFIG_BCM4383)
			#define BCMPCI_DEV_ID 0x4449
			#define BCMPCI_NOOTP_DEV_ID 0x4383
			#define BCM4383_CHIP_DEF
			#define DISABLE_EHT_CAP
			#define LEGACY_CROSS_AKM
		#endif
	#endif
	#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
		#if IS_ENABLED(CONFIG_SOC_GS201) || IS_ENABLED(CONFIG_SOC_ZUMA) || IS_ENABLED(CONFIG_SOC_LGA)
			#define PCIE_CPL_TIMEOUT_RECOVERY
		#endif
	#endif
	/* TCP TPUT Enhancement, enable only for GS101 */
	#define DHD_TCP_LIMIT_OUTPUT
	#define DHD_TCP_PACING_SHIFT
	#define MACADDR_PROVISION_ENFORCED
#elif IS_ENABLED(CONFIG_ARCH_HISI)
	#define BOARD_HIKEY
	#define BOARD_HIKEY_HW2
	#define BCMDEV
	#define DHD_CHECK_CTO_FROM_ISR
	#define DHD_SUPPORT_VFS_CALL
	/* Skip pktlogging of data packets */
	#define DHD_SKIP_PKTLOGGING_FOR_DATA_PKTS
	/* Copy to new pkts for pkts from invalid RA range */
	#define DHD_VALIDATE_PKT_ADDRESS
	/* Allow wl event forwarding as network packet */
	#define WL_EVENT_ENAB
	/* Enable memdump for logset beyond range only internal builds */
	#define DHD_LOGSET_BEYOND_MEMDUMP
	#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
		/* LB RXP Flow control to avoid OOM */
		#define LB_RXP_STOP_THR 200
		#define LB_RXP_STRT_THR 199
		/* Enable FIS Dump (with common subcore) to collect on special cases */
		#define DHD_FIS_DUMP
		#define FIS_WITH_CMN
	#endif
	#define DHD_SUPPORT_VFS_CALL
	#define DHD_IOVAR_LOG_FILTER_DUMP
	#define DHD_CAP_PLATFORM "hikey "
	#define DISABLE_L2_IN_D3
	#undef SIMPLE_MAC_PRINT /* Removed by filter-out */
#elif IS_ENABLED(CONFIG_SYNAPTICS_SOC)
	#define POWERUP_MAX_RETRY 1
	#define BOARD_STB
	#define BOARD_STB_ASTRA
	#define BOARD_STB_HW2
	#define BCMDEV
	#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
		/* Enable FIS Dump (with common subcore) to collect on special cases */
		#define DHD_FIS_DUMP
		#define FIS_WITH_CMN
	#endif
	#define DHD_SUPPORT_VFS_CALL
	/* Skip pktlogging of data packets */
	#define DHD_SKIP_PKTLOGGING_FOR_DATA_PKTS
	/* Allow wl event forwarding as network packet */
	#define WL_EVENT_ENAB
	/* Enable memdump for logset beyond range only internal builds */
	#define DHD_LOGSET_BEYOND_MEMDUMP
	#define DHD_IOVAR_LOG_FILTER_DUMP
	#define DHD_CAP_PLATFORM "astra "
	#undef SIMPLE_MAC_PRINT /* Removed by filter-out */
	/* Astra supports MSI */
	#define DHD_MSI_SUPPORT
	/* Astra kernel backported MLO and Channel switch */
	#define WL_MLO_BKPORT
	#define WL_MLO_BKPORT_NEW_PORT_AUTH
	#define WL_CH_SWITCH_BKPORT
	#define WL_OWE_OFFLD_BKPORT
	#define WL_AP_PORT_AUTH_BKPORT
	#define WL_OWE_OFFLD_FEAT_ADV_BKPORT
#elif IS_ENABLED(CONFIG_ARCH_BRCMSTB)
	#define POWERUP_MAX_RETRY 1
	#define BOARD_STB
	#define BOARD_STB_BRCM
	#define BOARD_STB_HW2
	#define BCMDEV
	#if IS_ENABLED(CONFIG_BCMDHD_PCIE)
		/* Enable FIS Dump (with common subcore) to collect on special cases */
		#define DHD_FIS_DUMP
		#define FIS_WITH_CMN
	#endif
	#define DHD_SUPPORT_VFS_CALL
	/* Skip pktlogging of data packets */
	#define DHD_SKIP_PKTLOGGING_FOR_DATA_PKTS
	/* Allow wl event forwarding as network packet */
	#define WL_EVENT_ENAB
	/* Enable memdump for logset beyond range only internal builds */
	#define DHD_LOGSET_BEYOND_MEMDUMP
	#define DHD_LB_TXP_DEFAULT_ENAB
	/* #define LB_RXP_STOP_THR 200 */
	/* #define LB_RXP_STRT_THR 199 */
	#define DHD_SUPPORT_VFS_CALL
	#define DHD_IOVAR_LOG_FILTER_DUMP
	#define DHD_CAP_PLATFORM "stb "
	#undef SIMPLE_MAC_PRINT /* Removed by filter-out */
	/* TCP TPUT Enhancement, enable only for GS101 */
	#define DHD_TCP_LIMIT_OUTPUT
	#define DHD_TCP_PACING_SHIFT
	#define DHDTCPACK_SUPPRESS
	#if !defined(STB_ANDROIDVER) || (STB_ANDROIDVER != T)
		/* ch_switch_notify back port changes */
		#define WL_CH_SWITCH_BKPORT
		/* External auth request back port changes */
		/* #define WL_EXT_AUTH_BKPORT */
		/* TDI policy kernel back port changes */
		#define WL_MLO_BKPORT_NEW_PORT_AUTH
		#if !defined(STB_BOARD_VARIANT) || (STB_BOARD_VARIANT != A0)
			#define WAKEUP_KSOFTIRQD_POST_NAPI_SCHEDULE
		#endif
	#endif
	/* STB supports MSI */
	#define DHD_MSI_SUPPORT
#endif /* CONFIG_SOC_GOOGLE */

#define DHD_DEBUG

#if IS_ENABLED(CONFIG_ARCH_HISI) || IS_ENABLED(CONFIG_ARCH_BRCMSTB) || IS_ENABLED(CONFIG_SYNAPTICS_SOC)
#include "dhd_defines_internal.h"
#endif
#endif /* _DHD_DEFINES_H_ */
