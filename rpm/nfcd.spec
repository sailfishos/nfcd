Name: nfcd
Version: 1.0.10
Release: 0
Summary: NFC daemon
Group: Development/Libraries
License: BSD
URL: https://git.merproject.org/mer-core/nfcd
Source: %{name}-%{version}.tar.bz2
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(libmce-glib)
BuildRequires: pkgconfig(libdbusaccess)
BuildRequires: pkgconfig(libglibutil) >= 1.0.34
BuildRequires: pkgconfig(libdbuslogserver-gio) >= 1.0.14
Requires: libglibutil >= 1.0.34
Requires: libdbuslogserver-gio >= 1.0.14
Requires: systemd
Requires(pre): systemd
Requires(post): systemd
Requires(post): coreutils
Requires(postun): systemd

%description
Provides D-Bus interfaces to NFC functionality.

%package plugin-devel
Summary: Development files for %{name} plugins
Requires: pkgconfig
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
make KEEP_SYMBOLS=1 release

%install
%define target_wants_dir %{_lib}/systemd/system/network.target.wants
%define settings_dir %{_sharedstatedir}/nfcd/
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}
install -d -m 0700 %{buildroot}/%{settings_dir}
mkdir -p %{buildroot}/%{target_wants_dir}
ln -s ../nfcd.service %{buildroot}/%{target_wants_dir}/nfcd.service

%check
make -C unit test

%pre
systemctl stop nfcd ||:

%post
chown nfc:nfc %{settings_dir}/* ||:
chmod 600 %{settings_dir}/* ||:
systemctl daemon-reload ||:
systemctl start nfcd ||:

%postun
systemctl daemon-reload ||:

%files
%defattr(-,root,root,-)
%dir %attr(700,nfc,nfc) %{settings_dir}
%{_sbindir}/*
%{_sysconfdir}/dbus-1/system.d/*.conf
/%{target_wants_dir}/nfcd.service
/%{_lib}/systemd/system/nfcd.service

%files tools
%{_bindir}/*

%files plugin-devel
%defattr(-,root,root,-)
%dir %{_includedir}/nfcd
%{_includedir}/nfcd/*.h
%{_libdir}/pkgconfig/*.pc
