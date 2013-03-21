Name:           globus-connect-multiuser
Version:        2.0.12
Release:        1%{?dist}
Summary:        Globus Connect Multi-User
%global _name %(tr - _ <<< %{name})

%global transferapi_name globusonline-transfer-api-client
%global transferapi_version 0.10.14
Group:          System Environment/Libraries
License:        ASL 2.0
URL:            http://www.globus.org/
Source:         %{_name}-%{version}.tar.gz
Source1:        %{transferapi_name}-%{transferapi_version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
BuildArch:      noarch

%if "%{?rhel}" == "5"
%global python  python26
%else
%global python  python
%endif

Requires:       globus-gridftp-server-progs >= 6
Requires:       globus-gass-copy-progs
Requires:       globus-gss-assist-progs
Requires:       myproxy
Requires:       myproxy-server
Requires:       gsi-openssh
Requires:       gsi-openssh-clients
Requires:       globus-gsi-cert-utils-progs
Requires:       globus-simple-ca
Requires:       globus-gridmap-verify-myproxy-callout
Requires:       globus-gridmap-eppn-callout
Requires:	%{python}
Obsoletes:      gcmu


%description
The Globus Toolkit is an open source software toolkit used for building Grid
systems and applications. It is being developed by the Globus Alliance and
many others all over the world. A growing number of projects and companies are
using the Globus Toolkit to unlock the potential of grids for their cause.

The %{name} package contains:
Globus Connect Multi-User

%prep
%setup -q -n %{_name}-%{version}
%setup -a 1 -D -T -n %{_name}-%{version}

%build
cd %{transferapi_name}-%{transferapi_version}
python setup.py build
cd ..
python_exe="`%{python} -c 'import sys; print sys.executable'`"

for templ in templates/*; do
    sed -e "s|@PYTHON@|$python_exe|g" \
        -e "s|@libdir@|%{_libdir}|g" < "$templ" > `basename "$templ" .in`
done
python setup.py build

%install
rm -rf $RPM_BUILD_ROOT
mkdir $RPM_BUILD_ROOT
cd %{transferapi_name}-%{transferapi_version}
%{python} setup.py install --root $RPM_BUILD_ROOT --install-lib=%{_libdir}/%{name}
cd ..
%{python} setup.py install --root $RPM_BUILD_ROOT --prefix=/usr
%global __os_install_post %(echo '%{__os_install_post}' | sed -e 's!/usr/lib[^[:space:]]*/brp-python-bytecompile[[:space:]].*$!!g')

test -x /usr/lib/rpm/brp-python-bytecompile && \
    /usr/lib/rpm/brp-python-bytecompile "${python_exe}"

%clean
rm -rf $RPM_BUILD_ROOT

%files
%{_bindir}/globus-connect-multiuser-setup
/usr/lib*/python*
/usr/lib*/globus-connect-multiuser/*
%config(noreplace) %{_sysconfdir}/%{name}.conf

%changelog
* Thu Mar 21 2013 Globus Toolkit <support@globus.org> 2.0.12-1
- Add detection of ec2 private IP addresses and set DataInterface
- Better automatic support of NATed servers
- Don't depend on particular arch for GT components

* Tue Mar 19 2013 Globus Toolkit <support@globus.org> 2.0.11-1
- Missing break in retry code

* Tue Mar 19 2013 Globus Toolkit <support@globus.org> 2.0.10-1
- Add retries on getting authentication token

* Tue Mar 19 2013 Globus Toolkit <support@globus.org> 2.0.9-1
- Fix some configuration file handling

* Tue Mar 19 2013 Globus Toolkit <support@globus.org> 2.0.8-1
- Fix some configuration file handling
- Fix nameopt for ca creation for real
- add socket timeout

* Mon Mar 18 2013 Globus Toolkit <support@globus.org> 2.0.7-1
- remove @PYTHON@ from globus-connect-multiuser-setup
- Fix nameopt for ca creation

* Mon Mar 18 2013 Globus Toolkit <support@globus.org> 2.0.6-2
- Update transfer api client version

* Fri Mar 15 2013 Globus Toolkit <support@globus.org> 2.0.6-1
- fix issues where MyProxyCA DN doesn't match MyProxy DN

* Fri Mar 15 2013 Globus Toolkit <support@globus.org> 2.0.5-1
- Fix setup.py

* Fri Mar 15 2013 Globus Toolkit <support@globus.org> 2.0.4-1
- fix MANIFEST.in

* Thu Mar 14 2013 Globus Toolkit <support@globus.org> 2.0.3-1
- dummy __init__.py

* Wed Mar 13 2013 Globus Toolkit <support@globus.org> 2.0.2-1
- Initial packaging as globus-connect-multiuser
