# XXX - this is a massive hack to make autoreconf honour the ACLOCAL_FLAGS
# that jhbuild sets while still retaining build/autotools as the authoritative
# source for m4 macros
ACLOCAL_AMFLAGS = -I build/autotools ${ACLOCAL_FLAGS}

SUBDIRS = \
	build \
	$(NULL)

bin_PROGRAMS = \
	multi-context-test \
	$(NULL)

AM_CPPFLAGS = \
	$(EPOXY_CFLAGS) \
	$(GL_CFLAGS) \
	$(X11_CFLAGS) \
	$(MULTI_CONTEXT_TEST_EXTRA_CFLAGS) \
	$(NULL)

multi_context_test_SOURCES = \
	multi-context-test.c \
	shader-data.c \
	shader-data.h \
	$(NULL)

multi_context_test_LDADD = \
	$(EPOXY_LIBS) \
	$(GL_LIBS) \
	$(X11_LIBS) \
	$(MULTI_CONTEXT_TEST_EXTRA_LIBS) \
	$(LIBM) \
	$(NULL)

EXTRA_DIST = \
	autogen.sh \
	COPYING \
	$(NULL)
