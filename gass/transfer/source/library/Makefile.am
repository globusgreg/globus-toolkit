include_HEADERS = \
	globus_gass_transfer.h \
	globus_gass_transfer_proto.h
lib_LTLIBRARIES = libglobus_gass_transfer.la

libglobus_gass_transfer_la_CPPFLAGS = $(PACKAGE_DEP_CFLAGS) -I$(top_builddir)
libglobus_gass_transfer_la_LDFLAGS = \
        -version-info $(MAJOR_VERSION):$(MINOR_VERSION):$(AGE_VERSION) \
        -no-undefined
libglobus_gass_transfer_la_LIBADD = $(PACKAGE_DEP_LIBS)
libglobus_gass_transfer_la_SOURCES = \
	globus_gass_transfer.c \
	globus_gass_transfer.h \
	globus_gass_transfer_attribute.c \
	globus_gass_transfer_client.c \
	globus_gass_transfer_http.c \
	globus_gass_transfer_keyvalue.c \
	globus_gass_transfer_proto.c \
	globus_gass_transfer_proto.h \
	globus_gass_transfer_referral.c \
	globus_gass_transfer_request.c \
	globus_gass_transfer_send_recv.c \
	globus_gass_transfer_server.c \
	globus_gass_transfer_text.c \
	globus_i_gass_transfer.h \
	globus_i_gass_transfer_keyvalue.h \
	globus_l_gass_transfer_http.h

DOC_STAMPS =
if ENABLE_DOXYGEN
DOC_STAMPS += doc-stamp

doc-stamp: Doxyfile $(libglobus_gass_transfer_la_SOURCES)
	rm -rf doc
	$(DOXYGEN)
	touch $@

clean-local:
	rm -rf doc doc-stamp
endif

all-local: $(DOC_STAMPS)

install-data-local: $(DOC_STAMPS)
	if test -d doc/man/man3; then \
		install -d -m 755 $(DESTDIR)$(mandir)/man3; \
	        $(INSTALL) -m 644 doc/man/man3/[Gg][Ll][Oo][Bb][Uu][Ss]*.3 $(DESTDIR)$(mandir)/man3 ; \
        fi
	if test -d doc/html; then \
		for dir in `(cd doc; find html -type d)`; do \
			install -d -m 755 $(DESTDIR)$(docdir)/$$dir; \
		done ; \
		for f in `(cd doc; find html -type f)`; do \
			$(INSTALL) -m 644 doc/$$f $(DESTDIR)$(docdir)/$$f ; \
		done ; \
	fi
