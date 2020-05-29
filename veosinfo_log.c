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
 * @file veosinfo_log.c
 * @brief Handles the debugging messages of veosinfo library.
 *
 * @internal
 * @author RPM command
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "veosinfo_log.h"

#define VE_RPMLIB_LOG_CATEGORY "veos.command.libveosinfo"

static log4c_category_t *category;
static pthread_mutex_t log_mtx;

__attribute__ ((constructor))
static void ve_rpmlib_constructor(void)
{
	errno = 0;
	pthread_mutex_init(&log_mtx, NULL);
	/*
	 * There is no harm calling log4c_init() multiple times
	 * in the same process.
	 */
	if (log4c_init())
		fprintf(stderr, "Log4c initialization failed.\n");

	category = log4c_category_get(VE_RPMLIB_LOG_CATEGORY);
}

void ve_rpmlib__vlog(int prio, const log4c_location_info_t *loc, const char *fmt,
		  va_list list)
{
	pthread_mutex_lock(&log_mtx);
	if (log4c_category_is_priority_enabled(category, prio))
		__log4c_category_vlog(category, loc, prio, fmt, list);
	pthread_mutex_unlock(&log_mtx);
}

void ve_rpmlib__log(int prio, const log4c_location_info_t *loc, const char *fmt,
		 ...)
{
	va_list ap;

	va_start(ap, fmt);
	ve_rpmlib__vlog(prio, loc, fmt, ap);
	va_end(ap);
}

__attribute__ ((destructor))
static void ve_rpmlib_destructor(void)
{
	/*
	 * log4c_fini() always returns 0.
	 * However according to the documentation, it's recommended
	 * to check the return value.
	 */
	if (log4c_fini())
		fprintf(stderr, "Log4c destructor failed.\n");

	pthread_mutex_destroy(&log_mtx);
}
