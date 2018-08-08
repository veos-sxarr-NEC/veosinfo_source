#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([veosinfo], [1.2.1], [in_aur_os@nti.jp.nec.com])
AM_INIT_AUTOMAKE(veosinfo, 1.2.1)
AC_CONFIG_SRCDIR([ve_sock.h])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_COMMANDS([PROTO],
	[protoc-c --proto_path=./ --c_out=./ ./veos_RPM.proto])
AC_ARG_WITH(ve-prefix, [AS_HELP_STRING([--with-ve-prefix],
            [Specify prefix of VE software [/opt/nec/ve]])],
            [ve_prefix=${with_ve_prefix}],
            [ve_prefix=/opt/nec/ve])
AC_SUBST(ve_prefix)

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

AC_CHECK_LIB([udev], [udev_device_get_syspath], [], [AC_MSG_ERROR([udev library support missing/incomplete])])
AC_CHECK_LIB([yaml], [yaml_parser_set_input_file], [], [AC_MSG_ERROR([yaml library support missing/incomplete])])
AC_CHECK_LIB([protobuf-c], [protobuf_c_message_pack], [], [AC_MSG_ERROR([protobuf library  support missing/incomplete])])
AC_CHECK_LIB([log4c], [log4c_init], [], [AC_MSG_ERROR([log4c library  support missing/incomplete])])
LDFLAGS="$LDFLAGS -L${prefix}/lib -L${prefix}/lib64"
AC_CHECK_LIB([velayout], [ve_layout], [], [AC_MSG_ERROR([velayout support missing/incomplete])])

LT_INIT
# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_rdev])
AC_TYPE_UID_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([memset realpath socket strcasecmp strchr strerror strtoul])

AC_CONFIG_FILES([Makefile
		veosinfo_comm.h
		veosinfo.h])
AC_OUTPUT
