# This file is part of memstomp.
#
# Copyright 2009 Lennart Poettering
# Copyright 2011 Red Hat
#
# memstomp is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# memstomp is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with memstomp. If not, see <http://www.gnu.org/licenses/>.

EXTRA_DIST = \
	bootstrap.sh \
	GPL2 \
	LGPL3 \
	GPL3 \
	README \
	memstomp.in

#include_HEADERS = \
#	memstomp.h

lib_LTLIBRARIES = \
	libmemstomp.la \
	libmemstomp-backtrace-symbols.la

bin_SCRIPTS = \
	memstomp

CLEANFILES = \
	memstomp

libmemstomp_la_SOURCES = \
	memstomp.c
libmemstomp_la_LDFLAGS = \
	-avoid-version \
	-module \
	-export-dynamic \
	-shared \
	-prefer-pic
libmemstomp_la_LIBADD = \
	$(PTHREAD_LIBS) \
	-lrt \
	-ldl
libmemstomp_la_CFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DSONAME=\"libmemstomp.so\"

libmemstomp_backtrace_symbols_la_SOURCES = \
	backtrace-symbols.c
libmemstomp_backtrace_symbols_la_LDFLAGS = \
	-avoid-version \
	-module \
	-export-dynamic \
	-shared \
	-prefer-pic
libmemstomp_backtrace_symbols_la_LIBADD = \
	$(PTHREAD_LIBS) \
	-lrt \
	-ldl
libmemstomp_backtrace_symbols_la_CFLAGS = \
	$(PTHREAD_CFLAGS)

memstomp: memstomp.in Makefile
	sed -e 's,@PACKAGE_STRING\@,$(PACKAGE_STRING),g' \
                -e 's,@PACKAGE_NAME\@,$(PACKAGE_NAME),g' < $< > $@
	chmod +x memstomp

install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/libmemstomp.la
	rm -f $(DESTDIR)$(libdir)/libmemstomp-backtrace-symbols.la

uninstall-hook:
	rm -f $(DESTDIR)$(libdir)/libmemstomp.so
	rm -f $(DESTDIR)$(libdir)/libmemstomp-backtrace-symbols.so

dist-gzip:
	cd $(srcdir); git status | grep working.directory.clean || (echo "You should commit your changes before 'make rpm'.")
	(cd $(srcdir); git archive --prefix=memstomp-$(VERSION)/ --format=tar HEAD) | gzip > memstomp-$(VERSION).tar.gz

ACLOCAL_AMFLAGS = -I m4
