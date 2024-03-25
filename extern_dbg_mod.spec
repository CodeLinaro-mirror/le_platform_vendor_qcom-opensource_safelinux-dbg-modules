# If kversion isn't defined on the rpmbuild line, define it here.
%{!?kversion: %define kversion %(uname -r)}

%{!?with_oot_debug: %define with_oot_debug 0}

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

kaslr_store: to store the kaslr-offset, in order to facilitate ramdump
analysis.

memory_dump_v2: QTI memory dump driver allows various client subsystems
to register and allocate respective dump regions. At the time of deadlocks
or cpu hangs these dump regions are captured to give a snapshot of the 
system at the time of the crash.

xbl_log: This drivers provides a way to print XBL logs on the console. To
do so, it provides a debugfs entry which captures the logs stored
in this reserved memory region. This entry can now be used to read
and print the XBL logs to console.

%prep
%setup -qn %{name}

%build
%if %{with_oot_debug}
make KERNEL_VERSION=%{kversion}+debug modules
%else
make KERNEL_VERSION=%{kversion} modules
%endif

%install
rm -rf $RPM_BUILD_ROOT
%if %{with_oot_debug}
make KERNEL_VERSION=%{kversion}+debug INSTALL_MOD_PATH="$RPM_BUILD_ROOT" modules_install
rm -rf "$RPM_BUILD_ROOT/lib/modules/%{kversion}+debug/modules."*
%else
make KERNEL_VERSION=%{kversion} INSTALL_MOD_PATH="$RPM_BUILD_ROOT" modules_install
rm -rf "$RPM_BUILD_ROOT/lib/modules/%{kversion}/modules."*
%endif

%post
%if %{with_oot_debug}
depmod %{kversion}+debug
%else
depmod %{kversion}
%endif

%files
%if %{with_oot_debug}
%define kernel_module_path /lib/modules/%{kversion}+debug
%else
%define kernel_module_path /lib/modules/%{kversion}
%endif
%{kernel_module_path}/extra/minidump/minidump.ko
%{kernel_module_path}/extra/kaslr_store/kaslr_store.ko
%{kernel_module_path}/extra/memory_dump_v2/memory_dump_v2.ko
%{kernel_module_path}/extra/xbl_log/dump_boot_log.ko

%changelog
* Mon Oct 30 2023 Ninad Naik <quic_ninanaik@quicinc.com> 1.0
- Add xbl_log support

* Tue Aug 29 2023 Sankalp Negi <quic_snegi@quicinc.com> 1.0
- Add memory_dump_v2 support

* Tue Aug 08 2023 Parikshit Pareek <quic_ppareek@quicinc.com> 1.0
- Added kaslr support.

* Fri Jun 30 2023 Parikshit Pareek <quic_ppareek@quicinc.com> 1.0
- First commit!
