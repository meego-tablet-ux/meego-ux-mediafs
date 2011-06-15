%define name        meego-ux-mediafs
%define release     0
%define version     0.6
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
cmake .
make

#%config /etc/%{name}.conf

%install
mkdir -p %{buildroot}/usr/bin
cp %{name}d %{buildroot}/usr/bin/
mkdir -p %{buildroot}/usr/lib/%{name}/readers
cp libplugin-*.so %{buildroot}/usr/lib/%{name}/readers/

mkdir -p %{buildroot}/etc/init.d
cp %{name} %{buildroot}/etc/init.d/

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

cat <<EOF >%{name}.conf
square		ratio=1.00 crop=centre maxwidth_px=128 maxheight_px=128
preview		maxwidth_px=512 maxheight_px=512
EOF
mv %{name}.conf %{buildroot}/etc/

%post
chkconfig --add meego-ux-mediafs
if ! cat /etc/fuse.conf | grep -qw user_allow_other; then
	echo "user_allow_other" >> /etc/fuse.conf
fi

%preun
if [ -f /etc/init.d/meego-ux-mediafs ]; then
	/etc/init.d/meego-ux-mediafs stop
	/etc/init.d/meego-ux-mediafs restore
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
