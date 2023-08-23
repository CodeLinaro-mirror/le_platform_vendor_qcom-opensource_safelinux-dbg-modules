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

kaslr_store: to store the kaslr-offset, in order to facilitate ramdump
analysis.

memory_dump_v2: QTI memory dump driver allows various client subsystems
to register and allocate respective dump regions. At the time of deadlocks
or cpu hangs these dump regions are captured to give a snapshot of the 
system at the time of the crash.

%prep
%setup -qn %{name}

%build
make KERNEL_VERSION=%{kversion}  modules

%install
rm -rf $RPM_BUILD_ROOT
make KERNEL_VERSION=%{kversion} INSTALL_MOD_PATH="$RPM_BUILD_ROOT" modules_install
rm -rf "$RPM_BUILD_ROOT/lib/modules/%{kversion}/modules."*

%post
depmod %{kversion}

%files
/lib/modules/%{kversion}/extra/minidump/minidump.ko
/lib/modules/%{kversion}/extra/kaslr_store/kaslr_store.ko
/lib/modules/%{kversion}/extra/memory_dump_v2/memory_dump_v2.ko

%changelog
* Tue Aug 29 2023 Sankalp Negi <quic_snegi@quicinc.com> 1.0
- Add memory_dump_v2 support

* Tue Aug 08 2023 Parikshit Pareek <quic_ppareek@quicinc.com> 1.0
- Added kaslr support.

* Fri Jun 30 2023 Parikshit Pareek <quic_ppareek@quicinc.com> 1.0
- First commit!
