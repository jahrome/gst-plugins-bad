plugin_LTLIBRARIES = libgstneonhttpsrc.la

libgstneonhttpsrc_la_SOURCES = gstneonhttpsrc.c

libgstneonhttpsrc_la_CFLAGS = $(GST_CFLAGS) $(NEON_CFLAGS) 
libgstneonhttpsrc_la_LIBADD = $(GST_BASE_LIBS) $(NEON_LIBS)
libgstneonhttpsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstneonhttpsrc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstneonhttpsrc.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstneonhttpsrc -:SHARED libgstneonhttpsrc \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstneonhttpsrc_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstneonhttpsrc_la_CFLAGS) \
                  -Iexternal/neon/src \
	 -:LDFLAGS $(libgstneonhttpsrc_la_LDFLAGS) \
	           $(libgstneonhttpsrc_la_LIBADD) \
	           -ldl -lneon \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
