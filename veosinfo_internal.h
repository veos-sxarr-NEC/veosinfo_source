/**
 * Copyright (C) 2017-2018 NEC Corporation
 * This file is part of the VEOS information library.
 *
 * The VEOS information library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version
 * 2.1 of the License, or (at your option) any later version.
 *
 * The VEOS information library is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the VEOS information library; if not, see
 * <http://www.gnu.org/licenses/>.
 */
/**
 * @file veosinfo_internal.h
 * @brief Internal header file for veosinfo.c file
 *
 * @internal
 * @author RPM command
 */
#ifndef _VEOSINFO_INTERNAL_H
#define _VEOSINFO_INTERNAL_H
#include <sched.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdbool.h>
#include <yaml.h>
#include "veosinfo.h"
#include "veosinfo_comm.h"
#define VE_MACHINE	"ve"
#define VE_PROCESSOR	"ve"
#define VE_HW_PLATFORM	"ve"
#define MAX_PROTO_MSG_SIZE 4096
#define VE_MAX_CACHE	4		/*!< Maximum number of VE cache */
#define DEV_PATH	"/dev"		/*!< VE node device path */
#define VE_DEVICE_NAME	"veslot"	/*!< VE node name */
#define YAML_FILE_PATH	VE_ETC_BASE "/mmm/info"
#define MAX_POWER_DEV	20
#define SENSOR_DEV_NAME_INDEX 0
#define YAML_FILE_INDEX 11
#define YAML_DATA_DEM  1000000		/*!<
					 * Denominator for YAML data values
					 * conversion
					 */
#define DEV_MATCH_NUMBER 3		/*!<
					 * A number used to check, if we are
					 * parsing the correct device type.
					 */
#define NOCORE_ID	2		/*!<
					 * To parse less values in yaml
					 * file if device entry not
					 * include "core_id".
					 */
#define HBM_DEV_COUNT	5		/*!<
					 * Count of Thermal "ve_hbm[0..5]_temp" device
					 */

/**
 * @brief RPM library specific structure to get the memory information of
 * VE node
 */
struct velib_meminfo {
	unsigned long kb_main_total;	/*!< Total usable RAM */
	unsigned long kb_main_used;	/*!< Total used RAM
					 * (kb_main_total - kb_main_free) */
	unsigned long kb_main_free;	/*!< Total free memory */
	unsigned long kb_main_shared;	/*!< Total shared memory size */
	unsigned long kb_hugepage_used; /*!<
					 * Amount of hugepages memory in
					 * kilobytes that has been allocated
					 */
};

/**
 * @brief RPM library specific structure to get CPU statistics about VE node
 */
struct velib_statinfo {
	unsigned long long user[VE_MAX_CORE_PER_NODE];	/*!<
							 * Amount of time CPU has
							 * spent executing normal
							 * processes in user mode
							 */
	unsigned long long idle[VE_MAX_CORE_PER_NODE];	/*!<
							 * Amount of time CPU has
							 * spent executing twiddling
							 * thumbs
							 */
	unsigned int ctxt;				/*!<
							 * The total number of context
							 * switches across all CPUs
							 */
	unsigned int running;				/*!<
							 * The total number of
							 * runnable threads
							 */
	unsigned int blocked;				/*!<
							 * The number of processes
							 * currently blocked,
							 * waiting for I/O to complete
							 */
	unsigned long btime;				/*!<
							 * The time at which the
							 * VE nodes were booted
							 */
	unsigned int processes;				/*!<
							 * The number of processes
							 * and threads created
							 */
};

/**
 * @brief RPM library specific structure to get given process's
 * statistics from VEOS
 */
struct velib_pidstat {
	char state;			/*!<
					 * Process state
					 * (running, sleeping, stopped, zombie)
					 */
	int ppid;			/*!< Parent Process ID  */
	int processor;			/*!< Core on which task is scheduled on */
	long priority;			/*!< Scheduling priority */
	long nice;			/*!< Nice level */
	unsigned int policy;		/*!< Scheduling policy */
	unsigned long long utime;	/*!< CPU time accumulated by process */
	unsigned long long cutime;	/*!<
					 * Cumulative utime of process and
					 * reaped children
					 */
	unsigned long flags;		/*!< Task flags */
	unsigned long vsize;		/*!< Process's virtual memory size */
	unsigned long rsslim;		/*!< Current limit in bytes on the rss */
	unsigned long startcode;	/*!<
					 * Address above which program text can run
					 */
	unsigned long endcode;		/*!< Address below which program text can run */
	unsigned long startstack;	/*!< Address of the start of the stack */
	unsigned long kstesp;		/*!< Current value of ESP */
	unsigned long ksteip;		/*!< Current value of EIP */
	long rss;			/*!< Resident set memory size   */
	char cmd[255];			/*!< Only command name without path */
	unsigned long start_time;       /*!< Start time of VE process */
	bool whole;			/*!<
					 * Flag to get statistics of single
					 * thread (0) or the whole thread group (1)
					 */
};

/**
 * @brief RPM library specific structure to get overall VE process information
 */
struct velib_pidstatus {
	unsigned long nvcsw;	/*!<
				 * Total number of voluntary context
				 * switches task made per second
				 */
	unsigned long nivcsw;	/*!<
				 * Total number of non-voluntary context
				 * switches task made per second
				 */
	char cmd[255];          /*!< Only command name without path */
	unsigned long long sigpnd;	/*!< mask of PER TASK pending signals */
	unsigned long long blocked;	/*!< blocked signals for VE task */
	unsigned long long sigignore;	/*!< signal with ignore disposition */
	unsigned long long sigcatch;	/*!< signal with changed default action */

};

/**
 * @brief structure to get/set the resource limit (hard and soft) of VE process
 */
struct velib_prlimit {
	int resource;			/*!<
					 * Resource argument can be Address
					 * space, CPU time, FSIZE, locks etc
					 */
	bool is_new_lim;		/*!<
					 * This value will decide the get/set
					 * of prlimit
					 */
	struct rlimit new_limit;	/*!<
					 * New values for the soft and hard
					 * limits for resource
					 */
	struct rlimit old_limit;	/*!<
					 * Successful call to prlimit() places
					 * the previous soft and hard limits
					 * for resource in the rlimit structure
					 * pointed to by old_limit
					 */
};

/**
 * @brief Structure to get/set CPU affinity
 */
struct velib_affinity {
	size_t cpusetsize;	/*!< Specifies the size (in bytes) of mask */
	cpu_set_t mask;		/*!< Affinity mask */
};

/**
 * @brief Structure to create process
 */
struct velib_create_process {
	int flag;	/*!< Flag to preserve the task struct for resource usage */
	int vedl_fd; 	/*!< FD from VE Driver */
	struct rlimit ve_rlim[RLIM_NLIMITS]; /*!<
					      * To store the process limit
					      * as per flag value
					      */
};

/**
 * @brief RPM library specific structure to get memory statistics of VE process
 */
struct velib_pidstatm {
	long size;	/*!< Total program size (as # pages) */
	long resident;	/*!< Resident non-swapped memory (as # pages) */
	long share;	/*!< Shared (mmap'd) memory (as # pages) */
	long trs;	/*!< Text (exe) resident set (as # pages) */
	long drs;	/*!< Data+stack resident set (as # pages) */

};

/**
 * @brief RPM library specific structure to get the resource usage of VE process
 */
struct velib_get_rusage_info {
	struct timeval utime;		/*!< User CPU time used */
	struct timeval elapsed;		/*!< Elapsed real (wall clock) time */
	long ru_maxrss;			/*!< Maximum resident set size */
	long ru_nvcsw;			/*!< Voluntary context switches */
	long ru_nivcsw;			/*!< Involuntary context switches */
	long page_size;			/*!< Page Size */
};

/**
 * @brief RPM library specific structure to get power management related
 * information
 */
struct ve_pwr_mgmt_info {
	char device_name[MAX_POWER_DEV][MAX_DEVICE_LEN];	/*!<
								 * Voltage
								 * device name
								 */
	int count;
	double min_val[MAX_POWER_DEV];				/*!<
								 * Minimum value
								 * for voltage
								 * for given
								 * node
								 */
	double max_val[MAX_POWER_DEV];				/*!<
								 * Maximum value
								 * for voltage
								 * for given
								 * node
								 */
	double actual_val[MAX_POWER_DEV];			/*!<
								 * Voltage of
								 * VE node
								 */
};

/**
 * @brief Structure which contains information about system resources consumed
 * by shared memory.
 */
struct velib_shm_summary {
        int used_ids;		/*!< Number of currently existing segments */
        ulong shm_tot;		/*!< Total number of shared memory pages */
        ulong shm_rss;          /*!< Number of resident shared memory pages */
};

/**
 * @brief Structure to provide information to VEOS
 */
struct ve_shm_info {
	int mode;	/*!< mode SHMKEY - Removed segment using shmkey */
			/*!< mode SHMID  - Removed segment using shmid */
			/*!< mode SHM_ALL - Removed all segment */
			/*!< mode SHM_LS - List All segment */
			/*!< mode SHM_SUMMARY - Summary of share Memory */
	int key_id;	/* SHMKEY or SHMID segment to be delete */
};

void get_ve_rlimit(struct rlimit *);
int ve_sysfs_path_info(int, const char *);
int ve_cache_info(int, char [][VE_BUF_LEN], int *);
int get_ve_node(int *, int *);
int read_yaml_file(int, char*, struct ve_pwr_mgmt_info *);
int ve_phy_core_map(int, int *);
char *ve_get_modelname(int);
char *ve_get_sensor_device_name(int, int, char *, int *);
int read_file_value(int, char *);
int get_yaml_data(char [][MAX_DEVICE_LEN], struct ve_pwr_mgmt_info *,
						int, char *, int, int);
#endif
