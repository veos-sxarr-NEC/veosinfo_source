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
 * @brief Header file for veosinfo.c file
 *
 * @internal
 * @author RPM command
 */

#ifndef __VEOSINFO_LOG_H
#define __VEOSINFO_LOG_H

#include <stdlib.h>
#include <log4c.h>
#include <log4c/category.h>
#include <log4c/priority.h>
#include <velayout.h>

enum ve_rpmlib_priority {
	VE_RPMLIB_LOG_ERR = LOG4C_PRIORITY_ERROR,
	VE_RPMLIB_LOG_INFO = LOG4C_PRIORITY_INFO,
	VE_RPMLIB_LOG_DEBUG = LOG4C_PRIORITY_DEBUG,
	VE_RPMLIB_LOG_TRACE = LOG4C_PRIORITY_TRACE,
};

void ve_rpmlib__vlog(int prio, const log4c_location_info_t *, const char *fmt,
		va_list list);
void ve_rpmlib__log(int prio, const log4c_location_info_t *, const char *fmt, ...);

#define VE_RPMLIB_LOG(prio, fmt, ...) do { \
	const log4c_location_info_t locinfo__ = \
	LOG4C_LOCATION_INFO_INITIALIZER(NULL); \
	ve_rpmlib__log(prio, &locinfo__, fmt, ##__VA_ARGS__); \
} while (0)

#define VE_RPMLIB_ERR(fmt, ...) VE_RPMLIB_LOG(VE_RPMLIB_LOG_ERR, \
	fmt, ##__VA_ARGS__)
#define VE_RPMLIB_INFO(fmt, ...) VE_RPMLIB_LOG(VE_RPMLIB_LOG_INFO, \
	fmt, ##__VA_ARGS__)
#define VE_RPMLIB_DEBUG(fmt, ...) VE_RPMLIB_LOG(VE_RPMLIB_LOG_DEBUG, \
	fmt, ##__VA_ARGS__)
#define VE_RPMLIB_TRACE(fmt, ...) VE_RPMLIB_LOG(VE_RPMLIB_LOG_TRACE, \
	"%s " fmt, __func__, ## __VA_ARGS__)
#define CAT_RPM cat_rpm
#define VE_RPM_ERROR(fmt, ...) \
	VE_LOG(CAT_RPM, LOG4C_PRIORITY_ERROR, fmt, ## __VA_ARGS__)
#define VE_RPM_DEBUG(fmt, ...) \
	VE_LOG(CAT_RPM, LOG4C_PRIORITY_DEBUG, fmt, ## __VA_ARGS__)
extern log4c_category_t *cat_rpm;
#endif
