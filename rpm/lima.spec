%define version 0.8.6

%define libpath /usr/lib
%ifarch x86_64
  %define libpath /usr/lib64
%endif

Summary: lima
Name: lima
Version: %version
Release: 1
License: GPLv2
Group: Development
Source: http://packages.openpanel.com/archive/lima-%{version}.tar.gz
BuildRoot: /var/tmp/%{name}-buildroot
Requires: openpanel-authd
Requires: openpanel-mod-user

%description
lima
The OpenPanel mailing list manager

%prep
%setup -q -n lima-%version

%build
BUILD_ROOT=$RPM_BUILD_ROOT
./configure
make
cd limasubmit && ./configure && make && cd ..

%install
BUILD_ROOT=$RPM_BUILD_ROOT
rm -rf ${BUILD_ROOT}
mkdir -p ${BUILD_ROOT}/var/lima
mkdir -p ${BUILD_ROOT}/var/lima/bin
install -m 750 -d ${BUILD_ROOT}/var/lima/lists
install -m 750 -d ${BUILD_ROOT}/var/lima/queue
mkdir -p ${BUILD_ROOT}/var/lima/queue/in
mkdir -p ${BUILD_ROOT}/var/lima/queue/moderation
mkdir -p ${BUILD_ROOT}/var/lima/queue/out
install -m 750 -d  ${BUILD_ROOT}/var/lima/templates
mkdir -p ${BUILD_ROOT}/etc/rc.d/init.d
install -m 755 contrib/redhat.init ${BUILD_ROOT}/etc/rc.d/init.d/limadaemon
cp -rf limadaemon.app ${BUILD_ROOT}/var/lima/bin/
cp -rf limasubmit/limasubmit.app ${BUILD_ROOT}/var/lima/bin/
cp limacreate ${BUILD_ROOT}/var/lima/bin/
cp templates/* ${BUILD_ROOT}/var/lima/templates

%post
grep "^lima" /etc/postfix/master.cf >/dev/null 2>&1 || {
  cat >> /etc/postfix/master.cf << _EOF_
lima    unix    -       n       n       -       -       pipe
  flags=F user=lima argv=/usr/local/bin/lima-submit $recipient $sender
_EOF_
}
chkconfig --level 2345 limadaemon on
groupadd -f lima >/dev/null 2>&1
useradd lima -r -g lima >/dev/null 2>&1
chown lima:lima /var/lima/lists
chown -R lima:lima /var/lima/queue
chown -R lima:lima /var/lima/templates

%files
%defattr(-,root,root)
/
