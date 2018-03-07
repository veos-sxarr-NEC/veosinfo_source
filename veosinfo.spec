%define _prefix /opt/nec/ve/veos
%define _localstatedir /var/opt/nec/ve/veos
%define _sysconfdir  /etc/opt/nec/ve
%define _ve_prefix  /opt/nec/ve

Name: veosinfo
Version: 1.0.1
Release: 1
Summary: RPM library to interact with VEOS and VE specific 'sysfs'
Group: System Environment/Libraries
License: LGPL
Source: %{name}-%{version}.tar.gz
Vendor:	NEC Corporation
BuildArch: x86_64
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
Requires: veos
Requires: libyaml

BuildRequires: log4c-devel
BuildRequires: systemd-devel
BuildRequires: veos-devel
BuildRequires: libtool
BuildRequires: autoconf
BuildRequires: automake
BuildRequires: protobuf-c
BuildRequires: protobuf-c-devel
BuildRequires: libyaml
BuildRequires: libyaml-devel
BuildRequires: libgudev1

%description
This library is responsible for interacting with 
VEOS and VE specific 'sysfs' to retrieve command
specific information.

%package devel
Summary: Development files for RPM library
Group: System Environment/Libraries
Requires: %{name} = %{version}-%{release}
Provides: %{_prefix}/include/veosinfo.h

%description devel
Header files for the RPM Library.

%prep
%setup -q -n %{name}-%{version}

%build
./autogen.sh
export CFLAGS="-I%{_prefix}/include"
%configure --prefix=%{_prefix} --localstatedir=%{_localstatedir} --sysconfdir=%{_sysconfdir} --with-ve-prefix=%{_ve_prefix}

make CFLAGS="%{optflags} $CFLAGS"

%install
make DESTDIR=%{buildroot} install
# remove unpackaged files from the buildroot
rm -f $RPM_BUILD_ROOT/%{_libdir}/*.la
rm -f $RPM_BUILD_ROOT/%{_libdir}/*.a

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%{_libdir}/libveosinfo.so.*

%files devel
%{_libdir}/lib*.so
%{_includedir}/*
