/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 *
 * Gsthlsdemux.c:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/**
 * SECTION:element-hlsdemux
 *
 * HTTP Live Streaming demuxer element.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch souphttpsrc location=http://devimages.apple.com/iphone/samples/bipbop/gear4/prog_index.m3u8 ! hlsdemux ! decodebin2 ! ffmpegcolorspace ! videoscale ! autovideosink
 * ]|
 * </refsect2>
 *
 * Last reviewed on 2010-10-07
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif


#include <string.h>
#include <gst/base/gsttypefindhelper.h>
#include "gsthlsdemux.h"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-hls"));

static GstStaticPadTemplate fetchertemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_hls_demux_debug);
#define GST_CAT_DEFAULT gst_hls_demux_debug

enum
{
  PROP_0,

  PROP_FRAGMENTS_CACHE,
  PROP_BITRATE_SWITCH_TOLERANCE,
  PROP_LAST
};

static const float update_interval_factor[] = { 1, 0.5, 1.5, 3 };

#define DEFAULT_FRAGMENTS_CACHE 3
#define DEFAULT_FAILED_COUNT 3
#define DEFAULT_BITRATE_SWITCH_TOLERANCE 0.4

/* GObject */
static void gst_hls_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_hls_demux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_hls_demux_dispose (GObject * obj);

/* GstElement */
static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition);

/* GstHLSDemux */
static GstBusSyncReply gst_hls_demux_fetcher_bus_handler (GstBus * bus,
    GstMessage * message, gpointer data);
static GstFlowReturn gst_hls_demux_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_hls_demux_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_hls_demux_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_hls_demux_src_query (GstPad * pad, GstQuery * query);
static GstFlowReturn gst_hls_demux_fetcher_chain (GstPad * pad,
    GstBuffer * buf);
static gboolean gst_hls_demux_fetcher_sink_event (GstPad * pad,
    GstEvent * event);
static void gst_hls_demux_loop (GstHLSDemux * demux);
static void gst_hls_demux_stop (GstHLSDemux * demux);
static void gst_hls_demux_stop_fetcher (GstHLSDemux * demux,
    gboolean cancelled);
static gboolean gst_hls_demux_start_update (GstHLSDemux * demux);
static gboolean gst_hls_demux_cache_fragments (GstHLSDemux * demux);
static gboolean gst_hls_demux_schedule (GstHLSDemux * demux);
static gboolean gst_hls_demux_switch_playlist (GstHLSDemux * demux);
static gboolean gst_hls_demux_get_next_fragment (GstHLSDemux * demux,
    gboolean retry);
static gboolean gst_hls_demux_update_playlist (GstHLSDemux * demux,
    gboolean retry);
static void gst_hls_demux_reset (GstHLSDemux * demux, gboolean dispose);
static gboolean gst_hls_demux_set_location (GstHLSDemux * demux,
    const gchar * uri);
static gchar *gst_hls_src_buf_to_utf8_playlist (gchar * string, guint size);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT (gst_hls_demux_debug, "hlsdemux", 0,
      "hlsdemux element");
}

GST_BOILERPLATE_FULL (GstHLSDemux, gst_hls_demux, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_hls_demux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details_simple (element_class,
      "HLS Demuxer",
      "Demuxer/URIList",
      "HTTP Live Streaming demuxer",
      "Marc-Andre Lureau <marcandre.lureau@gmail.com>\n"
      "Andoni Morales Alastruey <ylatuya@gmail.com>");
}

static void
gst_hls_demux_dispose (GObject * obj)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (obj);

  g_cond_free (demux->fetcher_cond);
  g_mutex_free (demux->fetcher_lock);

  g_cond_free (demux->thread_cond);
  g_mutex_free (demux->thread_lock);

  if (GST_TASK_STATE (demux->task) != GST_TASK_STOPPED) {
    gst_task_stop (demux->task);
    gst_task_join (demux->task);
  }
  gst_object_unref (demux->task);
  g_static_rec_mutex_free (&demux->task_lock);

  gst_object_unref (demux->fetcher_bus);
  gst_object_unref (demux->fetcherpad);

  gst_hls_demux_reset (demux, TRUE);

  gst_object_unref (demux->download);

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_hls_demux_class_init (GstHLSDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_hls_demux_set_property;
  gobject_class->get_property = gst_hls_demux_get_property;
  gobject_class->dispose = gst_hls_demux_dispose;

  g_object_class_install_property (gobject_class, PROP_FRAGMENTS_CACHE,
      g_param_spec_uint ("fragments-cache", "Fragments cache",
          "Number of fragments needed to be cached to start playing",
          2, G_MAXUINT, DEFAULT_FRAGMENTS_CACHE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BITRATE_SWITCH_TOLERANCE,
      g_param_spec_float ("bitrate-switch-tolerance",
          "Bitrate switch tolerance",
          "Tolerance with respect of the fragment duration to switch to "
          "a different bitrate if the client is too slow/fast.",
          0, 1, DEFAULT_BITRATE_SWITCH_TOLERANCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_hls_demux_change_state);
}

static void
gst_hls_demux_init (GstHLSDemux * demux, GstHLSDemuxClass * klass)
{
  /* sink pad */
  demux->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  gst_pad_set_chain_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_chain));
  gst_pad_set_event_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_sink_event));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);

  /* demux pad */
  demux->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");
  gst_pad_set_event_function (demux->srcpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_src_event));
  gst_pad_set_query_function (demux->srcpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_src_query));
  gst_pad_set_element_private (demux->srcpad, demux);
  gst_element_add_pad (GST_ELEMENT (demux), demux->srcpad);

  /* fetcher pad */
  demux->fetcherpad =
      gst_pad_new_from_static_template (&fetchertemplate, "sink");
  gst_pad_set_chain_function (demux->fetcherpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_fetcher_chain));
  gst_pad_set_event_function (demux->fetcherpad,
      GST_DEBUG_FUNCPTR (gst_hls_demux_fetcher_sink_event));
  gst_pad_set_element_private (demux->fetcherpad, demux);
  gst_pad_activate_push (demux->fetcherpad, TRUE);

  /* Properties */
  demux->fragments_cache = DEFAULT_FRAGMENTS_CACHE;
  demux->bitrate_switch_tol = DEFAULT_BITRATE_SWITCH_TOLERANCE;

  demux->download = gst_adapter_new ();
  demux->fetcher_bus = gst_bus_new ();
  gst_bus_set_sync_handler (demux->fetcher_bus,
      gst_hls_demux_fetcher_bus_handler, demux);
  demux->thread_cond = g_cond_new ();
  demux->thread_lock = g_mutex_new ();
  demux->fetcher_cond = g_cond_new ();
  demux->fetcher_lock = g_mutex_new ();
  demux->queue = g_queue_new ();
  g_static_rec_mutex_init (&demux->task_lock);
  demux->task = gst_task_create ((GstTaskFunction) gst_hls_demux_loop, demux);
  gst_task_set_lock (demux->task, &demux->task_lock);
}

static void
gst_hls_demux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (object);

  switch (prop_id) {
    case PROP_FRAGMENTS_CACHE:
      demux->fragments_cache = g_value_get_uint (value);
      break;
    case PROP_BITRATE_SWITCH_TOLERANCE:
      demux->bitrate_switch_tol = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_hls_demux_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (object);

  switch (prop_id) {
    case PROP_FRAGMENTS_CACHE:
      g_value_set_uint (value, demux->fragments_cache);
      break;
    case PROP_BITRATE_SWITCH_TOLERANCE:
      g_value_set_float (value, demux->bitrate_switch_tol);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_hls_demux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstHLSDemux *demux = GST_HLS_DEMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_hls_demux_reset (demux, FALSE);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      demux->cancelled = TRUE;
      g_cond_signal (demux->fetcher_cond);
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
gst_hls_demux_src_event (GstPad * pad, GstEvent * event)
{
  switch (event->type) {
      /* FIXME: ignore seek event for the moment */
    case GST_EVENT_SEEK:
      gst_event_unref (event);
      return FALSE;
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static gboolean
gst_hls_demux_sink_event (GstPad * pad, GstEvent * event)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (gst_pad_get_parent (pad));
  GstQuery *query;
  gboolean ret;
  gchar *uri;


  switch (event->type) {
    case GST_EVENT_EOS:{
      gchar *playlist;

      if (demux->playlist == NULL) {
        GST_WARNING_OBJECT (demux, "Received EOS without a playlist.");
        break;
      }

      GST_DEBUG_OBJECT (demux,
          "Got EOS on the sink pad: main playlist fetched");

      query = gst_query_new_uri ();
      ret = gst_pad_peer_query (demux->sinkpad, query);
      if (ret) {
        gst_query_parse_uri (query, &uri);
        gst_hls_demux_set_location (demux, uri);
        g_free (uri);
      }
      gst_query_unref (query);

      playlist = gst_hls_src_buf_to_utf8_playlist ((gchar *)
          GST_BUFFER_DATA (demux->playlist), GST_BUFFER_SIZE (demux->playlist));
      gst_buffer_unref (demux->playlist);
      if (playlist == NULL) {
        GST_WARNING_OBJECT (demux, "Error validating first playlist.");
      } else if (!gst_m3u8_client_update (demux->client, playlist)) {
        /* In most cases, this will happen if we set a wrong url in the
         * source element and we have received the 404 HTML response instead of
         * the playlist */
        GST_ELEMENT_ERROR (demux, STREAM, DECODE, ("Invalid playlist."), NULL);
        return FALSE;
      }

      if (!ret && gst_m3u8_client_is_live (demux->client)) {
        GST_ELEMENT_ERROR (demux, RESOURCE, NOT_FOUND,
            ("Failed querying the playlist uri, "
                "required for live sources."), NULL);
        return FALSE;
      }

      gst_task_start (demux->task);
      gst_event_unref (event);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_event_default (pad, event);
}

static gboolean
gst_hls_demux_src_query (GstPad * pad, GstQuery * query)
{
  GstHLSDemux *hlsdemux;
  gboolean ret = FALSE;

  if (query == NULL)
    return FALSE;

  hlsdemux = GST_HLS_DEMUX (gst_pad_get_element_private (pad));

  switch (query->type) {
    case GST_QUERY_DURATION:{
      GstClockTime duration;
      GstFormat fmt;

      gst_query_parse_duration (query, &fmt, NULL);
      if (fmt == GST_FORMAT_TIME) {
        duration = gst_m3u8_client_get_duration (hlsdemux->client);
        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0) {
          gst_query_set_duration (query, GST_FORMAT_TIME, duration);
          ret = TRUE;
        }
      }
      break;
    }
    case GST_QUERY_URI:
      if (hlsdemux->client) {
        /* FIXME: Do we answer with the variant playlist, with the current
         * playlist or the the uri of the least downlowaded fragment? */
        gst_query_set_uri (query, hlsdemux->client->current->uri);
        ret = TRUE;
      }
      break;
    case GST_QUERY_SEEKING:{
      GstFormat fmt;
      gint stop = -1;

      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      if (fmt == GST_FORMAT_TIME) {
        GstClockTime duration;

        duration = gst_m3u8_client_get_duration (hlsdemux->client);
        if (GST_CLOCK_TIME_IS_VALID (duration) && duration > 0)
          stop = duration;
      }
      gst_query_set_seeking (query, fmt, FALSE, 0, stop);
      ret = TRUE;
      break;
    }
    default:
      /* Don't fordward queries upstream because of the special nature of this
       * "demuxer", which relies on the upstream element only to be fed with the
       * first playlist */
      break;
  }

  return ret;
}

static gboolean
gst_hls_demux_fetcher_sink_event (GstPad * pad, GstEvent * event)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (gst_pad_get_element_private (pad));

  switch (event->type) {
    case GST_EVENT_EOS:{
      GST_DEBUG_OBJECT (demux, "Got EOS on the fetcher pad");
      /* signal we have fetched the URI */
      if (!demux->cancelled)
        g_cond_signal (demux->fetcher_cond);
    }
    default:
      break;
  }

  gst_event_unref (event);
  return FALSE;
}

static GstFlowReturn
gst_hls_demux_chain (GstPad * pad, GstBuffer * buf)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (gst_pad_get_parent (pad));

  if (demux->playlist == NULL)
    demux->playlist = buf;
  else
    demux->playlist = gst_buffer_join (demux->playlist, buf);

  gst_object_unref (demux);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_hls_demux_fetcher_chain (GstPad * pad, GstBuffer * buf)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (gst_pad_get_element_private (pad));

  /* The source element can be an http source element. In case we get a 404,
   * the html response will be sent downstream and the adapter
   * will not be null, which might make us think that the request proceed
   * successfully. But it will also post an error message in the bus that
   * is handled synchronously and that will set demux->fetcher_error to TRUE,
   * which is used to discard this buffer with the html response. */
  if (demux->fetcher_error) {
    goto done;
  }

  GST_LOG_OBJECT (demux, "The uri fetcher received a new buffer of size %u",
      GST_BUFFER_SIZE (buf));
  gst_adapter_push (demux->download, buf);

done:
  {
    return GST_FLOW_OK;
  }
}

static void
gst_hls_demux_stop_fetcher (GstHLSDemux * demux, gboolean cancelled)
{
  GstPad *pad;

  /* When the fetcher is stopped while it's downloading, we will get an EOS that
   * unblocks the fetcher thread and tries to stop it again from that thread.
   * Here we check if the fetcher as already been stopped before continuing */
  if (demux->fetcher == NULL || demux->stopping_fetcher)
    return;

  GST_DEBUG_OBJECT (demux, "Stopping fetcher.");
  demux->stopping_fetcher = TRUE;
  /* set the element state to NULL */
  gst_element_set_state (demux->fetcher, GST_STATE_NULL);
  gst_element_get_state (demux->fetcher, NULL, NULL, GST_CLOCK_TIME_NONE);
  /* unlink it from the internal pad */
  pad = gst_pad_get_peer (demux->fetcherpad);
  if (pad) {
    gst_pad_unlink (pad, demux->fetcherpad);
    gst_object_unref (pad);
  }
  /* and finally unref it */
  gst_object_unref (demux->fetcher);
  demux->fetcher = NULL;

  /* if we stopped it to cancell a download, free the cached buffer */
  if (cancelled && !gst_adapter_available (demux->download)) {
    gst_adapter_clear (demux->download);
    /* signal the fetcher thread that the download has finished/cancelled */
    g_cond_signal (demux->fetcher_cond);
  }
}

static void
gst_hls_demux_stop (GstHLSDemux * demux)
{
  gst_hls_demux_stop_fetcher (demux, TRUE);
  if (GST_TASK_STATE (demux->task) != GST_TASK_STOPPED)
    gst_task_stop (demux->task);
  g_cond_signal (demux->thread_cond);
}

static void
gst_hls_demux_loop (GstHLSDemux * demux)
{
  GstBuffer *buf;
  GstFlowReturn ret;

  /* Loop for the source pad task. The task is started when we have
   * received the main playlist from the source element. It tries first to
   * cache the first fragments and then it waits until it has more data in the
   * queue. This task is woken up when we push a new fragment to the queue or
   * when we reached the end of the playlist  */

  if (G_UNLIKELY (demux->need_cache)) {
    if (!gst_hls_demux_cache_fragments (demux))
      goto cache_error;

    /* we can start now the updates thread */
    gst_hls_demux_start_update (demux);
    GST_INFO_OBJECT (demux, "First fragments cached successfully");
  }

  if (g_queue_is_empty (demux->queue)) {
    if (demux->end_of_playlist)
      goto end_of_playlist;

    GST_TASK_WAIT (demux->task);
    /* If the queue is still empty check again if it's the end of the
     * playlist in case we reached it after beeing woken up */
    if (g_queue_is_empty (demux->queue) && demux->end_of_playlist)
      goto end_of_playlist;
  }

  buf = g_queue_pop_head (demux->queue);
  ret = gst_pad_push (demux->srcpad, buf);
  if (ret != GST_FLOW_OK)
    goto error;

  return;

end_of_playlist:
  {
    GST_DEBUG_OBJECT (demux, "Reached end of playlist, sending EOS");
    gst_pad_push_event (demux->srcpad, gst_event_new_eos ());
    gst_hls_demux_stop (demux);
    return;
  }

cache_error:
  {
    GST_ELEMENT_ERROR (demux, RESOURCE, NOT_FOUND,
        ("Could not cache the first fragments"), NULL);
    gst_hls_demux_stop (demux);
    return;
  }

error:
  {
    /* FIXME: handle error */
    gst_hls_demux_stop (demux);
    return;
  }
}

static GstBusSyncReply
gst_hls_demux_fetcher_bus_handler (GstBus * bus,
    GstMessage * message, gpointer data)
{
  GstHLSDemux *demux = GST_HLS_DEMUX (data);

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ERROR) {
    demux->fetcher_error = TRUE;
    g_cond_signal (demux->fetcher_cond);
  }

  gst_message_unref (message);
  return GST_BUS_DROP;
}

static gboolean
gst_hls_demux_make_fetcher (GstHLSDemux * demux, const gchar * uri)
{
  GstPad *pad;

  if (!gst_uri_is_valid (uri))
    return FALSE;

  GST_DEBUG_OBJECT (demux, "Creating fetcher for the URI:%s", uri);
  demux->fetcher = gst_element_make_from_uri (GST_URI_SRC, uri, NULL);
  if (!demux->fetcher)
    return FALSE;

  demux->fetcher_error = FALSE;
  demux->stopping_fetcher = FALSE;
  gst_element_set_bus (GST_ELEMENT (demux->fetcher), demux->fetcher_bus);

  g_object_set (G_OBJECT (demux->fetcher), "location", uri, NULL);
  pad = gst_element_get_static_pad (demux->fetcher, "src");
  if (pad) {
    gst_pad_link (pad, demux->fetcherpad);
    gst_object_unref (pad);
  }
  return TRUE;
}

static void
gst_hls_demux_reset (GstHLSDemux * demux, gboolean dispose)
{
  demux->need_cache = TRUE;
  demux->thread_return = FALSE;
  demux->accumulated_delay = 0;
  demux->end_of_playlist = FALSE;
  demux->cancelled = FALSE;

  if (demux->input_caps) {
    gst_caps_unref (demux->input_caps);
    demux->input_caps = NULL;
  }

  if (demux->playlist) {
    gst_buffer_unref (demux->playlist);
    demux->playlist = NULL;
  }

  gst_adapter_clear (demux->download);

  if (demux->client)
    gst_m3u8_client_free (demux->client);

  if (!dispose) {
    demux->client = gst_m3u8_client_new ("");
  }

  while (!g_queue_is_empty (demux->queue)) {
    GstBuffer *buf = g_queue_pop_head (demux->queue);
    gst_buffer_unref (buf);
  }
  g_queue_clear (demux->queue);
}

static gboolean
gst_hls_demux_set_location (GstHLSDemux * demux, const gchar * uri)
{
  if (demux->client)
    gst_m3u8_client_free (demux->client);
  demux->client = gst_m3u8_client_new (uri);
  GST_INFO_OBJECT (demux, "Changed location: %s", uri);
  return TRUE;
}

static gboolean
gst_hls_demux_update_thread (GstHLSDemux * demux)
{
  /* Loop for the updates. It's started when the first fragments are cached and
   * schedules the next update of the playlist (for lives sources) and the next
   * update of fragments. When a new fragment is downloaded, it compares the
   * download time with the next scheduled update to check if we can or should
   * switch to a different bitrate */

  g_mutex_lock (demux->thread_lock);
  while (TRUE) {
    /* block until the next scheduled update or the signal to quit this thread */
    if (g_cond_timed_wait (demux->thread_cond, demux->thread_lock,
            &demux->next_update)) {
      goto quit;
    }

    /* update the playlist for live sources */
    if (gst_m3u8_client_is_live (demux->client)) {
      if (!gst_hls_demux_update_playlist (demux, TRUE)) {
        GST_ERROR_OBJECT (demux, "Could not update the playlist");
        goto quit;
      }
    }

    /* schedule the next update */
    gst_hls_demux_schedule (demux);

    /* if it's a live source and the playlist couldn't be updated, there aren't
     * more fragments in the playlist, so we just wait for the next schedulled
     * update */
    if (gst_m3u8_client_is_live (demux->client) &&
        demux->client->update_failed_count > 0) {
      GST_WARNING_OBJECT (demux,
          "The playlist hasn't been updated, failed count is %d",
          demux->client->update_failed_count);
      continue;
    }

    /* fetch the next fragment */
    if (!gst_hls_demux_get_next_fragment (demux, TRUE)) {
      if (!demux->end_of_playlist && !demux->cancelled)
        GST_ERROR_OBJECT (demux, "Could not fetch the next fragment");
      goto quit;
    }

    /* try to switch to another bitrate if needed */
    gst_hls_demux_switch_playlist (demux);
  }

quit:
  {
    g_mutex_unlock (demux->thread_lock);
    return TRUE;
  }
}

static gboolean
gst_hls_demux_start_update (GstHLSDemux * demux)
{
  GError *error;

  /* creates a new thread for the updates */
  demux->updates_thread = g_thread_create (
      (GThreadFunc) gst_hls_demux_update_thread, demux, TRUE, &error);
  return (error != NULL);
}

static gboolean
gst_hls_demux_cache_fragments (GstHLSDemux * demux)
{
  gint i;

  /* Start parsing the main playlist */
  gst_m3u8_client_set_current (demux->client, demux->client->main);

  if (gst_m3u8_client_is_live (demux->client)) {
    if (!gst_hls_demux_update_playlist (demux, FALSE)) {
      GST_ERROR_OBJECT (demux, "Could not fetch the main playlist %s",
          demux->client->main->uri);
      return FALSE;
    }
  }

  /* If this playlist is a variant playlist, select the first one
   * and update it */
  if (gst_m3u8_client_has_variant_playlist (demux->client)) {
    GstM3U8 *child = demux->client->main->lists->data;
    gst_m3u8_client_set_current (demux->client, child);
    if (!gst_hls_demux_update_playlist (demux, FALSE)) {
      GST_ERROR_OBJECT (demux, "Could not fetch the child playlist %s",
          child->uri);
      return FALSE;
    }
  }

  /* If it's a live source, set the sequence number to the end of the list
   * and substract the 'fragmets_cache' to start from the last fragment*/
  if (gst_m3u8_client_is_live (demux->client)) {
    demux->client->sequence += g_list_length (demux->client->current->files);
    if (demux->client->sequence >= demux->fragments_cache)
      demux->client->sequence -= demux->fragments_cache;
    else
      demux->client->sequence = 0;
  }

  /* Cache the first fragments */
  for (i = 0; i < demux->fragments_cache - 1; i++) {
    if (!gst_hls_demux_get_next_fragment (demux, FALSE)) {
      if (!demux->cancelled)
        GST_ERROR_OBJECT (demux, "Error caching the first fragments");
      return FALSE;
    }
    /* make sure we stop caching fragments if something cancelled it */
    if (demux->cancelled)
      return FALSE;
  }

  g_get_current_time (&demux->next_update);

  demux->need_cache = FALSE;
  return TRUE;
}

static gboolean
gst_hls_demux_fetch_location (GstHLSDemux * demux, const gchar * uri)
{
  GstStateChangeReturn ret;
  gboolean bret = FALSE;

  g_mutex_lock (demux->fetcher_lock);

  if (!gst_hls_demux_make_fetcher (demux, uri)) {
    goto uri_error;
  }

  ret = gst_element_set_state (demux->fetcher, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto state_change_error;

  /* wait until we have fetched the uri */
  GST_DEBUG_OBJECT (demux, "Waiting to fetch the URI");
  g_cond_wait (demux->fetcher_cond, demux->fetcher_lock);

  gst_hls_demux_stop_fetcher (demux, FALSE);

  if (gst_adapter_available (demux->download)) {
    GST_INFO_OBJECT (demux, "URI fetched successfully");
    bret = TRUE;
  }
  goto quit;

uri_error:
  {
    GST_ELEMENT_ERROR (demux, RESOURCE, OPEN_READ,
        ("Could not create an element to fetch the given URI."), ("URI: \"%s\"",
            uri));
    bret = FALSE;
    goto quit;
  }

state_change_error:
  {
    GST_ELEMENT_ERROR (demux, CORE, STATE_CHANGE,
        ("Error changing state of the fetcher element."), NULL);
    bret = FALSE;
    goto quit;
  }

quit:
  {
    g_mutex_unlock (demux->fetcher_lock);
    return bret;
  }
}

static gchar *
gst_hls_src_buf_to_utf8_playlist (gchar * data, guint size)
{
  gchar *playlist;

  if (!g_utf8_validate (data, size, NULL))
    return NULL;

  /* alloc size + 1 to end with a null character */
  playlist = g_malloc0 (size + 1);
  memcpy (playlist, data, size + 1);
  return playlist;
}

static gboolean
gst_hls_demux_update_playlist (GstHLSDemux * demux, gboolean retry)
{
  const guint8 *data;
  gchar *playlist;
  guint avail;

  GST_INFO_OBJECT (demux, "Updating the playlist %s",
      demux->client->current->uri);
  if (!gst_hls_demux_fetch_location (demux, demux->client->current->uri))
    return FALSE;

  avail = gst_adapter_available (demux->download);
  data = gst_adapter_peek (demux->download, avail);
  playlist = gst_hls_src_buf_to_utf8_playlist ((gchar *) data, avail);
  gst_adapter_clear (demux->download);
  if (playlist == NULL) {
    GST_WARNING_OBJECT (demux, "Couldn't not validate playlist encoding");
    return FALSE;
  }
  gst_m3u8_client_update (demux->client, playlist);
  return TRUE;
}

static gboolean
gst_hls_demux_change_playlist (GstHLSDemux * demux, gboolean is_fast)
{
  GList *list;
  GstStructure *s;

  if (is_fast)
    list = g_list_next (demux->client->main->lists);
  else
    list = g_list_previous (demux->client->main->lists);

  /* Don't do anything else if the playlist is the same */
  if (!list || list->data == demux->client->current)
    return TRUE;

  demux->client->main->lists = list;

  gst_m3u8_client_set_current (demux->client, demux->client->main->lists->data);
  gst_hls_demux_update_playlist (demux, TRUE);
  GST_INFO_OBJECT (demux, "Client is %s, switching to bitrate %d",
      is_fast ? "fast" : "slow", demux->client->current->bandwidth);

  s = gst_structure_new ("playlist",
      "uri", G_TYPE_STRING, demux->client->current->uri,
      "bitrate", G_TYPE_INT, demux->client->current->bandwidth, NULL);
  gst_element_post_message (GST_ELEMENT_CAST (demux),
      gst_message_new_element (GST_OBJECT_CAST (demux), s));

  return TRUE;
}

static gboolean
gst_hls_demux_schedule (GstHLSDemux * demux)
{
  gfloat update_factor;
  gint count;

  /* As defined in §6.3.4. Reloading the Playlist file:
   * "If the client reloads a Playlist file and finds that it has not
   * changed then it MUST wait for a period of time before retrying.  The
   * minimum delay is a multiple of the target duration.  This multiple is
   * 0.5 for the first attempt, 1.5 for the second, and 3.0 thereafter."
   */
  count = demux->client->update_failed_count;
  if (count < 3)
    update_factor = update_interval_factor[count];
  else
    update_factor = update_interval_factor[3];

  /* schedule the next update using the target duration field of the
   * playlist */
  g_time_val_add (&demux->next_update,
      demux->client->current->targetduration * update_factor * 1000000);
  GST_DEBUG_OBJECT (demux, "Next update scheduled at %s",
      g_time_val_to_iso8601 (&demux->next_update));

  return TRUE;
}

static gboolean
gst_hls_demux_switch_playlist (GstHLSDemux * demux)
{
  GTimeVal now;
  gint64 diff, limit;

  g_get_current_time (&now);
  if (!demux->client->main->lists)
    return TRUE;

  /* compare the time when the fragment was downloaded with the time when it was
   * scheduled */
  diff = (GST_TIMEVAL_TO_TIME (demux->next_update) - GST_TIMEVAL_TO_TIME (now));
  limit = demux->client->current->targetduration * GST_SECOND *
      demux->bitrate_switch_tol;

  /* if we are on time switch to a higher bitrate */
  if (diff > limit) {
    gst_hls_demux_change_playlist (demux, TRUE);
  } else if (diff < 0) {
    /* if the client is too slow wait until it has accumulated a certain delay to
     * switch to a lower bitrate */
    demux->accumulated_delay -= diff;
    if (demux->accumulated_delay >= limit) {
      gst_hls_demux_change_playlist (demux, FALSE);
    } else if (demux->accumulated_delay < 0) {
      demux->accumulated_delay = 0;
    }
  }
  return TRUE;
}

static gboolean
gst_hls_demux_get_next_fragment (GstHLSDemux * demux, gboolean retry)
{
  GstBuffer *buf;
  guint avail;
  const gchar *next_fragment_uri;
  GstClockTime duration;
  gboolean discont;

  if (!gst_m3u8_client_get_next_fragment (demux->client, &discont,
          &next_fragment_uri, &duration)) {
    GST_INFO_OBJECT (demux, "This playlist doesn't contain more fragments");
    demux->end_of_playlist = TRUE;
    GST_TASK_SIGNAL (demux->task);
    return FALSE;
  }

  GST_INFO_OBJECT (demux, "Fetching next fragment %s", next_fragment_uri);

  if (!gst_hls_demux_fetch_location (demux, next_fragment_uri))
    return FALSE;

  avail = gst_adapter_available (demux->download);
  buf = gst_adapter_take_buffer (demux->download, avail);
  GST_BUFFER_DURATION (buf) = duration;

  if (G_UNLIKELY (demux->input_caps == NULL)) {
    demux->input_caps = gst_type_find_helper_for_buffer (NULL, buf, NULL);
    if (demux->input_caps) {
      gst_pad_set_caps (demux->srcpad, demux->input_caps);
      GST_INFO_OBJECT (demux, "Input source caps: %" GST_PTR_FORMAT,
          demux->input_caps);
    }
  }

  if (discont) {
    GST_DEBUG_OBJECT (demux, "Marking fragment as discontinuous");
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
  }

  g_queue_push_tail (demux->queue, buf);
  GST_TASK_SIGNAL (demux->task);
  gst_adapter_clear (demux->download);
  return TRUE;
}
