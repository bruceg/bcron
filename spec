Name: @PACKAGE@
Summary: Bruce's Cron System
Version: @VERSION@
Release: 1
License: GPL
Group: Utilities/System
Source: http://untroubled.org/@PACKAGE@/@PACKAGE@-@VERSION@.tar.gz
BuildRoot: %{_tmppath}/@PACKAGE@-buildroot
URL: http://untroubled.org/@PACKAGE@/
Packager: Bruce Guenter <bruceg@em.ca>
BuildRequires: bglibs >= 1.100
Requires: ucspi-unix
Requires: supervise-scripts >= 3.5
Conflicts: vixie-cron
Conflicts: fcron
Conflicts: dcron

%description
Bruce's Cron System

%prep
%setup
echo gcc "%{optflags}" >conf-cc
echo gcc -s >conf-ld
echo %{_bindir} >conf-bin
echo %{_mandir} >conf-man

%build
make

%install
rm -fr %{buildroot}
make install_prefix=%{buildroot} install

mkdir -p %{buildroot}%{_mandir}/man{1,8}
cp bcron-{exec,sched,spool,start,update}.8 %{buildroot}%{_mandir}/man8
cp bcrontab.1 %{buildroot}%{_mandir}/man1

mkdir -p %{buildroot}/var/service/bcron-{sched/log,spool,update}
install -m 755 bcron-sched.run %{buildroot}/var/service/bcron-sched/run
install -m 755 bcron-sched-log.run %{buildroot}/var/service/bcron-sched/log/run
install -m 755 bcron-spool.run %{buildroot}/var/service/bcron-spool/run
install -m 755 bcron-update.run %{buildroot}/var/service/bcron-update/run
chmod +t %{buildroot}/var/service/bcron-sched

mkdir -p %{buildroot}/var/log/bcron

mkdir -p %{buildroot}/var/spool/cron/{crontabs,tmp}
mkfifo %{buildroot}/var/spool/cron/trigger

mkdir -p %{buildroot}/etc/bcron
mkdir -p %{buildroot}/etc/cron.d

%clean
rm -rf %{buildroot}

%pre
grep -q '^cron:' /etc/group \
|| groupadd -r cron
grep -q '^cron:' /etc/passwd \
|| useradd -r -d /var/spool/cron -s /sbin/nologin -g cron cron

%post
PATH="$PATH:/usr/local/bin"
if [ "$1" = 1 ]; then
  for svc in bcron-sched bcron-spool bcron-update; do
    if ! [ -e /service/$svc ]; then
      svc-add $svc
    fi
  done
else
  for svc in bcron-sched bcron-spool bcron-update; do
    svc -t /service/$svc
  done
fi

%preun
if [ "$1" = 0 ]; then
  for svc in bcron-sched bcron-spool bcron-update; do
    if [ -L /service/$svc ]; then
      svc-remove $svc
    fi
  done
fi

%files

%defattr(-,root,root)

%doc ANNOUNCEMENT COPYING NEWS README
%doc bcron.texi bcron.html

%config %dir /etc/bcron
%config %dir /etc/cron.d

%{_bindir}/*
%{_mandir}/*/*

/var/service/*

%attr(700,cron,cron) %dir /var/spool/cron
%attr(700,cron,cron) %dir /var/spool/cron/crontabs
%attr(700,cron,cron) %dir /var/spool/cron/tmp
%attr(600,cron,cron)      /var/spool/cron/trigger

%attr(700,root,root) %dir /var/log/bcron
