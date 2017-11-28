Name: magicolor5430DL

Summary: Cups Driver for KONICA MINOLTA magicolor 5430 DL
Requires: ghostscript, cups >= 1.1.15
Version: 1.8.1
Release: 1
Source0: %{name}-%{version}.tar.gz
License: GPL
Vendor: KONICA MINOLTA
Packager: KONICA MINOLTA <http://printer.konicaminolta.net>
Group: Applications/Publishing

BuildRequires:  cups-devel >= 1.1.15, libjbig-devel

%if "%{_vendor}" == "redhat"
BuildRequires:  lcms-devel
%else
BuildRequires:  liblcms-devel
%endif

BuildRoot: %{_builddir}/%{name}-root

%description
This package contains KONICA MINOLTA CUPS LavaFlow stream(PCL-like) 
filter rastertokm5430dl and the PPD file. The filter converts CUPS 
raster data to KONICA MINOLTA LavaFlow stream, it uses jbig for 
compression and littleCMS for colormatching. For latest version of 
this package and source code, please check Website 
http://printer.konicaminolta.net/.

%prep
%setup -q

%build
./configure
make

%install
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT;
make DESTDIR=$RPM_BUILD_ROOT install

%clean
[ "$RPM_BUILD_ROOT" != "/" ] && [ -d $RPM_BUILD_ROOT ] && rm -rf $RPM_BUILD_ROOT;

%files
%defattr(-,root,root)
%{_libdir}/cups/filter/rastertokm5430dl
%{_datadir}/cups/model/KONICA_MINOLTA/km5430dl.ppd.gz
%{_datadir}/KONICA_MINOLTA/mc5430DL/*

%post
if [ "$1" = "2" ]; then
   PPD_DIR="/etc/cups/ppd/"
   FILETYPE=".ppd"
   LPADMIN="/usr/sbin/lpadmin"
   KM_PPD_DIR="/usr/share/cups/model/KONICA_MINOLTA"
   if [ -x $LPADMIN ] && [ -d $PPD_DIR ] && [ -n "`ls -1A ${PPD_DIR}`" ]; then
      KMPPDS=(`grep -l "magicolor 5430 DL" ${PPD_DIR}*`)
      for kmppd in ${KMPPDS[@]}; do
         ppdFile=${kmppd#${PPD_DIR}}
         Printer=${ppdFile%${FILETYPE}}
         ppdFileNew=`find "${KM_PPD_DIR}" -name "km5430dl.ppd.gz" -print`
         $LPADMIN -p $Printer -P $ppdFileNew
         if [ $? -eq 0 ]; then
            echo Succeed to update $Printer
         else
            echo Fail to update $Printer
         fi
      done
   fi
fi

if echo $MACHTYPE | grep "suse" ; then
   :
else
   if [ "$1" = "1" ]; then
      if [ -x /etc/init.d/cups ] ; then
	 /etc/init.d/cups restart
      fi
   fi
fi

%postun
if [ "$1" = "0" ]; then
   rm -rf /usr/share/KONICA_MINOLTA/mc5430DL

   PPD_DIR="/etc/cups/ppd/"
   FILETYPE=".ppd"
   LPADMIN="/usr/sbin/lpadmin"
   if [ -x $LPADMIN ] && [ -d $PPD_DIR ] && [ -n "`ls -1A ${PPD_DIR}`" ]; then
      KMPPDS=(`grep -l "magicolor 5430 DL" ${PPD_DIR}*`)
      for kmppd in ${KMPPDS[@]}; do
         ppdFile=${kmppd#${PPD_DIR}}
         Printer=${ppdFile%${FILETYPE}}
         $LPADMIN -x $Printer 
         if [ $? -eq 0 ]; then
             echo Succeed to delete $Printer
         else
             echo Fail to delete $Printer
         fi
      done
   fi
fi

if echo $MACHTYPE | grep "suse" ; then
   :
else
   if [ "$1" = "0" ]; then
      if [ -x /etc/init.d/cups ] ; then
	 /etc/init.d/cups restart
      fi
   fi
fi

%preun

%changelog
* Wed Feb 07 2007 Sean Zhan
- add BuildRequires so that rpm can be built with mock; libjbig-devel can get from Mandriva
* Mon Apr 03 2006 Sean Zhan
- add x86_64 support for Fedora Core
* Wed Mar 01 2006 Sean Zhan
- send extra UEL at end of USB job if the job size is 64 divisible
* Wed Feb 15 2006 Sean Zhan
- add Dot Counts support
* Tue Oct 05 2004 Sean Zhan
- remove ppd after uninstall, update ppd after upgrade
* Fri Jul 23 2004 Sean Zhan
- fix upgrade bug in %postun section,
- set cupsManualCopies to False to let printer handle multi-copies
* Thu Jul 08 2004 Sean Zhan
- add littleCMS colormatching support
* Wed Apr 21 2004 Sean Zhan
- update crd and halftone data
* Mon Jan 12 2004 Sean Zhan
- use combobox for optional trays and duplex unit
* Mon Jan 05 2004 Sean Zhan
- update papersize, mediatype, trays and their constraints
* Thu Dec 18 2003 Sean Zhan
- add job title
* Fri Dec 12 2003 Sean Zhan
- Width need 16 divisible for mc5430DL
* Mon Dec 08 2003 Sean Zhan
- magicolor 5430 DL initial beta build

