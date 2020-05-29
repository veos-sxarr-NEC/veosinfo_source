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
 * @file ve_sock.c
 * @brief Handles IPC communication between RPM library and VEOS
 *
 * @internal
 * @author RPM command
 */
#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include "ve_sock.h"
#include "veosinfo_log.h"

/**
 * @brief Create socket for communication with VEOS module
 *
 * @param sockpath[in] Socket file path name
 *
 * @return Socket file descriptor on success and negative values on failure
 */
int velib_sock(char *sockpath)
{
	struct sockaddr_un sa = {0};
	int sockfd = -1;
	int retval = -1;
	char *sock_path = sockpath;

	VE_RPMLIB_TRACE("Entering");

	VE_RPMLIB_INFO("SOCKPATH:%s", sockpath);

	if (!sockpath) {
		VE_RPMLIB_ERR("Wrong argument received: sockpath = %p",
							sockpath);
		errno = EINVAL;
		goto hndl_return;
	}
	/* Create a socket to enable communication between VEOS and RPM library
	*/
	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd < 0) {
		VE_RPMLIB_ERR("Failed to create '%s' socket: %s",
				sock_path, strerror(errno));
		goto hndl_return;
	}
	VE_RPMLIB_DEBUG("Socket created successfully and socket descriptor = %d",
							sockfd);
	retval = sockfd;

	if (strlen(sockpath) > (sizeof(sa.sun_path) - 1)) {
		VE_RPMLIB_ERR("Socket path is too long.: %s\n", sockpath);
		errno = ENAMETOOLONG;
		retval = -2;
		goto hndl_return;
	}
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, sock_path, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	/* Connect with VEOS socket to enable communication between VEOS and
	 * RPM library
	 */
	if (-1 == connect(sockfd, (struct sockaddr *)&sa, sizeof(sa))) {
		VE_RPMLIB_ERR("Connection to socket failed: %s",
				strerror(errno));
		retval = -2;
	}
hndl_return:
	VE_RPMLIB_TRACE("Exiting");
	return retval;
}


/**
 * @brief Communicate with VEOS modules to send data
 *
 * @param socket_fd[in] File descriptor used to communicate with VEOS
 * @param buf[in] Pointer to buffer to send
 * @param max_len[in] Size of buffer
 *
 * @return positive value on success, -1 on failure
 */
int velib_send_cmd(int socket_fd, void *buf, int max_len)
{
	ssize_t transferred = 0;
	ssize_t write_byte = 0;

	VE_RPMLIB_TRACE("Entering");

	if (!buf) {
		VE_RPMLIB_ERR("Wrong argument received: buf: %p", buf);
		errno = EINVAL;
		transferred = -1;
		goto send_error;
	}

	while ((write_byte = send(socket_fd, buf + transferred,
					max_len - transferred, MSG_NOSIGNAL)) != 0) {
		if (-1 == write_byte) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			} else if (EPIPE == errno) {
				VE_RPMLIB_ERR("IPC failed: %s", strerror(errno));
				perror("IPC failed");
				close(socket_fd);
				abort();
			} else {
				VE_RPMLIB_ERR("Writing on socket failed: %s",
						strerror(errno));
				perror("Writing on socket failed");
				close(socket_fd);
				abort();
			}
		}
		transferred += write_byte;
		if (transferred == max_len) {
			VE_RPMLIB_DEBUG("successfully transferred = %zu," \
					" write_byte = %zu",
					transferred, write_byte);
			break;
		}
		VE_RPMLIB_DEBUG("transferred = %zu, remaining_bytes = %zu",
				transferred, (max_len-transferred));
	}
send_error:
	VE_RPMLIB_TRACE("Exiting");
	return transferred;

}

/**
 * @brief Communicate with VEOS modules to receive data
 *
 * @param socket_fd[in] File descriptor to communicate with VEOS
 * @param buf[out] Pointer to buffer to receive
 * @param max_len[in] Size of buffer
 *
 * @return 0 on success and -1 on failure
 */
int velib_recv_cmd(int socket_fd, void *buf, int max_len)
{
	ssize_t read_byte = -1;

	VE_RPMLIB_TRACE("Entering");

	if (!buf) {
		VE_RPMLIB_ERR("Wrong argument received: buf: %p", buf);
		errno = EINVAL;
		goto recv_error;
	}
	while ((read_byte = recv(socket_fd, buf, max_len, 0)) == -1) {
		if (errno == EINTR || errno == EAGAIN) {
			VE_RPMLIB_ERR("Received command from VEOS"
					"to RPM failed :%s",
					strerror(errno));
			continue;
		} else {
			VE_RPMLIB_ERR("Reading from socket failed: %s",
					strerror(errno));
			perror("Reading from socket failed");
			close(socket_fd);
			abort();
		}
	}
	if (!read_byte) {
		VE_RPMLIB_ERR("peer has performed an orderly shutdown");
		close(socket_fd);
		abort();
	}
	VE_RPMLIB_DEBUG("successfully read %zu bytes", read_byte);
recv_error:
	VE_RPMLIB_TRACE("Exiting");
	return read_byte;
}
