%define name        meego-ux-mediafs
%define release     0
%define version     0.0.2
%define buildroot   %{_topdir}/%{name}-%{version}-root

BuildRequires: cmake fuse-devel ImageMagick-devel file-devel gstreamer-devel glib2-devel
BuildRoot:     %{buildroot}
Name:          %{name}
Version:       %{version}
Release:       %{release}
Source:        %{name}-%{version}.tar.gz
License:       LGPL
Summary:       Indexer and thumbnailer daemon for MeeGo

%description
N/A


%prep
%setup -q


%build
%cmake .
make %{?_smp_mflags}


%install
install -m 755 -d %{buildroot}/usr/bin
install -m 755 %{name}d %{buildroot}/usr/bin/
install -m 755 -d %{buildroot}/usr/lib/%{name}/readers
install -m 755 libplugin-*.so %{buildroot}/usr/lib/%{name}/readers/

install -m 755 -d %{buildroot}/etc/init.d
install -m 755 %{name} %{buildroot}/etc/init.d/

install -m 644 %{name}.conf %{buildroot}/etc/

for i in 0 1 2 6; do
	mkdir %{buildroot}/etc/rc$i.d
	cd %{buildroot}/etc/rc$i.d
	ln -s ../init.d/%{name} ./K76%{name}
done

for i in 3 4 5; do
	mkdir %{buildroot}/etc/rc$i.d
	cd %{buildroot}/etc/rc$i.d
	ln -s ../init.d/%{name} ./S26%{name}
done


%post
chkconfig --add meego-ux-mediafs
if ! cat /etc/fuse.conf | grep -qw user_allow_other; then
	echo "user_allow_other" >> /etc/fuse.conf
fi


%preun
if [ -f /etc/init.d/meego-ux-mediafs ]; then
	/etc/init.d/meego-ux-mediafs stop
fi
chkconfig --del meego-ux-mediafs


%files
%defattr(-,root,root)
/usr/bin/%{name}d
/etc/init.d/%{name}
/etc/%{name}.conf
/usr/lib/%{name}/*
/etc/rc0.d/K76%{name}
/etc/rc1.d/K76%{name}
/etc/rc2.d/K76%{name}
/etc/rc6.d/K76%{name}
/etc/rc3.d/S26%{name}
/etc/rc4.d/S26%{name}
/etc/rc5.d/S26%{name}

%config /etc/%{name}.conf


%clean
rm -rf %{buildroot}
