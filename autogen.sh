#! /bin/sh
aclocal
autoheader
libtoolize
automake --add-missing --foreign
autoconf

