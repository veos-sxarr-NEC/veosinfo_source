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
 * @file veosinfo.c
 * @brief Handles and manages the RPM source request and get required
 * information from VEOS and VE sysfs
 *
 * @internal
 * @author RPM command
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/types.h>
#include <libudev.h>
#include <elf.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include "veosinfo.h"
#include "ve_sock.h"
#include "veos_RPM.pb-c.h"
#include "veosinfo_log.h"
#include "veosinfo_internal.h"
#include "veosinfo_comm.h"

/**
 * @brief This function will check the value of VE node passed as environment
 * variable is a valid VE node or not
 *
 * @param envrn[in] VE node passed by user as environment variable in
 * "VE_NODE_NUMBER" with command
 *
 * @return 0 on success and -1 of failure
 */
int ve_match_envrn(char *envrn)
{
	FILE *fp = NULL;
	int nodeid = -1;
	int retval = -1;
	int nread = 0;
	int status = -1;
	int sock_fd = -1;
	char sock_name[VE_PATH_MAX] = {0};
	char *node_status_path = NULL;
	const char ve_sysfs_path[PATH_MAX] = {0};

	VE_RPMLIB_TRACE("Entering");
	if (!envrn) {
		VE_RPMLIB_ERR("Wrong argument received: envrn = %p", envrn);
		errno = EINVAL;
		goto hndl_return;
	}

	nodeid = atoi(envrn);
	/* Get sysfs path corresponding to given VE node */
	retval = ve_sysfs_path_info(nodeid, ve_sysfs_path);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get sysfs path: %s",
				strerror(errno));
		goto hndl_return;
	}
	retval = -1;
	node_status_path =
		(char *)malloc(strlen(ve_sysfs_path) + VE_FILE_NAME);
	if (!node_status_path) {
		VE_RPMLIB_ERR("Memory allocation to node status path failed: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Get sysfs path to read 'os_state' of given VE node
	 */
	sprintf(node_status_path, "%s/os_state", ve_sysfs_path);

	/* Open the "os_state" file to get the state of VE node
	 */
	fp = fopen(node_status_path, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Open file '%s' failed: %s",
				node_status_path, strerror(errno));
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Open node status file %s successfully.",
			node_status_path);

	/* Read the VE node status information
	 */
	nread = fscanf(fp, "%d", &status);
	if (nread == EOF || nread != 1) {
		VE_RPMLIB_ERR("Failed to read: %s", strerror(errno));
		fclose(fp);
		goto hndl_return2;
	}
	fclose(fp);

	if (status) {
		VE_RPMLIB_ERR("Given node %s is not online", envrn);
		goto hndl_return2;

	}

	sprintf(sock_name, "%s/veos%d.sock", VE_SOC_PATH, nodeid);
	VE_RPMLIB_DEBUG("Socket path for given VE node = %s", sock_name);

	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket: %s, error: %s",
				sock_name, strerror(errno));
	} else if (-2 == sock_fd) {
		VE_RPMLIB_ERR("Given node %s is offline", envrn);
		close(sock_fd);
	}
	else {
		close(sock_fd);
		retval = 0;
		VE_RPMLIB_DEBUG("Given node is online: %s", envrn);
	}
	errno = 0;
hndl_return2:
	free(node_status_path);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will create socket path corresponding to given VE node
 *
 * @param nodeid[in] VE node number
 *
 * @return Socket path corresponding to given node on success and
 * NULL on failure
 */
char *ve_create_sockpath(int nodeid)
{
	char *sock_path = NULL;

	VE_RPMLIB_TRACE("Entering");

	sock_path = (char *) malloc(sizeof(char) * VE_PATH_MAX);
	if (!sock_path) {
		VE_RPMLIB_ERR("Socket path memory allocation failed: %s",
				strerror(errno));
		goto hndl_return;
	}

	sprintf(sock_path, "%s/veos%d.sock", VE_SOC_PATH, nodeid);
	VE_RPMLIB_DEBUG("Socket path for given VE node = %s", sock_path);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return sock_path;
}

/**
 * @brief This function will populate total number of online VE nodes
 * and node name
 *
 * @param online_node_count[out] Total number of nodes,
 * on which VEOS is running
 * @param nodeid[out] Node number of all online VE nodes
 *
 * @return 0 on success and -1 of failure
 */
int ve_get_nos(unsigned int *online_node_count, int *nodeid)
{
	unsigned int node_count = 0;
	unsigned int count = 0;
	int retval = -1;
	int sock_fd = -1;
	char sock_name[VE_PATH_MAX] = {0};
	struct ve_nodeinfo ve_nodeinfo_req = { {0} };

	VE_RPMLIB_TRACE("Entering");

	if (!nodeid || !online_node_count) {
		VE_RPMLIB_ERR("Wrong argument received:node = %p," \
				" online_node_count = %p", nodeid,
						online_node_count);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Get the installed VE nodes */
	if (-1 ==  ve_node_info(&ve_nodeinfo_req)) {
		VE_RPMLIB_ERR("Failed to get VE node information: %s",
				strerror(errno));
		goto hndl_return;
	}
	for (count = 0; count < ve_nodeinfo_req.total_node_count; count++) {
		VE_RPMLIB_DEBUG("Check for node_count = %d and node = %d",
				count, ve_nodeinfo_req.nodeid[count]);
		/* Check for node status online and offline
		 */
		if (!ve_nodeinfo_req.status[count]) {
			sprintf(sock_name, "%s/veos%d.sock",
					VE_SOC_PATH, ve_nodeinfo_req.nodeid[count]);
			VE_RPMLIB_DEBUG("Socket path for given VE node = %s", sock_name);

			/* Create the socket connection corresponding to socket path
			*/
			sock_fd = velib_sock(sock_name);
			if (-1 == sock_fd) {
				VE_RPMLIB_DEBUG("Failed to create socket: %s, error: %s",
						sock_name, strerror(errno));
				errno = 0;
			} else if (-2 == sock_fd) {
				VE_RPMLIB_DEBUG("Node %d is offline",
						ve_nodeinfo_req.nodeid[count]);
				close(sock_fd);
				errno = 0;
			} else {
				close(sock_fd);
				nodeid[node_count] = ve_nodeinfo_req.nodeid[count];
				VE_RPMLIB_DEBUG("%d node is online.",
						nodeid[node_count]);
				node_count++;
			}
		}
	}

	/* If, there is no online node on VE */
	if (!node_count) {
		errno = ENOENT;
		goto hndl_return;
	}

	*online_node_count = node_count;
	retval = 0;

hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the VE architecture details
 *
 * @param nodeid[in] VE node number, corresponding to which architecture
 * information is required
 * @param ve_archinfo_req[out] Structure to store architecture information
 *
 * @return 0 on success and -1 on error
 */
int ve_arch_info(int nodeid, struct ve_archinfo *ve_archinfo_req)
{
	int retval = -1;

	VE_RPMLIB_TRACE("Entering");

	if (!ve_archinfo_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_archinfo_req = %p",
				ve_archinfo_req);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Populate VE architecture details
	 */
	strncpy(ve_archinfo_req->machine, VE_MACHINE, VE_FILE_NAME);
	strncpy(ve_archinfo_req->processor, VE_PROCESSOR, 257);
	strncpy(ve_archinfo_req->hw_platform, VE_HW_PLATFORM, 257);

	retval = 0;
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates online and offline VE nodes's details
 *
 * @param ve_nodeinfo_req [out] Structure to populate VE node information
 *
 *	This field consists of total number of VE nodes, total
 *	cores per node and status corresponding to each VE node
 *
 * @return 0 on success and -1 on error
 */
int ve_node_info(struct ve_nodeinfo *ve_nodeinfo_req)
{
	FILE *fp = NULL;
	int numcore = 0;
	int node_count = 0;
	int retval = -1;
	int nread = 0;
	char *node_status_path = NULL;
	const char ve_sysfs_path[PATH_MAX] = {0};

	VE_RPMLIB_TRACE("Entering");
	if (!ve_nodeinfo_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_nodeinfo_req = %p",
				ve_nodeinfo_req);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Get the VE node number and total number of VE nodes in system */
	if (0 > get_ve_node(ve_nodeinfo_req->nodeid,
					&ve_nodeinfo_req->total_node_count)) {
		VE_RPMLIB_ERR("Failed to get VE node number: %s",
				strerror(errno));
		goto hndl_return;
	}

	node_status_path =
		(char *)malloc(VE_PATH_MAX + VE_FILE_NAME);
	if (!node_status_path) {
		VE_RPMLIB_ERR("Memory allocation to node status path failed: %s",
				strerror(errno));
		goto hndl_return;
	}

	for (node_count = 0; node_count < ve_nodeinfo_req->total_node_count;
								node_count++) {
		VE_RPMLIB_DEBUG("Check for node_count = %d and node = %d",
				node_count, ve_nodeinfo_req->nodeid[node_count]);
		memset(ve_sysfs_path, '\0', PATH_MAX);
		/* Get sysfs path corresponding to given VE node */
		retval = ve_sysfs_path_info(ve_nodeinfo_req->nodeid[node_count],
					ve_sysfs_path);
		if (-1 == retval) {
			VE_RPMLIB_ERR("Failed to get VE sysfs path: %s",
					strerror(errno));
			goto hndl_return2;
		}
		retval = -1;

		/* Open VE specific sysfs directory, to get the node specific
		 * information
		 */
		memset(node_status_path, '\0', (VE_PATH_MAX + VE_FILE_NAME));
		sprintf(node_status_path, "%s/os_state", ve_sysfs_path);
		/* Open the "os_state" file corresponding to node,
		 * to get the state information
		 */
		fp = fopen(node_status_path, "r");
		if (!fp) {
			VE_RPMLIB_ERR("Open file '%s' failed: %s",
					node_status_path, strerror(errno));
			goto hndl_return2;
		}
		VE_RPMLIB_DEBUG("Open node status file %s successfully.",
				node_status_path);

		/* Read the VE node status information
		 */
		nread = 0;
		nread = fscanf(fp, "%d", &ve_nodeinfo_req->status[node_count]);
		if (nread == EOF || nread != 1) {
			VE_RPMLIB_ERR("Failed to read file (%s): %s",
					node_status_path, strerror(errno));
			fclose(fp);
			goto hndl_return2;
		}
		fclose(fp);
		VE_RPMLIB_DEBUG("Reading status = %d",
				ve_nodeinfo_req->status[node_count]);

		/* Get the cores corresponding to given node
		 */
		numcore = 0;
		if (-1 == ve_core_info(ve_nodeinfo_req->nodeid[node_count],
						&numcore)) {
			VE_RPMLIB_ERR("Failed to get CPU cores: %s",
					strerror(errno));
			goto hndl_return;
		}
		/* Populate the structure with cores value
		 */
		ve_nodeinfo_req->cores[node_count] = numcore;
		VE_RPMLIB_DEBUG("node = %d, node_status = %d, cores = %d",
				ve_nodeinfo_req->nodeid[node_count],
				ve_nodeinfo_req->status[node_count],
				ve_nodeinfo_req->cores[node_count]);

	}

	retval = 0;
	VE_RPMLIB_DEBUG("Total_node_count = %d",
			ve_nodeinfo_req->total_node_count);
hndl_return2:
	free(node_status_path);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief Convert 'string' to 'unsigned long long' and also handle
 * out of range value of 'unsigned long long'
 *
 * @param limit_optarg [in] String value
 * @param lim_val [out] Converted value in 'unsigned long long'
 *
 * @return 0 on success and -1 on failure
 */
int get_value(char *lim_optarg, unsigned long long *lim_val)
{
	int retval = 0;
	char *optstr = NULL;

	VE_RPMLIB_TRACE("Entering");

	if (!lim_optarg || !lim_val) {
		VE_RPMLIB_ERR("Wrong argument received:lim_optarg = %p," \
				" lim_val = %p", lim_optarg, lim_val);
		retval = -1;
		goto out;
	}
	optstr = lim_optarg;
	if (strncmp(optstr, "unlimited", sizeof("unlimited")) == 0) {
		*lim_val = RLIM_INFINITY;
		goto out;
	}
	while (*optstr >= '0' && *optstr <= '9')
		*lim_val = (*lim_val) * 10 + (*optstr++ - '0');
	VE_RPMLIB_DEBUG("Limit value spcified: %llu", *lim_val);
	if (*optstr != '\0') {
		VE_RPMLIB_ERR("Invalid limit value in optarg: %s", optstr);
		retval = -1;
	}
	VE_RPMLIB_TRACE("Exiting");

out:
	return retval;
}

/**
 * @brief Parse VE_LIMIT_OPT and fetch the resource limit
 *
 * @param limit_opt [in] Resource limit passed by user as environment
 * variable in "VE_LIMIT_OPT"
 * @param ve_rlim [out] To set the resource limit
 *
 * @return 0 on success and negative value on failure.
 */

int get_ve_limit_opt(char *limit_opt, struct rlimit *ve_rlim)
{
	int arg_c = 0;
	int opt = 0, limit = 0;
	int len_optind_arg = 0, len_optarg = 0;
	int retval = VE_EINVAL_LIMITOPT;
	bool repeat_lim[VE_RLIM_CNT] = {0};
	unsigned long long lim_val = 0;
	char *arg_v[VE_BUF_LEN] = {0};
	char *token = NULL;

	static const struct option longopts[] = {
		{ "hardc",        required_argument, NULL, 1 },
		{ "softc",     required_argument, NULL, 2 },
		{ "hardd",      required_argument, NULL, 3 },
		{ "softd",       required_argument, NULL, 4 },
		{ "hardi",        required_argument, NULL, 5 },
		{ "softi",       required_argument, NULL, 6 },
		{ "hardm",      required_argument, NULL, 7 },
		{ "softm",      required_argument, NULL, 8 },
		{ "hards",    required_argument, NULL, 9 },
		{ "softs",   required_argument, NULL, 10 },
		{ "hardt",       required_argument, NULL, 11 },
		{ "softt",     required_argument, NULL, 12 },
		{ "hardv",      required_argument, NULL, 13 },
		{ "softv",        required_argument, NULL, 14 },
		{ NULL, 0, NULL, 0 }
	};

	VE_RPMLIB_TRACE("Entering");
	if (!limit_opt || !ve_rlim) {
		VE_RPMLIB_ERR("Wrong argument received:limit_opt = %p," \
				" ve_rlim = %p", limit_opt, ve_rlim);
		goto out;
	}
	token = strtok(limit_opt, " ");
	arg_v[arg_c] = strndup("error", strlen("error"));
	if (!arg_v[arg_c]) {
		VE_RPMLIB_ERR("Failed to allocate memory");
		goto out;
	}
	arg_c++;

	/* Tokenize the value of VE_LIMIT_OPT environment variable */
	while (token != NULL) {
		arg_v[arg_c] = strndup(token, strlen(token));
		if (!arg_v[arg_c]) {
			VE_RPMLIB_ERR("Failed to allocate memory");
			goto out;
		}
		token = strtok(NULL, " ");
		arg_c++;
	}
	arg_v[arg_c] = '\0';
	optind = 1;
	/* Check the option specified with VE_LIMIT_OPT */
	while ((opt = getopt_long(arg_c, arg_v, "+:c:d:i:m:s:t:v:",
					longopts, NULL)) != -1) {
		/* If valid option is specified and no option
		 * argument is missing */
		if (opt != '?' && opt != ':') {
			lim_val = 0;
			retval = get_value(optarg, &lim_val);
			if (0 > retval) {
				VE_RPMLIB_ERR("Error in value conversion");
				retval = (VE_EINVAL_LIMITOPT);
				goto out_err;
			}
			retval = VE_EINVAL_LIMITOPT;
		}
		/* Validate the resource limit values*/
		if (opt == 'c' || opt == 'd' || opt == 'i' ||
				opt == 'm' || opt == 's' ||
				opt == 't' || opt == 'v') {
			len_optind_arg = strlen(arg_v[optind - 1]);
			len_optarg = strlen(optarg);
			if (strncmp(arg_v[optind-1], optarg,
						(len_optind_arg > len_optarg ?
						len_optind_arg : len_optarg)))
				goto out_err;
		}
		/* Validate RLIMIT_CPU resource limit's minimum value*/
		if ((lim_val == 0) && ((opt == 't') || (opt == SOFTT)))
			lim_val = 1;
		/*Resource limit value should not be greater than
		* than MAX_RESOURCE_LIMIT for c, d, m, s and
		* v resources */
		if (opt != HARDI && opt != SOFTI &&
			opt != HARDT && opt != SOFTT &&
			opt != 'i' && opt != 't' &&
			optarg != NULL &&
			strncmp(optarg, "unlimited", sizeof("unlimited"))) {
			if (lim_val > MAX_RESOURCE_LIMIT) {
				VE_RPMLIB_DEBUG("Resource limit out of range");
				retval = VE_ERANGE_LIMITOPT;
				goto out_err;
			}
			lim_val = lim_val * KB;
		}
		/* Only consider the first value if the same resource limit
		 * mentioned repeatedly */
		if (opt < VE_RLIM_CNT) {
			if (!repeat_lim[opt])
				repeat_lim[opt] = 1;
			else
				continue;
		}
		switch (opt) {
		case 1:
			ve_rlim[RLIMIT_CORE].rlim_max = lim_val;
			break;
		case 2:
			ve_rlim[RLIMIT_CORE].rlim_cur = lim_val;
			break;
		case 3:
			ve_rlim[RLIMIT_DATA].rlim_max = lim_val;
			break;
		case 4:
			ve_rlim[RLIMIT_DATA].rlim_cur = lim_val;
			break;
		case 5:
			ve_rlim[RLIMIT_SIGPENDING].rlim_max = lim_val;
			break;
		case 6:
			ve_rlim[RLIMIT_SIGPENDING].rlim_cur = lim_val;
			break;
		case 7:
			ve_rlim[RLIMIT_RSS].rlim_max = lim_val;
			break;
		case 8:
			ve_rlim[RLIMIT_RSS].rlim_cur = lim_val;
			break;
		case 9:
			ve_rlim[RLIMIT_STACK].rlim_max = lim_val;
			break;
		case 10:
			ve_rlim[RLIMIT_STACK].rlim_cur = lim_val;
			break;
		case 11:
			ve_rlim[RLIMIT_CPU].rlim_max = lim_val;
			break;
		case 12:
			ve_rlim[RLIMIT_CPU].rlim_cur = lim_val;
			break;
		case 13:
			ve_rlim[RLIMIT_AS].rlim_max = lim_val;
			break;
		case 14:
			ve_rlim[RLIMIT_AS].rlim_cur = lim_val;
			break;
		case 'c':
			if (!repeat_lim[SOFTC]) {
				ve_rlim[RLIMIT_CORE].rlim_cur = lim_val;
				repeat_lim[SOFTC] = 1;
			}
			if (!repeat_lim[HARDC]) {
				ve_rlim[RLIMIT_CORE].rlim_max = lim_val;
				repeat_lim[HARDC] = 1;
			}
			break;
		case 'd':
			if (!repeat_lim[SOFTD]) {
				ve_rlim[RLIMIT_DATA].rlim_cur = lim_val;
				repeat_lim[SOFTD] = 1;
			}
			if (!repeat_lim[HARDD]) {
				ve_rlim[RLIMIT_DATA].rlim_max = lim_val;
				repeat_lim[HARDD] = 1;
			}
			break;
		case 'i':
			if (!repeat_lim[SOFTI]) {
				ve_rlim[RLIMIT_SIGPENDING].rlim_cur = lim_val;
				repeat_lim[SOFTI] = 1;
			}
			if (!repeat_lim[HARDI]) {
				ve_rlim[RLIMIT_SIGPENDING].rlim_max = lim_val;
				repeat_lim[HARDI] = 1;
			}
			break;
		case 'm':
			if (!repeat_lim[SOFTM]) {
				ve_rlim[RLIMIT_RSS].rlim_cur = lim_val;
				repeat_lim[SOFTM] = 1;
			}
			if (!repeat_lim[HARDM]) {
				ve_rlim[RLIMIT_RSS].rlim_max = lim_val;
				repeat_lim[HARDM] = 1;
			}
			break;
		case 's':
			if (!repeat_lim[SOFTS]) {
				ve_rlim[RLIMIT_STACK].rlim_cur = lim_val;
				repeat_lim[SOFTS] = 1;
			}
			if (!repeat_lim[HARDS]) {
				ve_rlim[RLIMIT_STACK].rlim_max = lim_val;
				repeat_lim[HARDS] = 1;
			}
			break;
		case 't':
			if (!repeat_lim[SOFTT]) {
				ve_rlim[RLIMIT_CPU].rlim_cur = lim_val;
				repeat_lim[SOFTT] = 1;
			}
			if (!repeat_lim[HARDT]) {
				ve_rlim[RLIMIT_CPU].rlim_max = lim_val;
				repeat_lim[HARDT] = 1;
			}
			break;
		case 'v':
			if (!repeat_lim[SOFTV]) {
				ve_rlim[RLIMIT_AS].rlim_cur = lim_val;
				repeat_lim[SOFTV] = 1;
			}
			if (!repeat_lim[HARDV]) {
				ve_rlim[RLIMIT_AS].rlim_max = lim_val;
				repeat_lim[HARDV] = 1;
			}
			break;
		case '?':
			VE_RPMLIB_ERR("Unrecognized option");
			goto out_err;
		case ':':
			VE_RPMLIB_ERR("Missing option argument");
			goto out_err;
		}
	}
	/* For error checking, if any value is specified without any option */
	if (arg_v[optind]) {
		VE_RPMLIB_ERR("Invalid Value: %s", arg_v[optind]);
		retval = VE_EINVAL_LIMITOPT;
		goto out_err;
	}
	/* To validate that hard limit should be greater than its soft limit */
	for (limit = 0; limit < RLIM_NLIMITS; limit++) {
		if (ve_rlim[limit].rlim_cur > ve_rlim[limit].rlim_max) {
			VE_RPMLIB_DEBUG("lim: %d, soft lim: %llu, hard lim:" \
					" %llu", limit, ve_rlim[limit].rlim_cur,
					ve_rlim[limit].rlim_max);
			VE_RPMLIB_ERR("Soft limit is greater than hard limit");
			goto out_err;
		}
		VE_RPMLIB_DEBUG("limit: %d, soft lim: %llu, hard lim: %llu",
				limit, ve_rlim[limit].rlim_cur,
				ve_rlim[limit].rlim_max);
	}
	retval = 0;
	goto out;
out_err:
	VE_RPMLIB_ERR("Invalid input in VE_LIMIT_OPT");
out:
	arg_c--;
	while (arg_c >= 0) {
		free(arg_v[arg_c]);
		arg_c--;
	}
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief Fetch the resource limit.
 *
 * @param ve_rlim[out] resource limit
 *
 * @return 0 on success and negative value on failure.
 */

int get_ve_rlimit(struct rlimit *ve_rlim)
{
	int resource = 0;
	int limit_opt_length = 0;
	int retval = 0;
	char *limit_opt = NULL;
	struct rlimit *limit = ve_rlim;

	VE_RPMLIB_TRACE("Entering");
	while (resource < RLIM_NLIMITS) {
		switch (resource) {
			case RLIMIT_CPU:
			case RLIMIT_AS:
			case RLIMIT_CORE:
			case RLIMIT_DATA:
			case RLIMIT_SIGPENDING:
			case RLIMIT_RSS:
			case RLIMIT_FSIZE:
			case RLIMIT_LOCKS:
			case RLIMIT_MEMLOCK:
			case RLIMIT_MSGQUEUE:
			case RLIMIT_NOFILE:
			case RLIMIT_NPROC:
			case RLIMIT_RTTIME:
				getrlimit(resource, ve_rlim);
				break;
			default:
				break;
		}
		ve_rlim++;
		resource++;
	}
	ve_rlim = limit;
	/* Check for VE_LIMIT_OPT environment variable */
	limit_opt = getenv("VE_LIMIT_OPT");
	if (limit_opt) {
		limit_opt_length = strlen(limit_opt);
		/* If VE_LIMIT_OPT=<empty> */
		if(limit_opt_length) {
			char tmp[limit_opt_length];

			memcpy(tmp, limit_opt, limit_opt_length);
			tmp[limit_opt_length] = '\0';
			retval = get_ve_limit_opt(tmp, ve_rlim);
			if (retval < 0) {
				VE_RPMLIB_ERR("VE_LIMIT_OPT parsing failed");
				goto out;
			}
		}
	}
out:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will check status (online/offline) of given VE node.
 *
 * @param node_num[in] VE node number to check status.
 *
 * @return 0 on success and -1 on failure.
 */
int ve_check_node_status(int node_num)
{
	int retval = -1;
	char *ve_sock_name = NULL;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	/* Create the socket path corresponding to received VE node
	*/
	ve_sock_name = ve_create_sockpath(node_num);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}

	/* Create the socket connection corresponding to socket path
	*/
	if (0 > velib_sock(ve_sock_name)) {
		VE_RPMLIB_ERR("Failed to create socket [%s]: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	}
	retval = 0;

hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will create a new VE process on given VE node
 *
 * @param nodeid[in] Create process on given node number
 * @param pid[in] Create process of given PID at VE
 * @param flag[in] Identifier as specified in "enum create_task_flag"
 * @param numa_num[in] NUMA node number
 * @param membind_flag[in] Flag to indicate memory policy.
 * @param set[in] CPU mask for dummy VE process.
 *
 * @return PID of created process on success and -1 on failure
 */
int ve_create_process(int nodeid, int pid, int flag, int numa_num,
			int membind_flag, cpu_set_t *set)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	VelibConnect *res = NULL;
	ProtobufCBinaryData subreq = {0};
	VelibConnect request = VELIB_CONNECT__INIT;
	char *ve_dev_filename = NULL;
	struct velib_create_process ve_create_proc = {0};
	struct stat sb = {0};
	int fd = -1;

	VE_RPMLIB_TRACE("Entering : %s", __func__);
	errno = 0;
	/* Create the socket path corresponding to received VE node
	*/
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}

	/* Create the socket connection corresponding to socket path
	*/
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket: %s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	ve_dev_filename = (char *)malloc(sizeof(char) * VE_FILE_NAME);
	if (!ve_dev_filename) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return_sock;
	}
	sprintf(ve_dev_filename, "%s/%s%d", DEV_PATH, VE_DEVICE_NAME, nodeid);

	fd = open(ve_dev_filename, O_RDWR);
	if (fd < 0) {
		VE_RPMLIB_ERR("Couldn't open file (%s): %s",
				ve_dev_filename, strerror(errno));
		goto hndl_return1;
	}

	retval = fstat(fd, &sb);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get file status(%s): %s",
				ve_dev_filename, strerror(errno));
		close(fd);
		goto hndl_return1;
	}
	memset(ve_create_proc.ve_rlim, -1,
		sizeof(ve_create_proc.ve_rlim));
	/* To set the resource limit */
	retval = get_ve_rlimit(ve_create_proc.ve_rlim);
	if (retval < 0) {
		VE_RPMLIB_ERR("Failed to set resource limit");
		goto hndl_return1;
	}

	ve_create_proc.vedl_fd = fd;
	ve_create_proc.flag = flag;
	ve_create_proc.numa_num = numa_num;
	if (!membind_flag)
		ve_create_proc.membind_flag = MPOL_BIND;
	else
		ve_create_proc.membind_flag = MPOL_DEFAULT;
	VE_RPMLIB_DEBUG("flag:%d, fd:%d, numa: %d, membind_flag: %d",
			ve_create_proc.flag, ve_create_proc.vedl_fd,
			ve_create_proc.numa_num, ve_create_proc.membind_flag);
	if (set) {
		memcpy(&ve_create_proc.set, set, sizeof(ve_create_proc.set));
		ve_create_proc.cpu_mask_flag = true;
		VE_RPMLIB_DEBUG("CPU count in mask: %d",
				CPU_COUNT(&ve_create_proc.set));
	} else {
		ve_create_proc.cpu_mask_flag = false;
	}
	ve_create_proc.ppid = getppid();
	subreq.data = (uint8_t *)&ve_create_proc;
	subreq.len = sizeof(struct velib_create_process);

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_CREATE_PROCESS;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;
	request.has_ve_pid = true;
	request.ve_pid = pid;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}

	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s", strerror(errno));
		goto hndl_return1;
	}
	memset(pack_buf_send, '\0', pack_msg_len);
	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
			" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	*/
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 > retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return3;
	}
	/* Function will return success, if expected return value is
	 * received from VEOS
	 */
	VE_RPMLIB_DEBUG("Received message from VEOS and retval = %d", retval);
	goto hndl_return3;
abort:
	close(sock_fd);
	abort();
hndl_return3:
	velib_connect__free_unpacked(res, NULL);
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	free(ve_dev_filename);
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will check that pid is running on given VE node or not
 *
 * @param nodeid[in] VE node number
 *
 * @param pid[in] Process ID
 *
 * @return 0 or 1 on success and -1 on failure. 0 to indicate a valid VE PID
 * and 1 indicates process not exists on specified node
 */
int ve_check_pid(int nodeid, int pid)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}

	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket: %s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_CHECKPID;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_ve_pid = true;
	request.ve_pid = pid;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}

	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s", strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s", strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s", strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	if (0 == retval	|| VE_VALID_THREAD == retval) {
		VE_RPMLIB_DEBUG("Received PID (%d) from VEOS and retval %d",
				pid, retval);
		errno = 0;
	} else if (VEO_PROCESS_EXIST == retval) {
		VE_RPMLIB_DEBUG("VEOS returned = %d", retval);
		errno = -ESRCH;
	} else
		VE_RPMLIB_ERR("Received return value from veos= %d", retval);
		errno = -(retval);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the memory information of given VE node
 *
 * @param nodeid[in] VE node number
 * @param ve_meminfo_req[out] Structure to store memory information
 * of given VE node
 *
 * @return 0 on success and -1 on failure
 */
int ve_mem_info(int nodeid, struct ve_meminfo *ve_meminfo_req)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct velib_meminfo lib_meminfo = {0};
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!ve_meminfo_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_meminfo_req = %p",
						ve_meminfo_req);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}

	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket: %s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_MEM_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}

	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s", strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}
	memcpy(&lib_meminfo, res->rpm_msg.data, res->rpm_msg.len);
	VE_RPMLIB_DEBUG("Received message from VEOS and retval = %d", retval);

	/* Populate the argument used to store the memory information with
	 * the values received from VEOS
	 */
	memset(ve_meminfo_req, '\0', sizeof(struct ve_meminfo));
	ve_meminfo_req->kb_main_total = lib_meminfo.kb_main_total / VKB;
	ve_meminfo_req->kb_main_used = lib_meminfo.kb_main_used / VKB;
	ve_meminfo_req->kb_main_free = lib_meminfo.kb_main_free / VKB;
	ve_meminfo_req->kb_main_shared = lib_meminfo.kb_main_shared / VKB;
	ve_meminfo_req->kb_hugepage_used = lib_meminfo.kb_hugepage_used / VKB;
	VE_RPMLIB_DEBUG("Received message from VEOS and values are " \
			"as follows:kb_main_total = %lu, kb_main_used " \
			"= %lu, kb_main_free = %lu, kb_main_shared = %lu, " \
			"kb_hugepage_used=%lu",
			ve_meminfo_req->kb_main_total,
			ve_meminfo_req->kb_main_used,
			ve_meminfo_req->kb_main_free,
			ve_meminfo_req->kb_main_shared,
			ve_meminfo_req->kb_hugepage_used);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function is used to get uptime information.
 *
 * @param nodeid[in] VE node number
 * @param uptime_secs[out] Value of uptime
 *
 * @return 0 on success and -1 on failure
 */
int ve_uptime_info(int nodeid, double *uptime_secs)
{
	int retval = -1;
	struct ve_statinfo ve_statinfo_req = { {0} };

	VE_RPMLIB_TRACE("Entering");
	if (!uptime_secs) {
		VE_RPMLIB_ERR("Wrong argument received: uptime_secs = %p",
						uptime_secs);
		errno = EINVAL;
		goto hndl_return;
	}
	if (0 > ve_stat_info(nodeid, &ve_statinfo_req)) {
		VE_RPMLIB_ERR("Failed to get CPU statistics: %s",
				strerror(errno));
		goto hndl_return;
	}
	*uptime_secs = (((double)ve_statinfo_req.user[0] 
			+ (double)ve_statinfo_req.idle[0]) 
			/ MICROSEC_TO_SEC);
	retval = 0;
	VE_RPMLIB_DEBUG("Value of uptime for VE node (%d): %f",
					nodeid, *uptime_secs);

hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the CPU statistics for given VE node.
 * This includes CPU statistics about all cores of given node
 *
 * @param nodeid[in] VE node number corresponding to which CPU statistics will be
 * extracted from VEOS
 * @param ve_statinfo_req[out] Structure of RPM Source to provide information
 * received from VEOS
 *
 * @return 0 on success and -1 on failure
 */
int ve_stat_info(int nodeid, struct ve_statinfo *ve_statinfo_req)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	int core_loop = -1;
	int numcore = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct velib_statinfo lib_statinfo = { {0} };
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!ve_statinfo_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_statinfo_req = %p",
				ve_statinfo_req);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}

	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_STAT_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}

	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}
	memcpy(&lib_statinfo, res->rpm_msg.data, res->rpm_msg.len);
	VE_RPMLIB_DEBUG("Received message from VEOS and retval = %d", retval);

	memset(ve_statinfo_req, '\0', sizeof(struct ve_statinfo));
	/* Get the cores corresponding to given node
	 */
	if (-1 == ve_core_info(nodeid, &numcore)) {
		VE_RPMLIB_ERR("Failed to get CPU cores: %s",
				strerror(errno));
		goto hndl_return4;
	}
	/* Populate the structure used to store the process statistics, with
	 * the values received from VEOS
	 */
	VE_RPMLIB_DEBUG("Received message from VEOS and values are as follows:");
	for (core_loop = 0; core_loop < numcore; core_loop++) {
		ve_statinfo_req->user[core_loop] = lib_statinfo.user[core_loop];
		ve_statinfo_req->idle[core_loop] = lib_statinfo.idle[core_loop];
		VE_RPMLIB_DEBUG("user[%d] = %llu,  idle[%d] = %llu",
				core_loop, ve_statinfo_req->user[core_loop],
				core_loop, ve_statinfo_req->idle[core_loop]);
	}
	ve_statinfo_req->ctxt = lib_statinfo.ctxt;
	ve_statinfo_req->running = lib_statinfo.running;
	ve_statinfo_req->blocked = lib_statinfo.blocked;
	ve_statinfo_req->btime = lib_statinfo.btime;
	ve_statinfo_req->processes = lib_statinfo.processes;
	ve_statinfo_req->intr = 0;
	VE_RPMLIB_DEBUG("ctxt = %u  running = %u" \
			"blocked = %u  btime = %lu  processes = %u",
			ve_statinfo_req->ctxt, ve_statinfo_req->running,
			ve_statinfo_req->blocked, ve_statinfo_req->btime,
			ve_statinfo_req->processes);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS to enable and
 * disable the process accounting
 *
 * @param nodeid[in] Enable/disable the process accounting on given node
 * @param filename[in] File used to record the accounting data
 *
 * @return 0 on success and -1 on failure
 */
int ve_acct(int nodeid, char *filename)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	char *ret = NULL;
	char abs_pathname[VE_PATH_MAX + 1] = {0};
	ProtobufCBinaryData subreq = {0};
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}

	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.has_subcmd_str = true;
	request.subcmd_str = VE_ACCTINFO;
	request.cmd_str = RPM_QUERY;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();

	if (filename != NULL) {
		ret = realpath(filename, abs_pathname);
		if (!ret) {
			VE_RPMLIB_ERR("Failed to get real path of file :%s," \
					" error: %s", filename,
						strerror(errno));
			goto hndl_return_sock;
		}
		VE_RPMLIB_DEBUG("This file is at %s", abs_pathname);

		request.has_rpm_msg = true;
		subreq.data = (uint8_t *)abs_pathname;
		subreq.len = strlen(abs_pathname);
		request.rpm_msg = subreq;
	} else {
		 VE_RPMLIB_DEBUG("Passed filename as NULL to turn off accounting");
	}

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}

	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}
	/* Function will return success, if expected return value is
	 * from VEOS
	 */
	VE_RPMLIB_DEBUG("Received message from VEOS and retval = %d", retval);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and get the
 * information about load average
 *
 * @param nodeid[in] Get the load average of given VE node
 * @param ve_loadavg_req[out] Structure to get the load average information
 *
 * @return 0 on success and -1 on failure
 */
int ve_loadavg_info(int nodeid, struct ve_loadavg *ve_loadavg_req)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct ve_loadavg lib_loadavg = {0};
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!ve_loadavg_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_loadavg_req = %p",
						ve_loadavg_req);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}

	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_LOAD_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}

	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	memset(pack_buf_send, '\0', pack_msg_len);
	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}

	memcpy(&lib_loadavg, res->rpm_msg.data, res->rpm_msg.len);
	VE_RPMLIB_DEBUG("Received message from VEOS and retval = %d", retval);

	/* Populate the structure used to store the load average information,
	 * with the values received from VEOS
	 */
	ve_loadavg_req->av_1 = lib_loadavg.av_1;
	ve_loadavg_req->av_5 = lib_loadavg.av_5;
	ve_loadavg_req->av_15 = lib_loadavg.av_15;
	ve_loadavg_req->runnable = lib_loadavg.runnable;
	ve_loadavg_req->total_proc = lib_loadavg.total_proc;

	VE_RPMLIB_DEBUG("Received message from VEOS and values" \
			" are as follows:av_1 = %lf,  av_5 = %lf," \
			"  av_15 = %lf,  runnable=%d,  total_proc=%d",
			ve_loadavg_req->av_1, ve_loadavg_req->av_5,
			ve_loadavg_req->av_15, ve_loadavg_req->runnable,
			ve_loadavg_req->total_proc);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to get the number of cores for given VE
 * node
 *
 * @param nodeid[in] VE node number
 * @param numcore[out] Number of cores
 *
 * @return 0 on success and -1 of failure
 */
int ve_core_info(int nodeid, int *numcore)
{
	FILE *fp = NULL;
	int retval = -1;
	int cntr = 0;
	int indx = 0;
	char *tmp = NULL;
	uint64_t valid_cores = 0;
	char *endptr = NULL;
	char core_file[PATH_MAX] = {0};
	const char ve_sysfs_path[PATH_MAX] = {0};
	char valid_cores_from_file[LINE_MAX] = {0};

	VE_RPMLIB_TRACE("Entering");

	if (!numcore) {
		VE_RPMLIB_ERR("Wrong argument received: numcore = %p",
				numcore);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Get sysfs path corresponding to given VE node */
	retval = ve_sysfs_path_info(nodeid, ve_sysfs_path);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get sysfs path: %s",
				strerror(errno));
		goto hndl_return;
	}
	retval = -1;
	snprintf(core_file, sizeof(core_file), "%s/cores_enable", ve_sysfs_path);

	fp = fopen(core_file, "r");
	if (fp == NULL) {
		VE_RPMLIB_ERR("Fails to open file(%s): %s",
				core_file, strerror(errno));
		goto hndl_return;
	}

	if (fgets(valid_cores_from_file, sizeof(valid_cores_from_file), fp)
			== NULL) {
		VE_RPMLIB_ERR("Failed to gets valid cores:%s",
				strerror(errno));
		fclose(fp);
		goto hndl_return;
	}
	tmp = strchr(valid_cores_from_file, '\n');
	if (tmp != NULL)
		*tmp = '\0';
	fclose(fp);

	errno = 0;
	valid_cores = (uint64_t)strtoul(valid_cores_from_file, &endptr, 16);
	if (errno != 0) {
		VE_RPMLIB_ERR("Failed to get valid core number %s",
				strerror(errno));
		goto hndl_return;
	}
	if (*endptr != '\0') {
		VE_RPMLIB_ERR("Invalid valid_cores number[%s][%s].",
				valid_cores_from_file, endptr);
		errno = EINVAL;
		goto hndl_return;
	}
	VE_RPMLIB_DEBUG("valid_cores: %ld", valid_cores);

	for (cntr = 0, indx = 0; indx < VE_MAX_CORE_PER_NODE; indx++) {
		if ((valid_cores & (1 << indx)) == 0)
			continue;
		cntr++;

	}
	*numcore = cntr;
	retval = 0;
	VE_RPMLIB_DEBUG("Mapped core num: %d", *numcore);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and get the
 * process's CPU affinity mask for given VE node
 *
 * @param nodeid[in] VE node number
 * @param pid[in] Process ID
 * @param cpusetsize[in] The length (in bytes) of the data pointed to by 'mask'
 * @param mask[in] To get the CPU affinity mask of the given process
 *
 * @return 0 on success and -1 on failure
 */
int ve_sched_getaffinity(int nodeid, pid_t pid,
				size_t cpusetsize, cpu_set_t *mask)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct velib_affinity ve_affinity = {0};
	VelibConnect *res = NULL;
	ProtobufCBinaryData subreq = {0};
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!mask) {
		VE_RPMLIB_ERR("Wrong argument received: mask = %p", mask);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	ve_affinity.cpusetsize = cpusetsize;

	subreq.data = (uint8_t *)&ve_affinity;
	subreq.len = sizeof(struct velib_affinity);

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_GET_AFFINITY;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;
	request.has_ve_pid = true;
	request.ve_pid = pid;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);
	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}
	/* Populate the structure used to store information, with the values
	 * received from VEOS
	 */
	memcpy(&ve_affinity, res->rpm_msg.data, res->rpm_msg.len);

	memcpy(mask, &ve_affinity.mask, cpusetsize);
	VE_RPMLIB_DEBUG("Message received successfully from VEOS" \
			" and retval = %d,  cpusetsize = %zu", retval,
						cpusetsize);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and set
 * a process's CPU affinity mask for given VE node
 *
 * @param nodeid[in] VE node number
 * @param pid[in] Process ID
 * @param cpusetsize[in] The length (in bytes) of the data pointed to by 'mask'
 * @param mask[in] To set the CPU affinity mask of the given process.
 *
 * @return 0 on success and -1 on failure
 */
int ve_sched_setaffinity(int nodeid, pid_t pid, size_t cpusetsize,
							cpu_set_t *mask)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct velib_affinity ve_affinity = {0};
	VelibConnect *res = NULL;
	ProtobufCBinaryData subreq = {0};
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!mask) {
		VE_RPMLIB_ERR("Wrong argument received: mask = %p", mask);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	memcpy(&ve_affinity.mask, mask, cpusetsize);
	ve_affinity.cpusetsize = cpusetsize;

	subreq.data = (uint8_t *)&ve_affinity;
	subreq.len = sizeof(struct velib_affinity);

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_SET_AFFINITY;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;
	request.has_ve_pid = true;
	request.ve_pid = pid;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);
	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}
	/* Function will return success, if expected return value is
	 * received from VEOS
	 */
	VE_RPMLIB_DEBUG("Message received successfully from VEOS" \
			" and retval = %d", retval);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and get/set
 * resource limit of VE process for given VE node
 *
 * @param nodeid[in] VE node number
 * @param pid[in] Process ID
 * @param resource[in] Resources corresponding to VE process for which limits
 * needs to be get or set
 * @param new_limit[in] To get the new soft and hard limits for given resource
 * from VEOS, in case of not NULL
 * @param old_limit[in/out] To get the previous soft and hard limits for given
 * resource from VEOS, in case of not NULL
 *
 * @return 0 on success and -1 on failure
 */
int ve_prlimit(int nodeid, pid_t pid, int resource, struct rlimit *new_limit,
		struct rlimit *old_limit)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct velib_prlimit ve_limit = {0};
	VelibConnect *res = NULL;
	ProtobufCBinaryData subreq = {0};
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!old_limit && !new_limit) {
		VE_RPMLIB_ERR("Wrong argument received: old_limit = %p" \
				" new_limit = %p", old_limit, new_limit);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	ve_limit.resource = resource;

	if (!new_limit) {
		ve_limit.old_limit.rlim_cur = old_limit->rlim_cur;
		ve_limit.old_limit.rlim_max = old_limit->rlim_max;
		ve_limit.is_new_lim = false;
	}
	else {
		VE_RPMLIB_DEBUG("new_limit = %p,  old_limit = %p",
				new_limit, old_limit);
		ve_limit.new_limit.rlim_cur = new_limit->rlim_cur;
		ve_limit.new_limit.rlim_max = new_limit->rlim_max;
		ve_limit.is_new_lim = true;
	}

	subreq.data = (uint8_t *)&ve_limit;
	subreq.len = sizeof(struct velib_prlimit);

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_PRLIMIT;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;
	request.has_ve_pid = true;
	request.ve_pid = pid;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);
	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}

	/* Populate the structure used to store information, with the values
	 * received from VEOS
	 */
	if (!new_limit) {
		memcpy(&ve_limit, res->rpm_msg.data, res->rpm_msg.len);
		old_limit->rlim_cur = ve_limit.old_limit.rlim_cur;
		old_limit->rlim_max = ve_limit.old_limit.rlim_max;

		VE_RPMLIB_DEBUG("Received message from VEOS and values are" \
				" as follows:retval = %d,  " \
				"old_limit->rlim_cur = %lld,  " \
				"old_limit->rlim_max = %lld", retval,
				(long long)old_limit->rlim_cur,
				(long long)old_limit->rlim_max);
			goto hndl_return4;

	}
	VE_RPMLIB_DEBUG("Message received successfully from VEOS" \
			" and retval = %d", retval);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the virtual memory information of VE node
 *
 * @param nodeid[in] VE node number
 * @param ve_vmstatinfo_req[out] Structure to get virtual memory statistics
 *
 * @return 0 on success and -1 on failure
 */
int ve_vmstat_info(int nodeid, struct ve_vmstat *ve_vmstatinfo_req)
{

	int retval = -1;

	VE_RPMLIB_TRACE("Entering");
	if (!ve_vmstatinfo_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_vmstatinfo_req = %p",
				ve_vmstatinfo_req);
		errno = EINVAL;
		goto hndl_return;
	}
	memset(ve_vmstatinfo_req, '\0', sizeof(struct ve_vmstat));
	retval = 0;

hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and get the
 * memory map information for given PID on given VE node
 *
 * @param nodeid[in] VE node number
 * @param pid[in] Process ID for which memory map is required
 * @param length[out] Length of received information
 * @param filename[out] Filename which consists of required information
 *
 * @return 0 on success and -1 on failure
 */
int ve_map_info(int nodeid, pid_t pid, unsigned int *length, char *filename)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct file_info fileinfo = {0};
	VelibConnect *res = NULL;
	ProtobufCBinaryData subreq = {0};
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;

	if (!length || !filename) {
		VE_RPMLIB_ERR("Wrong argument received:length=%p, filename=%p",
					length, filename);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	fileinfo.nodeid = nodeid;
	subreq.data = (uint8_t *)&fileinfo;
	subreq.len = sizeof(struct file_info);
	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_MAP_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_ve_pid = true;
	request.ve_pid = pid;
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);
	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return3;
	}
	memcpy(&fileinfo, res->rpm_msg.data, res->rpm_msg.len);
	VE_RPMLIB_DEBUG("Received message from VEOS and values are" \
			" as follows:length = %u,  filename = %s",
			fileinfo.length, fileinfo.file);
	*length = fileinfo.length;
	/* Create the file path from received file name */
	if (*length) {
		memset(filename, '\0', VE_PATH_MAX);
		sprintf(filename, "%s/veos%d-tmp/%s",
				VE_SOC_PATH, nodeid, fileinfo.file);
		VE_RPMLIB_DEBUG("Read information from '%s' file", filename);
	}

	goto hndl_return3;
abort:
	close(sock_fd);
	abort();
hndl_return3:
	velib_connect__free_unpacked(res, NULL);
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and get the
 * status information of process for given VE node
 *
 * @param nodeid[in] VE node number for which process status needs to get
 * @param pid[in] Process ID
 * @param ve_pidstatus_req[out] Populate structure with status information of
 * VE process
 *
 * @return 0 on success and -1 on failure
 */
int ve_pidstatus_info(int nodeid, pid_t pid,
			struct ve_pidstatus *ve_pidstatus_req)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct velib_pidstatus pidstatus = {0};
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!ve_pidstatus_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_pidstatus_req = %p",
				ve_pidstatus_req);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_PIDSTATUS_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_ve_pid = true;
	request.ve_pid = pid;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);

	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}

	/* Populate the structure used to store the process's status
	 * information with the values received from VEOS
	 */
	memcpy(&pidstatus, res->rpm_msg.data, res->rpm_msg.len);
	ve_pidstatus_req->nvcsw = pidstatus.nvcsw;
	ve_pidstatus_req->nivcsw = pidstatus.nivcsw;
	ve_pidstatus_req->vm_swap = 0;
	ve_pidstatus_req->nvcsw = pidstatus.nvcsw;
	ve_pidstatus_req->blocked = pidstatus.blocked;
	ve_pidstatus_req->sigignore = pidstatus.sigignore;
	ve_pidstatus_req->sigcatch = pidstatus.sigcatch;
	ve_pidstatus_req->sigpnd = pidstatus.sigpnd;
	strncpy(ve_pidstatus_req->cmd, pidstatus.cmd, FILENAME + 1);
        ve_pidstatus_req->cmd[FILENAME + 1] = '\0';

	VE_RPMLIB_DEBUG("Received message from VEOS and values are" \
			" as follows:ve_pidstatus_req->nvcsw = %lu,  " \
			"ve_pidstatus_req->nivcsw = %lu, "	\
			"ve_pidstatus_req->blocked = %lld, "	\
			"ve_pidstatus_req->sigignore = %lld, "	\
			"ve_pidstatus_req->sigcatch = %lld, "	\
			"ve_pidstatus_req->sigpnd = %lld,  cmd = %s ",
			ve_pidstatus_req->nvcsw, ve_pidstatus_req->nivcsw,
			ve_pidstatus_req->blocked, ve_pidstatus_req->sigignore,
			ve_pidstatus_req->sigcatch, ve_pidstatus_req->sigpnd,
			ve_pidstatus_req->cmd);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;

}

/**
 * @brief This function will be used to communicate with VEOS and get the
 * given VE process's statistics
 *
 * @param nodeid[in] VE node number on which given process is running
 * @param pid[in] Process ID
 * @param ve_pidstat_req[out] Structure to populate the process's statistics
 *
 * @return 0 on success and -1 on failure
 */
int ve_pidstat_info(int nodeid, pid_t pid, struct ve_pidstat *ve_pidstat_req)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct velib_pidstat lib_pidstat = {0};
	VelibConnect *res = NULL;
	ProtobufCBinaryData subreq = {0};
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!ve_pidstat_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_pidstat_req = %p",
				ve_pidstat_req);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	lib_pidstat.whole = ve_pidstat_req->whole;
	subreq.data = (uint8_t *)&lib_pidstat;
	subreq.len = sizeof(struct velib_pidstat);
	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_PIDSTAT_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_ve_pid = true;
	request.ve_pid = pid;
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;


	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}

	memcpy(&lib_pidstat, res->rpm_msg.data, res->rpm_msg.len);
	/* Populate the structure used to store the process statistics, with
	 * the values received from VEOS
	 */
	memset(ve_pidstat_req, '\0', sizeof(struct ve_pidstat));
	ve_pidstat_req->state = lib_pidstat.state;
	ve_pidstat_req->processor = lib_pidstat.processor;
	ve_pidstat_req->priority = lib_pidstat.priority;
	ve_pidstat_req->nice = lib_pidstat.nice;
	ve_pidstat_req->policy = lib_pidstat.policy;
	ve_pidstat_req->utime = lib_pidstat.utime;
	ve_pidstat_req->cutime = lib_pidstat.cutime;
	ve_pidstat_req->flags = lib_pidstat.flags;
	ve_pidstat_req->vsize = lib_pidstat.vsize;
	ve_pidstat_req->rsslim = lib_pidstat.rsslim;
	ve_pidstat_req->startcode = lib_pidstat.startcode;
	ve_pidstat_req->endcode = lib_pidstat.endcode;
	ve_pidstat_req->startstack = lib_pidstat.startstack;
	ve_pidstat_req->kstesp = lib_pidstat.kstesp;
	ve_pidstat_req->ksteip = lib_pidstat.ksteip;
	ve_pidstat_req->rss = lib_pidstat.rss;
	strncpy(ve_pidstat_req->cmd, lib_pidstat.cmd, FILENAME + 1);
	ve_pidstat_req->cmd[FILENAME + 1] = '\0';
	ve_pidstat_req->start_time = lib_pidstat.start_time;
	ve_pidstat_req->tgid = lib_pidstat.tgid;
	VE_RPMLIB_DEBUG("Received message from VEOS and values are" \
			" as follows:state = %c\tprocessor = %d\t" \
			" priority = %ld\tnice = %ld\tpolicy = %u\t"	\
			"utime = %llu\tcutime = %llu\t"	\
			"flags = %lu\tvsize = %lu\trsslim = %lu\t"	\
			"startcode = %lu\tendcode = %lu\tstartstack = %lu\t"	\
			"kstesp = %lu\tksteip = %lu\trss = %ld\tcmd = %s\t"	\
			"start_time = %lu",
			ve_pidstat_req->state,
			ve_pidstat_req->processor, ve_pidstat_req->priority,
			ve_pidstat_req->nice, ve_pidstat_req->policy,
			ve_pidstat_req->utime,
			ve_pidstat_req->cutime,	ve_pidstat_req->flags,
			ve_pidstat_req->vsize, ve_pidstat_req->rsslim,
			ve_pidstat_req->startcode, ve_pidstat_req->endcode,
			ve_pidstat_req->startstack, ve_pidstat_req->kstesp,
			ve_pidstat_req->ksteip, ve_pidstat_req->rss,
			ve_pidstat_req->cmd, ve_pidstat_req->start_time);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and get the
 * register values of a process according to the IDs in regid.
 *
 * @param nodeid[in] VE node number
 * @param pid[in] PID of VE process
 * @param numregs[in] number of registers to retrieve
 * @param regid[in] int array with register indices to retrieve
 * @param regval[out] uint64_t array filled with retrieved register values
 *
 * @return 0 on success and -1 on failure
 */
int ve_get_regvals(int nodeid, pid_t pid, int numregs, int *regid, uint64_t *regval)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	VelibConnect *res = NULL;
	ProtobufCBinaryData subreq = {0};
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!regid || !regval) {
		VE_RPMLIB_ERR("Wrong argument received: regid = %p, regval = %p",
				regid, regval);
		errno = EINVAL;
		goto hndl_return;
	}

	if (numregs < 1 || numregs > VE_MAX_REGVALS) {
		VE_RPMLIB_ERR("Wrong argument received: numregs = %d",
			      numregs);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	subreq.data = (uint8_t *)regid;
	subreq.len = sizeof(int) * numregs;
	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_GET_REGVALS;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_ve_pid = true;
	request.ve_pid = pid;
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;


	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return3;
	}

	memcpy((void *)regval, res->rpm_msg.data, res->rpm_msg.len);
	/* Populate the structure used to store the process statistics, with
	 * the values received from VEOS
	 */
	VE_RPMLIB_DEBUG("Received %d regvals message from VEOS",
			res->rpm_msg.len / sizeof(uint64_t));
	goto hndl_return3;
abort:
	close(sock_fd);
	abort();
hndl_return3:
	velib_connect__free_unpacked(res, NULL);
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the CPU information for given VE node
 *
 * @param nodeid[in] VE node number
 * @param cpu_info[out] Structure to store CPU information for given VE node
 *
 * @return 0 on success and -1 on failure
 */
int ve_cpu_info(int nodeid, struct ve_cpuinfo *cpu_info)
{
	FILE *fp = NULL;
	int retval = -1;
	int nread = 0;
	int numcore = -1;
	int cache_loop = 0;
	int syspath_len = 0;
	char *filename = NULL;
	int cache_name_len = 0;
	int ve_cache_size[VE_MAX_CACHE] = {0};
	char ve_cache_name[VE_MAX_CACHE][VE_BUF_LEN] = { {0} };
	const char ve_sysfs_path[PATH_MAX] = {0};

	VE_RPMLIB_TRACE("Entering");

	if (!cpu_info) {
		VE_RPMLIB_ERR("Wrong argument received: cpu_info = %p",
						cpu_info);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Get the cores corresponding to given node
	 */
	if (-1 == ve_core_info(nodeid, &numcore)) {
		VE_RPMLIB_ERR("Failed to get CPU cores: %s", strerror(errno));
		goto hndl_return;
	}
	cpu_info->cores = numcore;
	cpu_info->core_per_socket = numcore;
	/* Get the cache details corresponding to given node
	 */
	if (-1 == ve_cache_info(nodeid, ve_cache_name, ve_cache_size)) {
		VE_RPMLIB_ERR("Failed to get cache information");
		goto hndl_return;
	}
	for (cache_loop = 0; cache_loop < VE_MAX_CACHE; cache_loop++) {
		/* Get the length of cache name */
		cache_name_len = sizeof(ve_cache_name[cache_loop]);
		strncpy(cpu_info->cache_name[cache_loop],
			ve_cache_name[cache_loop], cache_name_len);
		cpu_info->cache_name[cache_loop][cache_name_len - 1] = '\0';

		cpu_info->cache_size[cache_loop] = ve_cache_size[cache_loop];

		VE_RPMLIB_DEBUG("Value of cache_name[%d]= %s\tcache_size[%d]= %d",
				cache_loop, cpu_info->cache_name[cache_loop],
				cache_loop, cpu_info->cache_size[cache_loop]);
	}

	/* Get sysfs path corresponding to given VE node */
	retval = ve_sysfs_path_info(nodeid, ve_sysfs_path);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get sysfs path: %s",
				strerror(errno));
		goto hndl_return;
	}
	retval = -1;
	/* Get syspath length */
	syspath_len = strlen(ve_sysfs_path);
	filename = (char *)malloc(syspath_len + VE_FILE_NAME);
	if (!filename) {
		VE_RPMLIB_ERR("Memory allocation failed for filename: %s",
				strerror(errno));
		goto hndl_return;
	}

	memset(filename, '\0', (syspath_len + VE_FILE_NAME));
	/* Get the 'cpu_family' from VE specific sysfs
	 */
	sprintf(filename, "%s/model", ve_sysfs_path);
	fp = fopen(filename, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Failed to open file(%s): %s",
				filename, strerror(errno));
		goto hndl_return2;
	}

	nread = fscanf(fp, "%s", cpu_info->family);
	if (nread == EOF || nread != 1) {
		VE_RPMLIB_ERR("Failed to read file(%s): %s",
				filename, strerror(errno));
		fclose(fp);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("cpu family = %s", cpu_info->family);
	fclose(fp);

	memset(filename, '\0', (syspath_len + VE_FILE_NAME));
	/* Get the product type from VE specific sysfs
	 */
	sprintf(filename, "%s/type", ve_sysfs_path);
	fp = fopen(filename, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Failed to open file(%s): %s",
				filename, strerror(errno));
		goto hndl_return2;
	}

	nread = fscanf(fp, "%s", cpu_info->model);
	if (nread == EOF || nread != 1) {
		VE_RPMLIB_ERR("Failed to read file(%s): %s",
				filename, strerror(errno));
		fclose(fp);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Model = %s", cpu_info->model);
	fclose(fp);

	/* Get the model name from VE specific sysfs
	 */
	snprintf(cpu_info->modelname, sizeof(cpu_info->modelname),
			"VE_%s_%s", cpu_info->family, cpu_info->model);
	VE_RPMLIB_DEBUG("Model name = %s", cpu_info->modelname);

	memset(filename, '\0', (syspath_len + VE_FILE_NAME));
	/* Get the vendor ID from VE specific sysfs
	 */
	sprintf(filename, "%s/device/vendor", ve_sysfs_path);
	fp = fopen(filename, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Failed to open file(%s): %s",
				filename, strerror(errno));
		goto hndl_return2;
	}

	nread = fscanf(fp, "%s", cpu_info->vendor);
	if (nread == EOF || nread != 1) {
		VE_RPMLIB_ERR("Failed to read file(%s): %s",
				filename, strerror(errno));
		fclose(fp);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Vendor ID = %s", cpu_info->vendor);
	fclose(fp);

	/* These values will be fixed for a VE node */
	cpu_info->thread_per_core = 1;
	cpu_info->socket = 1;
	strcpy(cpu_info->stepping, "0");
	VE_RPMLIB_DEBUG("Thread per core = %d\t" \
			"number of sockets = %d\tstepping = %s",
			cpu_info->thread_per_core, cpu_info->socket,
			cpu_info->stepping);

	memset(filename, '\0', (syspath_len + VE_FILE_NAME));
	/* Get the value of BOGOMIPS from VE specific sysfs
	 */
	sprintf(filename, "%s/clock_chip", ve_sysfs_path);
	fp = fopen(filename, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Failed to open file(%s): %s",
				filename, strerror(errno));
		goto hndl_return2;
	}
	nread = fscanf(fp, "%s", cpu_info->bogomips);
	if (nread == EOF || nread != 1) {
		VE_RPMLIB_ERR("Failed to read file(%s): %s",
				filename, strerror(errno));
		fclose(fp);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Bogomips = %s", cpu_info->bogomips);
	fclose(fp);

	strcpy(cpu_info->mhz, cpu_info->bogomips);
	VE_RPMLIB_DEBUG("mhz = %s", cpu_info->mhz);
	strncpy(cpu_info->op_mode, "64 bit", VE_DATA_LEN);
	VE_RPMLIB_DEBUG("op_mode = %s", cpu_info->op_mode);
	retval = 0;

hndl_return2:
	free(filename);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and get the
 * memory status information of a VE process
 *
 * @param nodeid[in] VE node number
 * @param pid_t[in] PID of VE process
 * @param ve_pidstatm_req[out] Structure in which memory status information
 * gets populated
 *
 * @return 0 on success and -1 on failure
 */
int ve_pidstatm_info(int nodeid, pid_t pid, struct ve_pidstatm *ve_pidstatm_req)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct velib_pidstatm pidstatm = {0};
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!ve_pidstatm_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_pidstatm_req = %p",
						ve_pidstatm_req);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_PIDSTATM_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_ve_pid = true;
	request.ve_pid = pid;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG(" pack_msg_len = %d", pack_msg_len);
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;
	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}
	memcpy(&pidstatm, res->rpm_msg.data, res->rpm_msg.len);
	/* Populate the structure used to store process's memory information,
	 * with the values received from VEOS
	 */
	ve_pidstatm_req->size  = pidstatm.size / VKB;
	ve_pidstatm_req->resident  = pidstatm.resident / VKB;
	ve_pidstatm_req->share  = pidstatm.share / VKB;
	ve_pidstatm_req->trs  = pidstatm.trs / VKB;
	ve_pidstatm_req->drs  = pidstatm.drs / VKB;
	ve_pidstatm_req->dt  = 0;
	VE_RPMLIB_DEBUG("Received message from VEOS and values are" \
			" as follows:size = %ld\tresident = %ld\t" \
			"share = %ld\ttrs = %ld\tdrs = %ld",
			ve_pidstatm_req->size, ve_pidstatm_req->resident,
			ve_pidstatm_req->share, ve_pidstatm_req->trs,
			ve_pidstatm_req->drs);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and get the
 * resource usage of VE process
 *
 * @param nodeid[in] VE node number
 * @param pid[in] VE process ID
 * @param ve_get_rusage_req[out] Structure to populate resource usage of
 * VE process
 *
 * @return 0 on success and -1 on failure
 */
int ve_get_rusage(int nodeid, pid_t pid,
		struct ve_get_rusage_info *ve_get_rusage_req)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct velib_get_rusage_info lib_get_rusage = { {0} };
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;
	if (!ve_get_rusage_req) {
		VE_RPMLIB_ERR("Wrong argument received: ve_get_rusage_req = %p",
						ve_get_rusage_req);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_GET_RUSAGE;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_ve_pid = true;
	request.ve_pid = pid;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to receive message: %s",
				strerror(errno));
		goto hndl_return3;
	}
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;

	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return4;
	}
	memcpy(&lib_get_rusage, res->rpm_msg.data, res->rpm_msg.len);

	/* Populate the structure with VE process's resource usage information,
	 * with the values received from VEOS
	 */
	memset(ve_get_rusage_req, '\0', sizeof(struct ve_get_rusage_info));
	ve_get_rusage_req->utime = lib_get_rusage.utime;
	ve_get_rusage_req->elapsed = lib_get_rusage.elapsed;
	ve_get_rusage_req->ru_maxrss = lib_get_rusage.ru_maxrss;
	ve_get_rusage_req->ru_nvcsw = lib_get_rusage.ru_nvcsw;
	ve_get_rusage_req->ru_nivcsw = lib_get_rusage.ru_nivcsw;
	ve_get_rusage_req->page_size = lib_get_rusage.page_size;
	VE_RPMLIB_DEBUG("Received message from VEOS and values are" \
			" as follows:uptime(secs) = %ld\t" \
			"uptime(usecs) = %ld\telapsed(secs) = %ld\t" \
			"elapsed(usecs) = %ld\tru_maxrss = %ld\t" \
			"ru_nvcsw = %ld\tru_nivcsw = %ld\tpage_size = %ld\t",
			ve_get_rusage_req->utime.tv_sec,
			ve_get_rusage_req->utime.tv_usec,
			ve_get_rusage_req->elapsed.tv_sec,
			ve_get_rusage_req->elapsed.tv_usec,
			ve_get_rusage_req->ru_maxrss,
			ve_get_rusage_req->ru_nvcsw,
			ve_get_rusage_req->ru_nivcsw,
			ve_get_rusage_req->page_size);
	goto hndl_return4;
abort:
	close(sock_fd);
	abort();
hndl_return4:
	velib_connect__free_unpacked(res, NULL);
hndl_return3:
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the cache information of given VE node
 *
 * @param nodeid[in] VE node number
 * @param ve_cache_name[out] To store cache name for the given VE node
 * @param ve_cache_size[out] To store cache size for the given VE node
 *
 * @return 0 on success and -1 on error
 */
int ve_cache_info(int nodeid, char ve_cache_name[][VE_BUF_LEN], int *ve_cache_size)
{
	FILE *fp = NULL;
	int nread = 0;
	int retval = -1;
	int cache_loop = 0;
	const char ve_sysfs_path[PATH_MAX] = {0};
	char *ve_cache_path = NULL;

	VE_RPMLIB_TRACE("Entering");

	if (!ve_cache_name || !ve_cache_size) {
		VE_RPMLIB_ERR("Wrong argument received: ve_cache_name = %p," \
				" ve_cache_size = %p", ve_cache_name,
				ve_cache_size);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Get sysfs path corresponding to given VE node */
	retval = ve_sysfs_path_info(nodeid, ve_sysfs_path);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get sysfs path: %s",
				strerror(errno));
		goto hndl_return;
	}
	retval = -1;
	ve_cache_path =
		(char *)malloc(strlen(ve_sysfs_path) + VE_FILE_NAME);
	if (!ve_cache_path) {
		VE_RPMLIB_ERR("Memory allocation to cache status path failed: %s",
				strerror(errno));
		goto hndl_return;
	}

	strncpy(ve_cache_name[0], "cache_l1i", sizeof(ve_cache_name[0]));
	strncpy(ve_cache_name[1], "cache_l1d", sizeof(ve_cache_name[1]));
	strncpy(ve_cache_name[2], "cache_l2", sizeof(ve_cache_name[2]));
	strncpy(ve_cache_name[3], "cache_llc", sizeof(ve_cache_name[3]));
	for (cache_loop = 0; cache_loop < VE_MAX_CACHE;
			cache_loop++) {
		sprintf(ve_cache_path, "%s/%s",	ve_sysfs_path,
			ve_cache_name[cache_loop]);
		VE_RPMLIB_DEBUG("Get the information for cache: %d",
						cache_loop);
		/* Open the "cache_<N>" file corresponding to node,
		 * to get the cache information
		 */
		fp = fopen(ve_cache_path, "r");
		if (!fp) {
			VE_RPMLIB_ERR("Open file '%s' failed: %s",
					ve_cache_path, strerror(errno));
			goto hndl_return2;
		}
		VE_RPMLIB_DEBUG("Open cache status file %s successfully.",
				ve_cache_path);

		/* Read the cache size for given VE node
		 */
		nread = fscanf(fp, "%d", &ve_cache_size[cache_loop]);
		if (nread == EOF || nread != 1) {
				VE_RPMLIB_ERR("Failed to read file(%s): %s",
						ve_cache_path, strerror(errno));
			fclose(fp);
			goto hndl_return2;
		}
		fclose(fp);
	}

	VE_RPMLIB_DEBUG("Successfully read cache info");
	retval = 0;
hndl_return2:
	free(ve_cache_path);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the sysfs path corresponding to given VE node
 *
 * @param nodeid[in] VE node number
 * @param ve_sysfs_path[out] VE sysfs path corresponding to given node
 * @return 0 on success and -1 on failure
 */
int ve_sysfs_path_info(int nodeid, const char *ve_sysfs_path)
{
	struct udev_device *ve_udev = NULL;
	struct stat sb = {0};
	struct udev *udev = udev_new();
	int retval = -1;
	char *ve_dev_filename = NULL;
	const char *sysfs_path = NULL;

	VE_RPMLIB_TRACE("Entering");

	ve_dev_filename = (char *)malloc(sizeof(char) * VE_FILE_NAME);
	if (!ve_dev_filename) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return;
	}
	sprintf(ve_dev_filename, "%s/%s%d", DEV_PATH, VE_DEVICE_NAME, nodeid);

	retval = stat(ve_dev_filename, &sb);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get file status(%s): %s",
			ve_dev_filename, strerror(errno));
		goto hndl_return1;
	}
	retval = -1;
	/* Create new udev device, and fill the information from the sys device
	 * and the udev database entry
	 */
	ve_udev = udev_device_new_from_devnum(udev, 'c', sb.st_rdev);
	if (!ve_udev) {
		VE_RPMLIB_ERR("udev device doesn't exists: %s",
				strerror(errno));
		goto hndl_return1;
	}
	/* Retrieve the sys path of the udev device.
	 * The path is an absolute path and starts with the sys mount point
	 */
	sysfs_path = udev_device_get_syspath(ve_udev);
	if (!sysfs_path) {
		VE_RPMLIB_ERR("Failed to get sysfs path: %s",
				strerror(errno));
		goto udev_get_path_err;
	}
	memcpy((char *)ve_sysfs_path, sysfs_path, strlen(sysfs_path));
	retval = 0;
udev_get_path_err:
	udev_device_unref(ve_udev);
hndl_return1:
	free(ve_dev_filename);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	udev_unref(udev);
	return retval;
}

/**
 * @brief This function populates the total number of VE nodes and node numbers
 *
 * @param dev_num[out] To store VE device number
 * @param total_dev_count[out] To store VE node device file count
 *
 * @return 0 on success and -1 on failure
 */
int get_ve_node(int *dev_num, int *total_dev_count)
{
	DIR *dir = NULL;
	int retval = -1;
	int dev_count = 0;
	struct dirent *ent = NULL;
	char *token = NULL;

	VE_RPMLIB_TRACE("Entering");
	if (!dev_num || !total_dev_count) {
		VE_RPMLIB_ERR("Wrong argument received: dev_num = %p," \
				" total_dev_count = %p",
				dev_num, total_dev_count);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Open VE node device directory, to get the node specific
	 * information
	 */
	dir = opendir(DEV_PATH);
	if (dir != NULL) {
		VE_RPMLIB_DEBUG("Directory opened successfully = %s",
						DEV_PATH);
		/* Read VE specific directory from '/dev' device directory
		 */
		errno = 0;
		while ((ent = readdir(dir)) != NULL) {
			/* Read the VE device specific directory entries only
			 */
			if (strncmp(ent->d_name, VE_DEVICE_NAME,
						strlen(VE_DEVICE_NAME))) {
				continue;
			}
			VE_RPMLIB_DEBUG("VE device file (%s) exists",
						ent->d_name);

			/* Get the token value */
			token = strtok(ent->d_name, VE_DEVICE_NAME);
			if (token == NULL) {
				VE_RPMLIB_ERR("Invalid token: %s",
						strerror(errno));
				errno = EINVAL;
				goto hndl_return1;
			}
			dev_num[dev_count] = atoi(token);
			dev_count++;

		}
		/* If, unable to read the VE device specific directory,
		 * then return with error
		 */
		if (!ent && errno) {
			VE_RPMLIB_ERR("Failed to read directory: %s",
					strerror(errno));
			goto hndl_return1;

		} else {
			*total_dev_count = dev_count;
			VE_RPMLIB_DEBUG("total_dev_count = %d",
					*total_dev_count);
			retval = 0;
		}
	} else {
		/* If unable to open the VE device specific directory, then
		 * return with error
		 */
		VE_RPMLIB_ERR("Failed to open (%s) directory: %s",
				DEV_PATH, strerror(errno));
		goto hndl_return;
	}
hndl_return1:
	closedir(dir);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to get physical mapping of cores
 * for given VE node
 *
 * @param nodeid[in] VE node number
 * @param phy_core[out] physical cores index
 *
 * @return -1 on failure and number of cores on success
 */
int ve_phy_core_map(int nodeid, int phy_core[])
{
	FILE *fp = NULL;
	int retval = -1;
	int cntr = 0;
	int indx = 0;
	char *endptr = NULL;
	char ve_sysfs_path[PATH_MAX] = {0};
	char *tmp = NULL;
	char core_file[PATH_MAX] = {0};
	char valid_cores_from_file[LINE_MAX] = {0};
	uint64_t valid_cores = 0;

	VE_RPMLIB_TRACE("Entering");

	if (!phy_core) {
		VE_RPMLIB_ERR("Wrong argument received: phy_core = %p",
				phy_core);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Get sysfs path corresponding to given VE node
	*/
	retval = ve_sysfs_path_info(nodeid, ve_sysfs_path);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get sysfs path: %s",
				strerror(errno));
		goto hndl_return;
	}
	retval = -1;
	snprintf(core_file, sizeof(core_file), "%s/cores_enable", ve_sysfs_path);

	fp = fopen(core_file, "r");
	if (fp == NULL) {
		VE_RPMLIB_ERR("Fails to open file(%s): %s",
				core_file, strerror(errno));
		goto hndl_return;
	}

	if (fgets(valid_cores_from_file, sizeof(valid_cores_from_file), fp)
			== NULL) {
		VE_RPMLIB_ERR("Failed to gets valid cores:%s",
				strerror(errno));
		fclose(fp);
		goto hndl_return;
	}
	tmp = strchr(valid_cores_from_file, '\n');
	if (tmp != NULL)
		*tmp = '\0';
	fclose(fp);

	errno = 0;
	valid_cores = (uint64_t)strtoul(valid_cores_from_file, &endptr, 16);
	if (errno != 0) {
		VE_RPMLIB_ERR("Failed to get valid core number %s",
				strerror(errno));
		goto hndl_return;
	}
	if (*endptr != '\0') {
		VE_RPMLIB_ERR("Invalid valid_cores number[%s][%s].",
				valid_cores_from_file, endptr);
		errno = EINVAL;
		goto hndl_return;
	}
	VE_RPMLIB_DEBUG("valid_cores:%ld", valid_cores);

	for (cntr = 0, indx = 0; indx < VE_MAX_CORE_PER_NODE; indx++) {
		if ((valid_cores & (1 << indx)) == 0)
			continue;
		phy_core[cntr++] = indx;

	}
	if (!cntr) {
		VE_RPMLIB_DEBUG("No core on VE node: %d", nodeid);
		goto hndl_return;
	}
	VE_RPMLIB_DEBUG("VE core num: %d", cntr);
	retval = cntr;
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to get model name for given VE node for
 * yaml file parsing
 *
 * @param nodeid[in] VE node number
 *
 * @return modelname on success and NULL on failure
 */
char *ve_get_modelname(int nodeid)
{
	FILE *fp = NULL;
	int nread = 0;
	char *model_name = NULL;
	char ve_sysfs_path[PATH_MAX] = {0};
	char *cmn_filename = NULL;
	char product_type[VE_DATA_LEN] = {0};
	char model_num[VE_DATA_LEN] = {0};

	VE_RPMLIB_TRACE("Entering");

	model_name = NULL;

	int retval = ve_sysfs_path_info(nodeid, ve_sysfs_path);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get sysfs path: %s",
				strerror(errno));
		goto hndl_return;
	}
	retval = -1;
	cmn_filename = (char *)malloc(strlen(ve_sysfs_path) + VE_FILE_NAME);
	if (!cmn_filename) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return;
	}

	/* To get the product type for VE node */
	sprintf(cmn_filename, "%s/type", ve_sysfs_path);
	fp = fopen(cmn_filename, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Failed to open file(%s): %s",
				cmn_filename, strerror(errno));
		goto hndl_return1;
	}

	nread = fscanf(fp, "%s", product_type);
	if (nread == EOF || nread != 1) {
		VE_RPMLIB_ERR("Failed to read file (%s): %s",
				cmn_filename, strerror(errno));
		fclose(fp);
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("product type = %s", product_type);
	fclose(fp);

	memset(cmn_filename, '\0', strlen(ve_sysfs_path) + VE_FILE_NAME);

	/* To get the model number for VE node */
	sprintf(cmn_filename, "%s/model", ve_sysfs_path);
	fp = fopen(cmn_filename, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Failed to open file(%s): %s",
				cmn_filename, strerror(errno));
		goto hndl_return1;
	}

	nread = fscanf(fp, "%s", model_num);
	if (nread == EOF || nread != 1) {
		VE_RPMLIB_ERR("Failed to read file (%s): %s",
				cmn_filename, strerror(errno));
		fclose(fp);
		goto hndl_return1;
	}

	VE_RPMLIB_DEBUG("Model number = %s", model_num);
	fclose(fp);

	model_name = (char *)malloc(VE_DATA_LEN + VE_DATA_LEN + 2);
	if (!model_name) {
		VE_RPMLIB_ERR("Memory allocation failed for model_name: %s",
				strerror(errno));
		goto hndl_return1;
	}
	sprintf(model_name, "ve%s_%s",	model_num, product_type);

	VE_RPMLIB_DEBUG("Model name : %s", model_name);

hndl_return1:
	free(cmn_filename);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return model_name;
}
/**
 * @brief This function populates power management statistics of fan
 * of VE node
 *
 * @param nodeid[in] VE node number
 * @param ve_fan[out] Structure to get fan speed, minimum value and device name
 *
 * @return 0 on success and -1 on error
 */
int ve_read_fan(int nodeid, struct ve_pwr_fan *ve_fan)
{
	int retval = -1;
	int lv = -1;
	struct ve_pwr_mgmt_info pwr_info = { { {0} } };

	VE_RPMLIB_TRACE("Entering");

	if (!ve_fan) {
		VE_RPMLIB_ERR("Wrong argument received: ve_fan = %p", ve_fan);
		errno = EINVAL;
		goto hndl_return;
	}

	/* Get the hardware specific data from yaml file for given type */
	if (-1 == read_yaml_file(nodeid, "Fan", &pwr_info)) {
		VE_RPMLIB_ERR("Failed to get yaml data: %s",
				strerror(errno));
		goto hndl_return;
	}
	memcpy(ve_fan, &pwr_info, sizeof(struct ve_pwr_mgmt_info));
	/* Populate the structure to get fan statistics */
	for (lv = 0; lv < ve_fan->count; lv++) {
		VE_RPMLIB_DEBUG("Successfully read fan information:" \
				" device name = %s:: fan_min = %lf :: " \
				"fan_speed = %lf", ve_fan->device_name[lv],
				ve_fan->fan_min[lv], ve_fan->fan_speed[lv]);
	}
	retval = 0;
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the power management statistics of
 * temperature for VE node
 *
 * @param nodeid[in] VE node number
 * @param temp[out] Structure to get temperatures, minimum/maximum value
 * and device name
 *
 * @return 0 on success and -1 on error
 */
int ve_read_temp(int nodeid, struct ve_pwr_temp *temp)
{
	int retval = -1;
	int lv = -1;
	int hb_dev_lv = -1;
	int match_hbm_dev = 0;
	char temp_hbm_dev[MAX_DEVICE_LEN] = {0};
	struct ve_pwr_mgmt_info pwr_info = { { {0} } };

	VE_RPMLIB_TRACE("Entering");

	if (!temp) {
		VE_RPMLIB_ERR("Wrong argument received: temp = %p", temp);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Get the hardware specific data from yaml file */
	if (-1 == read_yaml_file(nodeid, "Thermal", &pwr_info)) {
		VE_RPMLIB_ERR("Failed to get yaml data: %s",
				strerror(errno));
		goto hndl_return;
	}
	memcpy(temp, &pwr_info, sizeof(struct ve_pwr_mgmt_info));
	/* Populate the structure to get temperature */
	for (lv = 0; lv < temp->count; lv++) {
		/* Thermal Device ve_hbmN_temp (N=0, 1, ..., 5) should be
		 * handled as the value is in degree Celsius, not micro degree
		 * Celsius like the other thermal sensors
		*/
		for (hb_dev_lv = 0; hb_dev_lv <= HBM_DEV_COUNT; hb_dev_lv++) {
			match_hbm_dev = 0;
			memset(temp_hbm_dev, '\0', sizeof(temp_hbm_dev));
			sprintf(temp_hbm_dev, "ve_hbm%d_temp", hb_dev_lv);
			if (!strncmp(temp->device_name[lv], temp_hbm_dev, sizeof(temp_hbm_dev))) {
				match_hbm_dev = 1;
				break;
			}
		}
		if (!match_hbm_dev)
				temp->ve_temp[lv] = temp->ve_temp[lv] / YAML_DATA_DEM;
		VE_RPMLIB_DEBUG("Successfully read temperature information:" \
				" device name = %s :: temp_min = %lf :: " \
				"temp_max = %lf:: temp_val = %lf",
				temp->device_name[lv], temp->temp_min[lv],
				temp->temp_max[lv], temp->ve_temp[lv]);
	}
	retval = 0;
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates power management statistics for voltage
 * of VE node
 *
 * @param nodeid[in] VE node number
 * @param ve_volt[out] Structure to get voltage, minimum/maximum value and
 * device name
 *
 * @return 0 on success and -1 on failure
 */
int ve_read_voltage(int nodeid, struct ve_pwr_voltage *ve_volt)
{
	int retval = -1;
	int lv = -1;
	struct ve_pwr_mgmt_info pwr_info = { { {0} } };

	VE_RPMLIB_TRACE("Entering");
	if (!ve_volt) {
		VE_RPMLIB_ERR("Wrong argument received: ve_volt = %p",
				ve_volt);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Get the hardware specific data from yaml file */
	if (-1 == read_yaml_file(nodeid, "Voltage", &pwr_info)) {
		VE_RPMLIB_ERR("Failed to get yaml data: %s",
				strerror(errno));
		goto hndl_return;
	}
	memcpy(ve_volt, &pwr_info, sizeof(struct ve_pwr_mgmt_info));
	/* Populate the structure to get voltage statistics */
	for (lv = 0; lv < ve_volt->count; lv++) {
		ve_volt->cpu_volt[lv] = ve_volt->cpu_volt[lv] / YAML_DATA_DEM;
		VE_RPMLIB_DEBUG("Successfully read voltage information:" \
				" device name = %s:: volt_min = %lf::" \
				" volt_max = %lf:: volt_val = %lf",
				ve_volt->device_name[lv],
				ve_volt->volt_min[lv], ve_volt->volt_max[lv],
				ve_volt->cpu_volt[lv]);
	}
	retval = 0;
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the CPU frequency of given VE node from
 * sysfs
 *
 * @param nodeid[in] VE node number
 * @param cpufreq[out] CPU frequency for given VE Node
 *
 * @return 0 on success and -1 on failure
 */
int ve_cpufreq_info(int nodeid, unsigned long *cpufreq)
{
	FILE *fp = NULL;
	int nread = 0;
	int retval = -1;
	char *ve_cpufreq_path = NULL;
	char ve_sysfs_path[PATH_MAX] = {0};

	VE_RPMLIB_TRACE("Entering");

	if (!cpufreq) {
		VE_RPMLIB_ERR("Wrong argument received: cpufreq = %p",
				cpufreq);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Get sysfs path corresponding to given VE node */
	retval = ve_sysfs_path_info(nodeid, ve_sysfs_path);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get sysfs path: %s",
				strerror(errno));
		goto hndl_return;
	}
	retval = -1;
	ve_cpufreq_path =
		(char *)malloc(strlen(ve_sysfs_path) + VE_FILE_NAME);
	if (!ve_cpufreq_path) {
		VE_RPMLIB_ERR("Memory allocation to cpufreq path failed: %s",
				strerror(errno));
		goto hndl_return;
	}
	sprintf(ve_cpufreq_path, "%s/clock_chip", ve_sysfs_path);

	/* Open the "clock_chip" file corresponding to node,
	 * to get the CPU frequency information
	 */
	fp = fopen(ve_cpufreq_path, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Open file '%s' failed: %s",
				ve_cpufreq_path, strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("Open cpu frequency status file %s successfully.",
			ve_cpufreq_path);

	/* Read the VE cpu frequency information
	*/
	nread = fscanf(fp, "%lu", cpufreq);
	if (nread == EOF || nread != 1) {
		VE_RPMLIB_ERR("Failed to read file(%s): %s",
				ve_cpufreq_path, strerror(errno));
		fclose(fp);
		goto hndl_return1;
	}
	fclose(fp);

	VE_RPMLIB_DEBUG("Successful to get cpu frequency info: %lu",
			*cpufreq);
	retval = 0;
hndl_return1:
	free(ve_cpufreq_path);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to update the device name for
 * temperature device to replace physical core with logical core in
 * device name.
 *
 * @param nodeid[in] VE node number
 * @param core_id[in] core_id received from yaml file
 * @param dev_name[in] device name received from yaml file
 * @param ret_flag[out] flag to show error condition that
 * no physical core matches with logical core.
 *
 * @return device name for success and NULL on failure
 */
char *ve_get_sensor_device_name(int nodeid, int core_id, char *dev_name,
		int *ret_flag)
{
	char *device_name = NULL;
	char *new_dev_name = NULL;
	char *dup_devname_addr = NULL;
	int index = 0, lv = 0;
	int log_core_val = 0;
	int ve_cores = -1;
	int phy_core_map[VE_MAX_CORE_PER_NODE] = {-1};

	VE_RPMLIB_TRACE("Entering");

	new_dev_name = NULL;
	*ret_flag = 0;

	if (!dev_name) {
		VE_RPMLIB_ERR("Wrong argument received: dev_name = %p",
				dev_name);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Get total cores and physical to logical mapping for given
	 * VE node
	 */
	ve_cores = ve_phy_core_map(nodeid, phy_core_map);
	if (-1 == ve_cores) {
		VE_RPMLIB_ERR("Failed to get core mapping for VE node: %d",
				nodeid);
		goto hndl_return;
	}
	device_name = dev_name;
	dup_devname_addr = strdup(device_name);
	while (*device_name) {
		VE_RPMLIB_DEBUG("Checking each character from device name : %c",
				*device_name);
		if (isdigit(*device_name)) {
			VE_RPMLIB_DEBUG("Got the number in given device name:" \
					" core_id = %d",
					core_id);
			if (core_id == 1) {
				VE_RPMLIB_DEBUG("Get the physical core:" \
						" core_id = %d", core_id);
				long phy_core_id = strtol(device_name,
						&device_name, 10);
				for (lv = 0; lv < ve_cores; lv++) {
					VE_RPMLIB_DEBUG("physical_core_id = %d," \
							" logical_core_id = %d",
							phy_core_map[lv], lv);
					if (phy_core_map[lv] == phy_core_id) {
						VE_RPMLIB_DEBUG("%d physical core" \
								" matched with" \
								" coreid: %ld",
								phy_core_map[lv],
								phy_core_id);
						VE_RPMLIB_DEBUG("Physical core" \
								" matched with" \
								" coreid");
						log_core_val = lv;
						break;
					}
				}
				if (lv == ve_cores) {
					*ret_flag = -1;
					free(dup_devname_addr);
					goto hndl_return;

				}
				break;
			} else {
				core_id--;
				device_name++;
				index++;
				continue;
			}
		} else {
			device_name++;
			index++;
		}
	}
	sprintf((dup_devname_addr + index), "%d", log_core_val);
	new_dev_name = malloc(VE_BUF_LEN);
	if (!new_dev_name) {
		VE_RPMLIB_ERR("Memory allocation failed: %s", strerror(errno));
		goto hndl_return;
	}
	sprintf(new_dev_name, "%s%s", dup_devname_addr, device_name);
	free(dup_devname_addr);
	VE_RPMLIB_DEBUG("New sensor device name: %s", new_dev_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return new_dev_name;
}

/**
 * @brief This function will be used to read data from given file
 *
 * @param nodeid[in] VE node number
 * @param file_name[in] File name used to fetch the data.
 *
 * @return positive value on success and -1 on failure
 */
int read_file_value(int nodeid, char *file_name)
{
	FILE *fp = NULL;
	char ve_sysfs_path[PATH_MAX] = {0};
	char *ve_file_path = NULL;
	int nread = -1;
	double ve_pwr_val = -1;

	VE_RPMLIB_TRACE("Entering");

	/* Get sysfs path corresponding to given VE node */
	int retval = ve_sysfs_path_info(nodeid, ve_sysfs_path);
	if (-1 == retval) {
		VE_RPMLIB_ERR("Failed to get sysfs path: %s",
				strerror(errno));
		goto hndl_return;
	}
	retval = -1;
	/* Get the path to read desired value for given node */
	ve_file_path =
		(char *)malloc(strlen(ve_sysfs_path) + VE_FILE_NAME);
	if (!ve_file_path) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return;
	}
	sprintf(ve_file_path, "%s/%s", ve_sysfs_path, file_name);

	fp = fopen(ve_file_path, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Open file '%s' failed: %s",
				ve_file_path, strerror(errno));
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("Open file %s successfully.", ve_file_path);

	nread = fscanf(fp, "%lf", &ve_pwr_val);
	if (nread == EOF || nread != 1) {
		VE_RPMLIB_ERR("Failed to read file (%s): %s",
				ve_file_path, strerror(errno));
		fclose(fp);
		goto hndl_return1;
	}
	VE_RPMLIB_DEBUG("Value received from file %s : %lf",
				ve_file_path, ve_pwr_val);
	fclose(fp);

hndl_return1:
	free(ve_file_path);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return ve_pwr_val;
}

/**
 * @brief This function will be used to populate the values in
 * common power management related structures
 *
 * @param parsed_value[in] values retrieved from yaml file
 * @param pwr_info[in] Structure to populate values
 * @param index[in] Total values in yaml file in sigle occurrence of given type
 * @param type[in] Type of power management statistics
 * @param count[in] Total count of values for specified type
 * @param nodeid[in] VE node number
 *
 * @return positive value on success and -1 on failure
 */
int get_yaml_data(char parsed_value[][MAX_DEVICE_LEN],
				struct ve_pwr_mgmt_info *pwr_info,
				int index, char *type, int count, int nodeid)
{
	int entry_cnt = 0;
	char *sensor_device_name = NULL;
	int value = 0;
	int tot_cnt = -1;
	int core_id = -1;
	int retval = -1;

	VE_RPMLIB_TRACE("Entering");

	tot_cnt = count;
	for (entry_cnt = 0; entry_cnt < index; entry_cnt++) {
		VE_RPMLIB_DEBUG("%d Value from yaml file: %s",
				index, parsed_value[entry_cnt]);
		if (SENSOR_DEV_NAME_INDEX == entry_cnt) {
			strncpy(pwr_info->device_name[tot_cnt],
					parsed_value[entry_cnt],
					MAX_DEVICE_LEN);
			VE_RPMLIB_DEBUG("Device name = %s",
					pwr_info->device_name[tot_cnt]);
		} else if (!strcmp("core_id", parsed_value[entry_cnt])) {
			VE_RPMLIB_DEBUG("Thermal device type received");
			if (!strcmp(type, "Thermal")) {
				core_id = atoi(parsed_value[entry_cnt + 1]);
				if (core_id) {
					VE_RPMLIB_DEBUG("Received core_id = %d",
							core_id);
					sensor_device_name =
						ve_get_sensor_device_name(nodeid,
							core_id,
							parsed_value[SENSOR_DEV_NAME_INDEX],
							&retval);
					if (!sensor_device_name) {
						if (-1 == retval) {
							pwr_info->count = 0;
							retval = 0;
							goto hndl_return;
						} else {
							VE_RPMLIB_ERR("Failed to get" \
								" device name: %s",
								sensor_device_name);
							goto hndl_return;
						}
					}
					memset(pwr_info->device_name[tot_cnt], '\0',
						sizeof(pwr_info->device_name[tot_cnt]));
					strcpy(pwr_info->device_name[tot_cnt],
							sensor_device_name);
					free(sensor_device_name);
				}
				VE_RPMLIB_DEBUG("New Sensor device name : %s",
						pwr_info->device_name[tot_cnt]);
			}
			/* Get the sysfs file name */
		} else if (!strcmp("sysfs_file",
					parsed_value[entry_cnt])) {
			VE_RPMLIB_DEBUG("Now read device value from sysfs file");
			/* Read value from sysfs file */
			value  = read_file_value(nodeid,
					parsed_value[entry_cnt + 1]);
			if (-1 == value) {
				VE_RPMLIB_ERR("Failed to read from file: %s",
						parsed_value[entry_cnt + 1]);
				goto hndl_return;
			}
			pwr_info->actual_val[tot_cnt] = value;
			VE_RPMLIB_DEBUG("Received value from sysfs = %lf",
					pwr_info->actual_val[tot_cnt]);
		} else if (!strcmp("min_value", parsed_value[entry_cnt])) {
			pwr_info->min_val[tot_cnt] =
				atoi(parsed_value[entry_cnt + 1]);
			VE_RPMLIB_DEBUG("Received minimum value = %lf",
					pwr_info->min_val[tot_cnt]);
		} else if (!strcmp("max_value", parsed_value[entry_cnt])) {
			pwr_info->max_val[tot_cnt] =
				atoi(parsed_value[entry_cnt + 1]);
			VE_RPMLIB_DEBUG("Received maximum value = %lf",
					pwr_info->max_val[tot_cnt]);
		}
	}
	pwr_info->count = tot_cnt + 1;
	retval = 0;
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function populates the yaml file data for VE Node
 *
 * @param nodeid[in] Model name for VE node
 * @param type[in] Type corresponding to power management information
 * required. It can be FAN, TEMP and VOLTAGE
 * @param pwr_info[out] Values corresponding to given type
 *
 * @return 0 on success and -1 on failure
 */

int read_yaml_file(int nodeid, char *type, struct ve_pwr_mgmt_info *pwr_info)
{
	FILE *fp = NULL;
	char *yamlfile = NULL;
	char parsed_value[YAML_FILE_INDEX][MAX_DEVICE_LEN] = { {0} };
	char *model_name = NULL;
	int index = 0;
	int continue_parse = 0;
	int count = 0;
	int retval = -1;
	int save_values = 0;
	int yaml_data_store = 0;
	int device_count = 0;
	yaml_parser_t parser;
	yaml_event_t  event;
	int parsing_flag = 0;

	VE_RPMLIB_TRACE("Entering");

	if (!type || !pwr_info) {
		VE_RPMLIB_ERR("Wrong argument received: type = %p: pwr_info= %p",
						type, pwr_info);
		errno = EINVAL;
		goto hndl_return;
	}

	if (!yaml_parser_initialize(&parser)) {
		VE_RPMLIB_ERR("Failed to initialize parser: %s",
				strerror(errno));
		goto hndl_return;
	}

	memset(pwr_info, '\0', sizeof(struct ve_pwr_mgmt_info));
	/* Get the VE node specific model name */
	model_name = ve_get_modelname(nodeid);
	if (!model_name) {
		VE_RPMLIB_ERR("Failed to get VE model name: %s",
				strerror(errno));
		goto parser_delt;
	}
	VE_RPMLIB_DEBUG("VE model name: %s", model_name);

	yamlfile = (char *)malloc(sizeof(char) * VE_PATH_MAX);
	if (!yamlfile) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	sprintf(yamlfile, "%s/%s", YAML_FILE_PATH, "ve_hw_spec.yaml");

	fp = fopen(yamlfile, "r");
	if (!fp) {
		VE_RPMLIB_ERR("Failed to open file (%s): %s",
				yamlfile, strerror(errno));
		goto hndl_return2;
	}
	/* Function to set the input file */
	yaml_parser_set_input_file(&parser, fp);

	do {
		VE_RPMLIB_DEBUG("Start parsing file till YAML_STREAM_END_EVENT");
		/* Function to parse the input stream and produces
		 * the parsing event
		 */
		if (yaml_parser_parse(&parser, &event) == 0) {
			VE_RPMLIB_ERR("Failed to parse file (%s): %s",
					yamlfile, strerror(errno));
			goto hndl_return2;
		}

		/* Start parsing if, VE model matched */
		if (event.type == YAML_SCALAR_EVENT &&
				(!strcmp(model_name,
					 (const char *)event.data.scalar.value))) {
			VE_RPMLIB_DEBUG("Matched with given model(%s)",
					event.data.scalar.value);
			yaml_event_delete(&event);
			do {
				VE_RPMLIB_DEBUG("Parse file till" \
						" YAML_MAPPING_END_EVENT");
				if (!parsing_flag) {
					if (yaml_parser_parse(&parser, &event) == 0) {
						VE_RPMLIB_ERR("Failed to parse" \
								" file (%s): %s",
								yamlfile,
								strerror(errno));
						goto event_delete;
					}
				}
				do {
					VE_RPMLIB_DEBUG("Parse file till" \
							" YAML_MAPPING_END_EVENT");
					if (!parsing_flag) {
						if (yaml_parser_parse(&parser, &event) == 0) {
							VE_RPMLIB_ERR("Failed to" \
								" parse file" \
								" (%s): %s",
								yamlfile,
								strerror(errno));
							goto event_delete;
						}
					}
					parsing_flag = 0;
					if (event.type ==
						YAML_MAPPING_START_EVENT) {
						VE_RPMLIB_DEBUG("Parsing for event type:" \
							" YAML_MAPPING_START_EVENT");
						if (yaml_parser_parse(&parser, &event) == 0) {
							VE_RPMLIB_ERR("Failed to" \
								" parse file" \
								" (%s): %s",
								yamlfile,
								strerror(errno));
							goto event_delete;
						}
					}
					VE_RPMLIB_DEBUG("Parse the file: %s",
							event.data.scalar.value);

					if (event.data.scalar.value) {
						/* To handle core_id field (optional) */
						if ((!strcmp("core_id",
								(const char *)event.data.scalar.value))) {
							if (continue_parse) {
								continue_parse = continue_parse + 1;
								yaml_event_delete(&event);
								continue;
							} else if (save_values)
								save_values = save_values + 2;
						} else if (continue_parse) {
								continue_parse--;
								yaml_event_delete(&event);
								continue;
						}
						strncpy(parsed_value[index],
								(const char *)event.data.scalar.value,
								VE_DATA_LEN);
						VE_RPMLIB_DEBUG("Received value from " \
								"yaml file = %s",
								parsed_value[index]);
						index++;
					} else {
						strncpy(parsed_value[index],
								"0", VE_DATA_LEN);
						VE_RPMLIB_DEBUG("Does not received " \
								"value from yaml " \
								"file = %s",
								parsed_value[index]);
						if (!yaml_data_store)
							continue;
					yaml_event_delete(&event);
					}
					count++;
					if (save_values) {
						save_values--;
						VE_RPMLIB_DEBUG("Save parsed " \
								"value and " \
								"continue: %d",
								save_values);
						yaml_event_delete(&event);
						continue;
					}
					if (yaml_data_store) {
						VE_RPMLIB_DEBUG("Store yaml entry: %d",
								yaml_data_store);
						if (get_yaml_data(parsed_value,
									pwr_info,
									index,
									type,
									device_count,
									nodeid)
								== -1) {
							VE_RPMLIB_ERR("Failed to " \
									"get yaml data " \
									": %s",
									strerror(errno));
							goto event_delete;
						}

						if (pwr_info->count) {
							VE_RPMLIB_DEBUG("Successfully get yaml data: " \
									"device name : %s\t" \
									"minimum value : %lf\t" \
									"maximum value : %lf\t" \
									"actual value : %lf\tcount: %d",
									pwr_info->device_name[device_count],
									pwr_info->min_val[device_count],
									pwr_info->max_val[device_count],
									pwr_info->actual_val[device_count],
									pwr_info->count);

							device_count++;
						}
						/* Reset all the values to get
						 * the another yaml entry
						 */
						yaml_data_store = 0;
						memset(parsed_value, '\0', sizeof(parsed_value));
						index = 0;
						count = 0;
						continue_parse = 0;
						save_values = 0;
						yaml_event_delete(&event);
						continue;
					}
					/* Save the values if type is matched */
					if (!strcmp(type,
							(char *)event.data.scalar.value)) {
						VE_RPMLIB_DEBUG("Type to get " \
								"value = %s",
								type);
						save_values = (YAML_FILE_INDEX - count - 1 - NOCORE_ID);
						count = 0;
						continue_parse = 0;
						yaml_data_store = 1;
						yaml_event_delete(&event);
					} else {
						if (count ==  DEV_MATCH_NUMBER) {
							VE_RPMLIB_DEBUG("device type" \
								"not matched");
							index = 0;
							continue_parse = (YAML_FILE_INDEX - count - NOCORE_ID);
							count = 0;
							save_values = 0;
							yaml_data_store = 0;
						}
						yaml_event_delete(&event);
					}
				} while (event.type != YAML_MAPPING_END_EVENT);
				yaml_event_delete(&event);
				if (yaml_parser_parse(&parser, &event) == 0) {
					VE_RPMLIB_ERR("Failed to parse file" \
							" (%s): %s", yamlfile,
							strerror(errno));
					goto event_delete;
				}
				parsing_flag = 1;
			} while (event.type != YAML_MAPPING_END_EVENT);
			yaml_event_delete(&event);
			break;
		} else
			VE_RPMLIB_DEBUG("Model: %s not matched", model_name);
		if (event.type == YAML_SCALAR_EVENT) {
			yaml_event_delete(&event);
		}
	} while (event.type != YAML_STREAM_END_EVENT);

	index = 0;
	retval = 0;
	fclose(fp);

event_delete:
	yaml_event_delete(&event);
hndl_return2:
	free(yamlfile);
hndl_return1:
	free(model_name);
parser_delt:
	yaml_parser_delete(&parser);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and get/remove
 * the specifid shmid's informationa and summary.
 *
 * @param nodeid[in] VE node number
 * @param mode[in] Get information from VEOS for specified mode
 * @param key_id[in/out] Shared memory key/id
 * @param result[out] To identify whether given KEY/ID exist on VE or not
 * @param shm_data[out] Populate structure to get specified shmid's information
 * @param ve_shm_smry[out] Populate structure to get VE shared memory
 * summary of given node.
 *
 * @return 0 on success and negative value on failure
 */
int ve_shm_info(int nodeid, int mode, int *key_id, bool *result,
		struct ve_shm_data *shm_data, struct shm_info *ve_shm_smry)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct ve_shm_info shm_info = {0};
	struct velib_shm_summary velib_shm_smry = {0};
	ProtobufCBinaryData subreq = {0};
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;

	/* Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	shm_info.mode = mode;
	VE_RPMLIB_DEBUG("Shared memory mode = %d", shm_info.mode);

	if (mode == SHMID_RM || mode == SHMKEY_RM ||
	mode == SHMID_INFO || mode == SHMID_QUERY ||
	mode == SHMKEY_QUERY) {
		shm_info.key_id = *key_id;
		VE_RPMLIB_DEBUG("SHM key/id to send to VEOS: %d", shm_info.key_id);
	}
	subreq.data = (uint8_t *)&shm_info;
        subreq.len = sizeof(struct ve_shm_info);
	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_SHM_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
					" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;

	/* Check if the desired return value is received
	 */
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return3;
	}
	/* Populate the structure with information received from VEOS
	 */
	if (mode == SHM_SUMMARY) {
		/* Populate structure with VE shared memory summary for given node
		 */
		memcpy(&velib_shm_smry, res->rpm_msg.data, sizeof(struct velib_shm_summary));
		ve_shm_smry->used_ids = velib_shm_smry.used_ids;
		ve_shm_smry->shm_tot= velib_shm_smry.shm_tot;
		ve_shm_smry->shm_rss = velib_shm_smry.shm_rss;
		ve_shm_smry->shm_swp = 0;
		ve_shm_smry->swap_attempts = 0;
		ve_shm_smry->swap_successes = 0;
		VE_RPMLIB_DEBUG("Values received from VEOS: "	\
				"used_ids = %d, shm_tot %lu, shm_rss %lu",
				ve_shm_smry->used_ids,
				ve_shm_smry->shm_tot,
				ve_shm_smry->shm_rss);
	} else if (mode == SHMKEY_RM) {
		/* Remove the shared memory of given 'key' and return its 'ID'
		 */
		memcpy(&shm_info, res->rpm_msg.data, res->rpm_msg.len);
		*key_id = shm_info.key_id;
		VE_RPMLIB_DEBUG("Key value received from VEOS: %d", *key_id);
	} else if (mode == SHMID_INFO) {
		/* Received information corresponding to given SHMID
		*/
		memcpy(shm_data, res->rpm_msg.data, sizeof(struct ve_shm_data));
		VE_RPMLIB_DEBUG("Data received successfully of length: %d",
				sizeof(struct ve_shm_data));
	} else if (mode == SHMID_QUERY || mode == SHMKEY_QUERY) {
		/* Result whether given shared memory exists on VE or not
		 */
		memcpy(result, res->rpm_msg.data, res->rpm_msg.len);
		VE_RPMLIB_DEBUG("Resulted value received from VEOS: %d", *result);

	}
	goto hndl_return3;
abort:
	close(sock_fd);
	abort();
hndl_return3:
	velib_connect__free_unpacked(res, NULL);
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}


/**
 * @brief This function will be used to communicate with VEOS and get the
 * NUMA statistics for given VE node.
 *
 * @param nodeid[in] VE node number
 * @param ve_numa_stat[out] Populate structure to get NUMA statistics for
 * given node.
 *
 * @return 0 on success and negative value on failure
 */
int ve_numa_info(int nodeid, struct ve_numa_stat *ve_numa)
{
	int retval = -1;
	int sock_fd = -1;
	unsigned int lv = 0;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	ProtobufCBinaryData subreq = {0};
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;

	/* Create the socket path corresponding to received VE node
	*/
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	*/
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_NUMA_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		retval = -1;
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and"
			" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	*/
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	VE_RPMLIB_DEBUG("Data received from VEOS %d bytes", retval);

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;

	/* Check if the desired return value is received
	*/
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		retval = -1;
		goto hndl_return3;
	}
	/* Populate the structure with information received from VEOS
	*/
	memcpy(ve_numa, res->rpm_msg.data, res->rpm_msg.len);
	VE_RPMLIB_DEBUG("Received total NUMA nodes in given VE node = %d",
			ve_numa->tot_numa_nodes);
	for (lv = 0; lv < ve_numa->tot_numa_nodes; lv++) {
		VE_RPMLIB_DEBUG("NUMA: %d core: %s mem size: %llu mem free: %llu",
				lv, ve_numa->ve_core[lv],
				ve_numa->mem_size[lv],
				ve_numa->mem_free[lv]);
	}
	goto hndl_return3;
abort:
	close(sock_fd);
	abort();
hndl_return3:
	velib_connect__free_unpacked(res, NULL);
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS and delete the
 * dummy task of given PID exists on given VE node.
 *
 * @param nodeid[in] VE node number
 * @param pid[in] PID of process whose task struct needs to be deleted.
 *
 * @return 0 on success and negative value on failure
 */
int ve_delete_dummy_task(int nodeid, pid_t pid)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;

	/* Create the socket path corresponding to received VE node
	*/
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	*/
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_DEL_DUMMY_TASK;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_ve_pid = true;
	request.ve_pid = pid;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		retval = -1;
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and "
			"waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	*/
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	VE_RPMLIB_DEBUG("Data received %d bytes from VEOS", retval);

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;

	/* Check if the desired return value is received
	*/
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		retval = -1;
		goto hndl_return3;
	}
	/* Function will return success, if expected return value is
	 * received from VEOS
	 */
	VE_RPMLIB_DEBUG("Received message from VEOS and retval = %d", retval);
	goto hndl_return3;
abort:
	close(sock_fd);
	abort();
hndl_return3:
	velib_connect__free_unpacked(res, NULL);
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to communicate with VEOS to get/remove
 * shared memory exists on VE.
 *
 * @param nodeid[in] VE node number
 * @param mode[in] Get information from VEOS for specified mode
 * @param length[out] Data length to be read from file.
 * @param filename[out] File which consists of shared memory information
 *
 * @return 0 on success and negative value on failure
 */
int ve_shm_list_or_remove(int nodeid, int mode,
			unsigned int *length, char *filename)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	void *pack_buf_send = NULL;
	uint8_t *unpack_buf_recv = NULL;
	char *ve_sock_name = NULL;
	struct file_info fileinfo = {0};
	struct ve_shm_info shm_info = {0};
	ProtobufCBinaryData subreq = {0};
	VelibConnect *res = NULL;
	VelibConnect request = VELIB_CONNECT__INIT;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;

	if (!length || !filename) {
		VE_RPMLIB_ERR("Wrong argument received:length = %p, filename = %p",
				length, filename);
		errno = EINVAL;
		goto hndl_return;
	}


	/* Create the socket path corresponding to received VE node
	*/
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	*/
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				strerror(errno));
		goto hndl_return;
	}
	/* Create the socket connection corresponding to socket path
	*/
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_return_sock;
	} else if (-2 == sock_fd)
		goto abort;

	shm_info.mode = mode;
	VE_RPMLIB_DEBUG("Shared memory mode = %d", shm_info.mode);

	shm_info.nodeid = nodeid;
	subreq.data = (uint8_t *)&shm_info;
	subreq.len = sizeof(struct ve_shm_info);
	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = VE_SHM_INFO;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	request.has_rpm_msg = true;
	request.rpm_msg = subreq;

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (0 >= pack_msg_len) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}
	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return1;
	}
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/* Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
				retval);
		goto hndl_return2;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and" \
			" waiting to receive....");

	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_return2;
	}
	memset(unpack_buf_recv, '\0', MAX_PROTO_MSG_SIZE);

	/* Receive the IPC message from VEOS
	*/
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	VE_RPMLIB_DEBUG("Data received successfully from VEOS, now verify it.");

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
			(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}
	retval = res->rpm_retval;

	/* Check if the desired return value is received
	*/
	if (0 != retval) {
		VE_RPMLIB_ERR("Received message verification failed.");
		errno = -(retval);
		goto hndl_return3;
	}
	/* Required information will be written on a file on VH,
	 * So populate structure with file name and data length
	 */
	memcpy(&fileinfo, res->rpm_msg.data, res->rpm_msg.len);
	VE_RPMLIB_DEBUG("Values received from VEOS: length %u, file: %s",
			fileinfo.length,
			fileinfo.file);
	*length = fileinfo.length;

	/* Create file path from received file name */
	if (*length) {
		memset(filename, '\0', VE_PATH_MAX);
		sprintf(filename, "%s/veos%d-tmp/%s",
				VE_SOC_PATH, nodeid, fileinfo.file);
		VE_RPMLIB_DEBUG("Read information from '%s' file", filename);
	}

	goto hndl_return3;
abort:
	close(sock_fd);
	abort();
hndl_return3:
	velib_connect__free_unpacked(res, NULL);
	free(unpack_buf_recv);
hndl_return2:
	free(pack_buf_send);
hndl_return1:
	close(sock_fd);
hndl_return_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function will be used to check binary format
 * (VE or any other)
 *
 * @param ext_file[in] Check ELF format of this file
 *
 * @return 0 on success and negative value on failure
 */
int ve_chk_exec_format(char *ext_file)
{
	int fd = -1;
	Elf64_Ehdr ehdr = { {0} };
	int retval = 0;

	VE_RPMLIB_TRACE("Entering");
	errno = 0;

	if (!ext_file) {
		VE_RPMLIB_ERR("Wrong argument received: ext_file = %p",
				ext_file);
		errno = EINVAL;
		retval = -errno;
		goto end;
	}

	ext_file = realpath(ext_file, NULL);
	if (NULL == ext_file) {
		retval = -errno;
		VE_RPMLIB_ERR("Fail(%s) to get realpath of:%s",
				strerror(-retval),
				ext_file);
		goto end;
	}
	/* Check if realpath has search permissions
	 * If not, then EACCES should be returned */
	retval = access(ext_file, F_OK);
	if (-1 == retval) {
		retval = -errno;
		VE_RPMLIB_ERR("Accessing real_path to binary failed, "
				"errno %s", strerror(-retval));
		goto end;
	}
	fd = open(ext_file, O_RDONLY | O_CLOEXEC);
	if (0 > fd) {
		retval = -errno;
		VE_RPMLIB_ERR("Failed(%s) to open ELF file name",
				strerror(-retval));
		goto end;
	}
	if (0 > (read(fd, &ehdr, sizeof(Elf64_Ehdr)))) {
		retval = -errno;
		close(fd);
		VE_RPMLIB_ERR("Failed(%s) to read ELF file", strerror(-retval));
		goto end;
	}
	/* Check the format of given executable */
	if (ELF_VE != ehdr.e_machine) {
		retval = -ENOEXEC;
		VE_RPMLIB_ERR("This is not VE ELF file: %s", strerror(-retval));
		close(fd);
		goto end;
	}
	VE_RPMLIB_DEBUG("This is a VE ELF file (%s)", ext_file);

end:
	VE_RPMLIB_TRACE("Exiting");
	errno = -retval;
	return retval;
}

/**
 * @brief This function is request send to veos,
 *	  and recive from veos
 *
 * @param nodeid[in] VE node ID
 * @param subcmd sub command to send
 * @param sendmsg[in] message to send
 * @param sendmsg_len[in] the length of the message to send
 * @param recv_buf[out] buffer to store the received message
 * @param recv_bufsize[in] the size of the buffer to receive a message
 * @return 0 on success and negative value on failure
 */
static int ve_message_send_receive(int nodeid, int subcmd, void *sendmsg,
	size_t sendmsg_len, void *recv_buf, size_t recv_bufsize)
{
	int retval = -1;
	int sock_fd = -1;
	int pack_msg_len = -1;
	char *ve_sock_name = NULL;
	uint8_t *unpack_buf_recv = NULL;
	void *pack_buf_send = NULL;

	VelibConnect request = VELIB_CONNECT__INIT;
	VelibConnect *res;

	VE_RPMLIB_TRACE("Entering");

	request.cmd_str = RPM_QUERY;
	request.has_subcmd_str = true;
	request.subcmd_str = subcmd;
	request.has_rpm_pid = true;
	request.rpm_pid = getpid();
	if (sendmsg) {
		request.has_rpm_msg = true;
		request.rpm_msg.data = sendmsg;
		request.rpm_msg.len = sendmsg_len;
	} else {
		request.has_rpm_msg = false;
	}
	errno = 0;

	/*
	 * Create the socket path corresponding to received VE node
	 */
	ve_sock_name = ve_create_sockpath(nodeid);
	if (!ve_sock_name) {
		VE_RPMLIB_ERR("Failed to create socket path for VE: %s",
				strerror(errno));
		goto hndl_return;
	}

	/*
	 * Create the socket connection corresponding to socket path
	 */
	sock_fd = velib_sock(ve_sock_name);
	if (-1 == sock_fd) {
		VE_RPMLIB_ERR("Failed to create socket:%s, error: %s",
				ve_sock_name, strerror(errno));
		goto hndl_free_sock;
	} else if (-2 == sock_fd) {
		goto abort;
	}

	/* Get the request message size to send to VEOS */
	pack_msg_len = velib_connect__get_packed_size(&request);
	if (pack_msg_len <= 0) {
		VE_RPMLIB_ERR("Failed to get size to pack message");
		fprintf(stderr, "Failed to get size to pack message\n");
		goto abort;
	}

	pack_buf_send = malloc(pack_msg_len);
	if (!pack_buf_send) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		goto hndl_close_sock;
	}
	memset(pack_buf_send, '\0', pack_msg_len);

	/* Pack the message to send to VEOS */
	retval = velib_connect__pack(&request, pack_buf_send);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to pack message");
		fprintf(stderr, "Failed to pack message\n");
		goto abort;
	}

	/*
	 * Send the IPC message to VEOS and wait for the acknowledgement
	 * from VEOS
	 */
	retval = velib_send_cmd(sock_fd, pack_buf_send, pack_msg_len);
	if (retval != pack_msg_len) {
		VE_RPMLIB_ERR("Failed to send message: %d bytes written",
								retval);
		retval = -ECANCELED;
		goto hndl_free_buff;
	}
	VE_RPMLIB_DEBUG("Send data successfully to VEOS and "
						"waiting to receive....");
	unpack_buf_recv = malloc(MAX_PROTO_MSG_SIZE);
	if (!unpack_buf_recv) {
		VE_RPMLIB_ERR("Memory allocation failed: %s",
				strerror(errno));
		retval = -ECANCELED;
		goto hndl_free_buff;
	}

	/*
	 * Receive the IPC message from VEOS
	 */
	retval = velib_recv_cmd(sock_fd, unpack_buf_recv, MAX_PROTO_MSG_SIZE);
	VE_RPMLIB_DEBUG("Data received from VEOS %d bytes", retval);

	/* Unpack the data received from VEOS */
	res = velib_connect__unpack(NULL, retval,
					(const uint8_t *)(unpack_buf_recv));
	if (!res) {
		VE_RPMLIB_ERR("Failed to unpack message: %d", retval);
		fprintf(stderr, "Failed to unpack message\n");
		goto abort;
	}

	if (recv_buf) {
		if (!res->has_rpm_msg) {
			VE_RPMLIB_ERR("No data in the received data",
				res->rpm_msg.len, recv_bufsize);
			fprintf(stderr, "No data in the received data\n");
			goto abort;
		}
		if (res->rpm_msg.len > recv_bufsize) {
			VE_RPMLIB_ERR("The length of the received message is too long: %d",
				res->rpm_msg.len);
			fprintf(stderr, "The length of the received message too long\n");
			goto abort;
		}
		memcpy(recv_buf, res->rpm_msg.data, recv_bufsize);
	}

	retval = res->rpm_retval;
	goto hndl_free_unpacked_msg;
abort:
	close(sock_fd);
	abort();
hndl_free_unpacked_msg:
	velib_connect__free_unpacked(res, NULL);
	free(unpack_buf_recv);
hndl_free_buff:
	free(pack_buf_send);
hndl_close_sock:
	close(sock_fd);
hndl_free_sock:
	free(ve_sock_name);
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}

/**
 * @brief This function is used to get the swapped memory size
 *	  from VEOS for the given VE process id.
 *
 * @param nodeid[in] VE node ID
 * @param ve_swap_node [out] Structure to get information
 *			     for swap about VE nodes
 * @return 0 on success and negative value on failure
 */
int ve_swap_nodeinfo(int nodeid, struct ve_swap_node_info *ve_swap_node)
{
	return ve_message_send_receive(nodeid, VE_SWAP_NODEINFO, NULL, 0,
			ve_swap_node, sizeof(struct ve_swap_node_info));
}

/**
 * @brief This function is used to get the swap status information
 *	  from VEOS for the given VE process id.
 *
 * @param nodeid[in] VE node ID
 * @param pids [in] Structre that contain VE processID array
 * @param ve_swap_status [out] Structure to get status information
 *			       of swap about VE process
 * @return 0 on success and negative value on failure
 */
int ve_swap_statusinfo(int nodeid, struct ve_swap_pids *pids,
			struct ve_swap_status_info *ve_swap_status)
{
	return ve_message_send_receive(nodeid, VE_SWAP_STATUSINFO,
			pids, sizeof(struct ve_swap_pids),
			ve_swap_status, sizeof(struct ve_swap_status_info));
}

/**
 * @brief This function is used to get the swap information
 *	  from VEOS for the given VE process id.
 *
 * @param nodeid[in] VE node ID
 * @param pids [in] Structre that contain VE processID array
 * @param ve_swap [out] Structure to get information of
 *			swap about VE process.
 *
 * @return 0 on success and negative value on failure
 */
int ve_swap_info(int nodeid, struct ve_swap_pids *pids,
			struct ve_swap_info *ve_swap)
{
	return ve_message_send_receive(nodeid, VE_SWAP_INFO,
				pids, sizeof(struct ve_swap_pids),
				ve_swap, sizeof(struct ve_swap_info));
}

/**
 * @brief This function is used to request for VEOS to swap out
 *	  of the given VE process id.
 *
 * @param nodeid[in] VE node ID
 * @param pids [in] Structre that contain VE processID array
 *
 * @return 0 on success and negative value on failure
 */
int ve_swap_out(int nodeid, struct ve_swap_pids *pids)
{
	return ve_message_send_receive(nodeid, VE_SWAP_OUT,
				pids, sizeof(struct ve_swap_pids),
				NULL, 0);
}

/**
 * @brief This function is used to request for VEOS to swap in
 *	  of the given VE process id.
 *
 * @param nodeid[in] VE node ID
 * @param pids [in] Structre that contain VE processID array
 * @return 0 on success and negative value on failure
 */
int ve_swap_in(int nodeid, struct ve_swap_pids *pids)
{
	return ve_message_send_receive(nodeid, VE_SWAP_IN,
				pids, sizeof(struct ve_swap_pids),
				NULL, 0);
}
