Name: nfcd

Version: 1.1.14
Release: 0
Summary: NFC daemon
License: BSD
URL: https://github.com/sailfishos/nfcd
Source: %{name}-%{version}.tar.bz2

%define libglibutil_version 1.0.52
%define glib_version 2.32

BuildRequires: pkgconfig
BuildRequires: pkgconfig(glib-2.0) >= %{glib_version}
BuildRequires: pkgconfig(libdbusaccess)
BuildRequires: pkgconfig(libglibutil) >= %{libglibutil_version}
BuildRequires: file-devel

# license macro requires rpm >= 4.11
BuildRequires: pkgconfig(rpm)
%define license_support %(pkg-config --exists 'rpm >= 4.11'; echo $?)

Requires: glib2 >= %{glib_version}
Requires: libglibutil >= %{libglibutil_version}
Requires: systemd
Requires(pre): systemd
Requires(post): systemd
Requires(post): coreutils
Requires(postun): systemd

%description
Provides D-Bus interfaces to NFC functionality.

%package plugin-devel
Summary: Development files for %{name} plugins
Requires: pkgconfig(libglibutil)

%description plugin-devel
This package contains development files for %{name} plugins.

%package tools
Summary: Command line NFC tools
Requires: %{name}

%description tools
This package contains command line NFC tools.

%prep
%setup -q

%build
make %{_smp_mflags} LIBDIR=%{_libdir} KEEP_SYMBOLS=1 release pkgconfig

%install
%define target_wants_dir %{_unitdir}/network.target.wants
%define settings_dir %{_sharedstatedir}/nfcd/
%define settings_file %{settings_dir}/settings
make install DESTDIR=%{buildroot} LIBDIR=%{_libdir} UNITDIR=%{_unitdir}
install -d -m 0700 %{buildroot}/%{settings_dir}
mkdir -p %{buildroot}/%{target_wants_dir}
ln -s ../nfcd.service %{buildroot}/%{target_wants_dir}/nfcd.service

%check
make -C unit test

%pre
systemctl stop nfcd ||:

%post
if [ -f %{settings_file} ] ; then
  chown nfc:nfc %{settings_file} ||:
  chmod 600 %{settings_file} ||:
fi
systemctl daemon-reload ||:
systemctl start nfcd ||:

%postun
systemctl daemon-reload ||:

%files
%defattr(-,root,root,-)
%dir %attr(700,nfc,nfc) %{settings_dir}
%{_sbindir}/*
%{_sysconfdir}/dbus-1/system.d/*.conf
%{target_wants_dir}/nfcd.service
%{_unitdir}/nfcd.service
%if %{license_support} == 0
%license LICENSE
%endif

%files tools
%{_bindir}/*

%files plugin-devel
%defattr(-,root,root,-)
%dir %{_includedir}/nfcd
%{_includedir}/nfcd/*.h
%{_libdir}/pkgconfig/*.pc
