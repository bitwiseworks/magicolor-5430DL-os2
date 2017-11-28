AUTOMAKE_OPTIONS = gnu
filterdir=@CUPS_SERVERBIN@/filter
filter_PROGRAMS=rastertokm5430dl
rastertokm5430dl_SOURCES=rastertokmlf.c bmp.c
INCLUDES=$(all_includes)
noinst_HEADERS=kmlf.h
install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(KM_PPDDIR)
	$(mkinstalldirs) $(DESTDIR)$(KM_DATADIR)/Halftones
	$(mkinstalldirs) $(DESTDIR)$(KM_DATADIR)/Profiles
	$(INSTALL_DATA) $(srcdir)/km_en.ppd.gz $(DESTDIR)$(KM_PPDDIR)/km5430dl.ppd.gz
	$(INSTALL_DATA) $(srcdir)/km_ht_00.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_00.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_01.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_01.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_02.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_02.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_03.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_03.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_10.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_10.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_11.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_11.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_12.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_12.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_13.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_13.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_20.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_20.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_21.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_21.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_22.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_22.bmp
	$(INSTALL_DATA) $(srcdir)/km_ht_23.bmp $(DESTDIR)$(KM_DATADIR)/Halftones/km_ht_23.bmp
	$(INSTALL_DATA) $(srcdir)/km_0.icm $(DESTDIR)$(KM_DATADIR)/Profiles/km_0.icm
	$(INSTALL_DATA) $(srcdir)/km_1.icm $(DESTDIR)$(KM_DATADIR)/Profiles/km_1.icm
	$(INSTALL_DATA) $(srcdir)/km_2.icm $(DESTDIR)$(KM_DATADIR)/Profiles/km_2.icm
	$(INSTALL_DATA) $(top_srcdir)/COPYING $(DESTDIR)$(KM_DATADIR)/COPYING
uninstall-local:
	rm -f $(DESTDIR)$(KM_PPDDIR)km5430dl.ppd.gz
	if [ -d $(DESTDIR)$(KM_PPDDIR) ] && [ -z "`ls -1A $(DESTDIR)$(KM_PPDDIR)`" ]; then \
	   rm -rf $(DESTDIR)$(KM_PPDDIR); \
 	fi
	rm -rf $(DESTDIR)$(KM_DATADIR)
rastertokm5430dl_LDADD=@LIBLCMS@ @LIBJBIG@
EXTRA_DIST = km_en.ppd.gz km_ht_00.bmp km_ht_01.bmp km_ht_02.bmp km_ht_03.bmp km_ht_10.bmp km_ht_11.bmp km_ht_12.bmp km_ht_13.bmp km_ht_20.bmp km_ht_21.bmp km_ht_22.bmp km_ht_23.bmp km_0.icm km_1.icm km_2.icm

