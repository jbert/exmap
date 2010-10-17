# This spec violates Fedora packaging guidelines.
# Specifically, packages must not contain add-on kernel modules (kmod-*);
# kernel modules should be submitted upstream.
# For details, see:
# http://fedoraproject.org/wiki/Packaging/Guidelines#No_External_Kernel_Modules
#
# Nevertheless, this spec build two packages:
#   exmap (base userland)
#   exmap-kmod (kernel module)
#
# The kmod subpackage is based on tips found at:
# http://fedoraproject.org/wiki/Obsolete/KernelModules
# 
# Use global instead of define for scoping as recommended at:
# http://fedoraproject.org/wiki/Packaging/Guidelines#.25global_preferred_over_.25define

%global kmod_name exmap

# define the full path to kmodtool
%global kmodtool /usr/lib/rpm/redhat/kmodtool

# get the kernel version , but strip off the machine arch to
# avoid hard-coding the kernel version
%global dotvariant .%(uname -m 2> /dev/null)
%global my_verrel %(%{kmodtool} verrel | sed "s/\.$(uname -m)//" 2> /dev/null)

# The upstream git repo at http://github.com/jbert/exmap
# provides tags for intermediate releases,
# such as exmap-0.11-pre0
# To see the tags, use `git tag'.
# If you want to build an intermediate release,
# modify upstream_tag in the following definition.
# If not using a pre-release, just comment-out the next global.
%global upstream_tag .pre0


# ----------- base userland package ----------------
Name:      exmap
Summary:   see how much memory is in use by different processes

Version:   0.11
Release:   1%{?dist}%{?upstream_tag}

Group:     Applications/Engineering
License:   GPLv2

# NOTE: exmap downloads has not been updated since 28-Sep-2006
#URL:       http://www.berthels.co.uk/exmap

# NOTE: original author is now on github
URL:       http://github.com/jbert/exmap

Source0:   %{name}-%{version}.tar.gz
BuildRoot: %(mktemp -ud %{_tmppath}/%{name}-%{version}-%{release}-XXXXXX)

BuildRequires: pcre-devel
BuildRequires: boost-devel
BuildRequires: gtkmm24-devel

Requires: pcre
Requires: boost
Requires: gtkmm24
Requires: %{kmod_name}-kmod >= %{?epoch:%{epoch}:}%{version}-%{release}

%description
Exmap is a tool which allows you to see how much memory is in
use by different processes, mapped files, ELF sections and ELF
symbols at a given moment in time.

It accounts for shared memory in the following way: when a page
of memory is found to be shared between N processes, the totals
for each process are given a 1/N share of that page.

Exmap doesn't allow you to see details on how and where memory on
the heap is allocated. Tools such as valgrind/massif and memprof
are more useful in this case.


%prep
%setup -q

%clean
%{__rm} -rf %{buildroot}

%files
%defattr(-,root,root,-)
%doc README
%doc COPYING
%doc TODO
%doc doc.asciidoc
%doc doc.html
%doc FAQ.asciidoc
%doc FAQ.html
%doc screenshots/screenshot-processes.png
%doc screenshots/screenshot-files.png
%{_bindir}/gexmap



# ----------- kmod subpackage ----------------
%package -n kmod-%{kmod_name}

Summary:  %{kmod_name} kernel module(s)
Group:    System Environment/Kernel

# redhat-rpm-config provides /usr/lib/rpm/redhat/kmodtool
BuildRequires: redhat-rpm-config
buildrequires: kernel-devel

# needed for plague to make sure it builds for i586 and i686
ExclusiveArch:  i586 i686 x86_64 ppc ppc64

Provides: %{kmod_name}-kmod = %{?epoch:%{epoch}:}%{version}-%{release}

Requires: kernel = %{my_verrel}

# module-init-tools provides /sbin/depmod
Requires(post):   module-init-tools
Requires(postun): module-init-tools

%description   -n kmod-%{kmod_name}
This package provides the %{kmod_name} kernel module built for the Linux
kernel %{my_verrel}%{?dotvariant}.

Exmap is a tool which allows you to see how much memory is in
use by different processes, mapped files, ELF sections and ELF
symbols at a given moment in time.

%post -n kmod-%{kmod_name}
/sbin/depmod -aeF /boot/System.map-%{my_verrel}%{?dotvariant} %{my_verrel}%{?dotvariant} &> /dev/null || :

%postun -n kmod-%{kmod_name}
/sbin/depmod -aF /boot/System.map-%{my_verrel}%{?dotvariant} %{my_verrel}%{?dotvariant} &> /dev/null || :


%files -n kmod-%{kmod_name}
%defattr(644,root,root,755)
/lib/modules/%{my_verrel}%{?dotvariant}/extra/%{kmod_name}/


# ------------- build files for both packages -------------
%build
make %{?_smp_mflags}
make docs

# ------------- install files for both packages -------------
%install
%{__rm} -rf %{buildroot}

# userland
%{__mkdir_p} %{buildroot}%{_bindir}
%{__install} -p src/gexmap %{buildroot}%{_bindir}

# kmod
%{__mkdir_p} %{buildroot}/lib/modules/%{my_verrel}%{?dotvariant}/extra/%{kmod_name}
%{__install} -pm 755 kernel/%{kmod_name}.ko %{buildroot}/lib/modules/%{my_verrel}%{?dotvariant}/extra/%{kmod_name}



%changelog
* Sun Oct 17 2010 Paul Morgan <jumanjiman@gmail.com> 0.11-1.pre0
- new package built with tito

