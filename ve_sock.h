/**
 * Copyright (C) 2020 NEC Corporation
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
 * @file ve_sock.h
 * @brief Header file for ve_sock.c file
 *
 * @internal
 * @author RPM command
 */

#ifndef _VE_SOCK_H
#define _VE_SOCK_H

#include <sys/types.h>
#define VEOS_SOC_PATH "@localstatedir@"

/**
 * @brief To uniquely identify request, sent to VEOS
 */
enum velib_cmdreq {
	VE_GETPRIORITY = 0,
	VE_SETPRIORITY,
	VE_CHECKPID,
	VE_MEM_INFO,
	VE_MAP_INFO,
	VE_PIDSTAT_INFO,
	VE_PIDSTATM_INFO,
	VE_PIDSTATUS_INFO,
	VE_LOAD_INFO,
	VE_STAT_INFO,
	VE_GET_RUSAGE,
	VE_SCHED_GET_SCHEDULER,
	VE_SCHED_SET_SCHEDULER,
	VE_SCHED_GET_PARAM,
	VE_GET_PRIORITY_MAX,
	VE_GET_PRIORITY_MIN,
	VE_SET_AFFINITY,
	VE_GET_AFFINITY,
	VE_PRLIMIT,
	VE_ACCTINFO,
	VE_CREATE_PROCESS,
	VE_SHM_INFO,
	VE_GET_REGVALS,
	VE_NUMA_INFO,
	VE_DEL_DUMMY_TASK,
	VE_SWAP_STATUSINFO,
	VE_SWAP_INFO,
	VE_SWAP_NODEINFO,
	VE_SWAP_OUT,
	VE_SWAP_IN,
	VE_SWAP_GET_CNS,
	VE_RPM_INVALID = -1
};

/**
 * @brief command ID's used to communicate b/w RPM Library and VEOS
 */
enum veos_msg_id {
	RPM_QUERY,
	RPM_QUERY_COMPT = 56
};

int velib_sock(char *);
int velib_send_cmd(int, void *, int);
int velib_recv_cmd(int, void *, int);
#endif
