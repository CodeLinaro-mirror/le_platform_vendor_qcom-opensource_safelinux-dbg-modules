# If kversion isn't defined on the rpmbuild line, define it here.
%{!?kversion: %define kversion %(uname -r)}

%define kmod_name external-dbg
%define debug_package %{nil}

Name: kernel-module-%{kmod_name}
Version: 1.0
Release:        1%{?dist}
Summary: Build support for external kernel modules

License: GPLv2
Source0: %{name}-%{version}.tar.gz

BuildRequires: kernel-automotive-devel-uname-r = %{kversion}
Requires: kernel-automotive-core-uname-r = %{kversion}

%description
For building external kernel modules mentioned as following:

minidump: to dump certain regions of the RAM, for debugging, as a
result of panic.

%prep
%setup -q
echo "# Load minidump.ko at boot" > minidump.conf
echo "minidump" >> minidump.conf

%build
make KERNEL_VERSION=%{kversion}  modules

%install
rm -rf $RPM_BUILD_ROOT
make KERNEL_VERSION=%{kversion} INSTALL_MOD_PATH="$RPM_BUILD_ROOT" modules_install
rm -rf "$RPM_BUILD_ROOT/lib/modules/%{kversion}/modules."*

%{__install} -d %{buildroot}%{_sysconfdir}/modules-load.d/
%{__install} minidump.conf %{buildroot}%{_sysconfdir}/modules-load.d/

%post
depmod %{kversion}

%files
%{_sysconfdir}/modules-load.d/minidump.conf
/lib/modules/%{kversion}/extra/minidump/minidump.ko

%changelog
* Fri Jun 30 2023 Parikshit Pareek <quic_ppareek@quicinc.com> 1.0
- First commit!
