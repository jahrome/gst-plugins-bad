LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_CPP_EXTENSION := .cc
LOCAL_SRC_FILES:= \
        gstpitch.cc \
	gstbpmdetect.cc \
	plugin.c


LOCAL_SHARED_LIBRARIES :=	\
	libgstaudio-0.10	\
	libgstreamer-0.10	\
	libgstbase-0.10		\
	libglib-2.0		\
	libgthread-2.0		\
	libgmodule-2.0		\
	libgobject-2.0		\
	libgstcontroller-0.10		\
	libsoundtouch

LOCAL_MODULE:= libgstsoundtouch

LOCAL_C_INCLUDES := 			\
	$(LOCAL_PATH)			\
	$(GST_PLUGINS_BASE_TOP)		\
	external/gst-plugins-base/gst-libs\
	$(GST_PLUGINS_BASE_TOP)/gst-libs/gst/audio/android\
	$(GST_PLUGINS_BAD_TOP)		\
	$(GST_PLUGINS_BAD_TOP)/android	\
	external/gstreamer		\
	external/gstreamer/android 	\
	external/gstreamer/libs		\
	external/gstreamer/gst		\
	external/gstreamer/gst/android	\
	external/glib			\
	external/glib/glib		\
	external/glib/android	  	\
	external/glib/gmodule	  	\
	external/glib/gobject	  	\
	external/soundtouch/include	\
	external/glib/gthread

LOCAL_CFLAGS := \
	-DHAVE_CONFIG_H

include $(BUILD_PLUGIN_LIBRARY)
