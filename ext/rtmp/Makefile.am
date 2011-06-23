plugin_LTLIBRARIES = libgstrtmp.la

libgstrtmp_la_SOURCES = gstrtmpsrc.c gstrtmpsink.c gstrtmp.c

noinst_HEADERS = gstrtmpsrc.h gstrtmpsink.h
libgstrtmp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(RTMP_CFLAGS)
libgstrtmp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) $(RTMP_LIBS)
libgstrtmp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtmp_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstrtmp -:SHARED libgstrtmp \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrtmp_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrtmp_la_CFLAGS) \
                  -Iexternal/rtmpdump \
	 -:LDFLAGS $(libgstrtmp_la_LDFLAGS) \
	           $(libgstrtmp_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm LOCAL_STATIC_LIBRARIES:=librtmp \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
