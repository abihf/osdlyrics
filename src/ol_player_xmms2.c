/* -*- mode: C; c-basic-offset: 2; indent-tabs-mode: nil; -*- */
/*
 * Copyright (C) 2009-2011  Tiger Soldier <tigersoldier@gmail.com>
 *
 * This file is part of OSD Lyrics.
 * 
 * OSD Lyrics is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OSD Lyrics is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OSD Lyrics.  If not, see <http://www.gnu.org/licenses/>. 
 */
#include "config.h"
#ifdef ENABLE_XMMS2
#include <stdlib.h>
#include <xmmsclient/xmmsclient.h>
#include "ol_player_xmms2.h"
#include "ol_utils.h"
#include "ol_debug.h"

static xmmsc_connection_t *connection = NULL;
static gboolean connected = FALSE;
static const char *icon_paths[] = {
  "/usr/share/pixmaps/xmms2.svg",
  "/usr/local/share/pixmaps/xmms2.svg",
  "/usr/share/pixmaps/lxmusic.png",
  "/usr/local/share/pixmaps/lxmusic.png",
  "/usr/share/icons/abraca.svg",
  "/usr/local/share/icons/abraca.svg",
  "/usr/share/icons/hicolor/48x48/apps/ario.png",
  "/usr/local/share/icons/hicolor/48x48/apps/ario.png",
};
static gboolean ol_player_xmms2_get_music_info (OlMusicInfo *info);
static gboolean ol_player_xmms2_get_played_time (int *played_time);
static gboolean ol_player_xmms2_get_music_length (int *len);
static gboolean ol_player_xmms2_ensure_connection ();
static gboolean ol_player_xmms2_get_activated ();
static enum OlPlayerStatus ol_player_xmms2_get_status ();
static int ol_player_xmms2_get_capacity ();
static gboolean ol_player_xmms2_play ();
static gboolean ol_player_xmms2_pause ();
static gboolean ol_player_xmms2_stop ();
static gboolean ol_player_xmms2_prev ();
static gboolean ol_player_xmms2_next ();
static gboolean ol_player_xmms2_seek (int pos_ms);
static gboolean ol_player_xmms2_init ();
static void disconnect_callback (void *user_data);
static gboolean ol_player_xmms2_connect ();
/** 
 * @brief Play the music relative to the current one
 * 
 * @param rel The relative position, can be negative
 * 
 * @return TRUE if succeeded
 */
static gboolean ol_player_xmms2_go_rel (int rel);
static const char *_get_icon_path (void);


#if XMMS_IPC_PROTOCOL_VERSION < 13 /* Before xmmsv_t introduced */
typedef xmmsc_result_t xmmsv_t;
xmmsv_t *
xmmsc_result_get_value (xmmsc_result_t *result)
{
  return result;
}

int
xmmsv_is_error (xmmsv_t *val)
{
  return xmmsc_result_iserror (val);
}

int
xmmsv_get_int (xmmsv_t *val, int32_t *r)
{
  return xmmsc_result_get_int (val, r);
}

int
xmmsv_get_string (xmmsv_t *val, const char **r)
{
  return xmmsc_result_get_string (val, r);
}

struct TargetInt {
  const char *key;
  int val;
  int succeed;
};

static void
xmmsv_propdict_get_int (const void *key,
                        xmmsc_result_value_type_t type,
                        const void *value,
                        const char *source,
                        void *user_data)
{
  struct TargetInt *target = (struct TargetInt *) user_data;
  if (type != XMMSC_RESULT_VALUE_TYPE_INT32)
  {
    return;
  }
  if (strcmp (key, target->key) == 0)
  {
    target->val = XPOINTER_TO_INT (value);
    target->succeed = 1;
  }
}


static int
xmmsv_dict_get_int (xmmsv_t *dictv, const char *key, int32_t *val)
{
  ol_assert_ret (dictv != NULL, 0);
  ol_assert_ret (key != NULL, 0);
  ol_assert_ret (val != NULL, 0);
  struct TargetInt target;
  target.key = key;
  target.val = 0;
  target.succeed = 0;
  if (!xmmsc_result_propdict_foreach (dictv, xmmsv_propdict_get_int, &target))
    return 0;
  if (target.succeed)
    *val = target.val;
  return target.succeed;
}

struct TargetString {
  const char *key;
  char *val;
  int succeed;
};

static void
xmmsv_propdict_get_string (const void *key,
                           xmmsc_result_value_type_t type,
                           const void *value,
                           const char *source,
                           void *user_data)
{
  struct TargetString *target = (struct TargetString *) user_data;
  if (type != XMMSC_RESULT_VALUE_TYPE_STRING)
  {
    return;
  }
  if (strcmp (key, target->key) == 0)
  {
    target->val = g_strdup (value);
    target->succeed = 1;
  }
}

static int
xmmsv_dict_get_string (xmmsv_t *dictv, const char *key, const char **val)
{
  ol_assert_ret (dictv != NULL, 0);
  ol_assert_ret (key != NULL, 0);
  ol_assert_ret (val != NULL, 0);
  struct TargetString target;
  target.key = key;
  target.val = NULL;
  target.succeed = 0;
  if (!xmmsc_result_propdict_foreach (dictv, xmmsv_propdict_get_string, &target))
    return 0;
  if (target.succeed)
    *val = target.val;
  return target.succeed;
}

static xmmsv_t *
xmmsv_propdict_to_dict (xmmsv_t *propdict, const char **src_prefs)
{
  xmmsv_ref (propdict);
  return propdict;
}

#else  /* We have xmmsv_t, so things are easy */
static int
xmmsv_dict_get_string (xmmsv_t *dict,
                       const char *key,
                       const char **val)
{
  xmmsv_t *dict_entry;
  return (xmmsv_dict_get (dict, key, &dict_entry) &&
          xmmsv_get_string (dict_entry, val));
}

static int
xmmsv_dict_get_int (xmmsv_t *dict,
                    const char *key,
                    int32_t *val)
{
  xmmsv_t *dict_entry;
  return (xmmsv_dict_get (dict, key, &dict_entry) &&
          xmmsv_get_int (dict_entry, val));
}
#endif  /* XMMS_IPC_PROTOCOL_VERSION < 13 */

/** 
 * @brief Gets a string value from a dict
 * 
 * @param dict A dict, you can get it from return_value with xmmsv_propdict_to_dict
 * @param key The key of the value
 * 
 * @return If succeeded, a new string containing the value. If failed, returns a NULL. The returned string should be free with g_free.
 */
static char *ol_player_xmms2_get_dict_string (xmmsv_t *dict, const char *key);
/** 
 * @brief Gets the id of the current playing music
 * 
 * @return Positive if succeeded. 0 if failed or nothing is playing.
 */
static int32_t ol_player_xmms2_get_currend_id ();

static void
disconnect_callback (void *user_data)
{
  ol_log_func ();
  connected = FALSE;
}

static gboolean
ol_player_xmms2_init ()
{
  ol_log_func ();
  ol_debug ("init XMMS2");
  if (connection == NULL)
  {
    connection = xmmsc_init ("osdlyrics");
    if (!connection)
    {
      ol_error ("Initialize XMMS2 connection failed.");
      return FALSE;
    }
  }
  ol_debug ("Now connect");
  return ol_player_xmms2_connect ();
}

static gboolean
ol_player_xmms2_connect ()
{
  /* ol_log_func (); */
  if (connection == NULL && connected == FALSE && !ol_player_xmms2_init ())
    return FALSE;
  connected = xmmsc_connect (connection, getenv ("XMMS_PATH"));
  if (connected)
  {
    xmmsc_disconnect_callback_set (connection, disconnect_callback, NULL);
    ol_debugf ("connected");
  }
  return connected;
}

static gboolean
ol_player_xmms2_ensure_connection ()
{
  /* ol_log_func (); */
  if (connection == NULL && !ol_player_xmms2_init ())
    return FALSE;
  gboolean ret = TRUE;
  if (connected)
    xmmsc_io_in_handle (connection);
  if (connected && xmmsc_io_want_out (connection))
    xmmsc_io_out_handle (connection);
  if (!connected)
  {
    ret = ol_player_xmms2_connect ();
  }
  if (!ret)
    ol_debug ("Ensure connection failed!");
  return ret;
}

static gboolean
ol_player_xmms2_get_activated ()
{
  ol_log_func ();
  return ol_player_xmms2_ensure_connection ();
}

static enum OlPlayerStatus
ol_player_xmms2_get_status ()
{
  if (!ol_player_xmms2_ensure_connection ())
    return OL_PLAYER_ERROR;
  int ret;
  xmmsc_result_t *result = xmmsc_playback_status (connection);
  xmmsc_result_wait (result);
  xmmsv_t *return_value = xmmsc_result_get_value (result);
  if (xmmsv_is_error (return_value))
  {
    ret = OL_PLAYER_ERROR;
  }
  else
  {
    int32_t status;
    if (xmmsv_get_int (return_value, &status))
    {
      switch (status)
      {
      case XMMS_PLAYBACK_STATUS_STOP:
        ret = OL_PLAYER_STOPPED;
        break;
      case XMMS_PLAYBACK_STATUS_PLAY:
        ret = OL_PLAYER_PLAYING;
        break;
      case XMMS_PLAYBACK_STATUS_PAUSE:
        ret = OL_PLAYER_PAUSED;
        break;
      default:
        ret = OL_PLAYER_UNKNOWN;
        break;
      }
    }
    else
    {
      ret = OL_PLAYER_ERROR;
    }
  }
  xmmsc_result_unref (result);
  return ret;
}

static int32_t
ol_player_xmms2_get_currend_id ()
{
  /* ol_log_func (); */
  if (!ol_player_xmms2_ensure_connection ())
    return 0;
  int32_t ret = 0;
  xmmsc_result_t *result = xmmsc_playback_current_id (connection);
  xmmsc_result_wait (result);
  xmmsv_t *return_value = xmmsc_result_get_value (result);
  if (xmmsv_is_error (return_value))
  {
    ol_error ("Error on getting current id");
    ret = 0;
  }
  else
  {
    if (!xmmsv_get_int (return_value, &ret))
    {
      ol_error ("Get id from result failed");
      ret = 0;
    }
  }
  xmmsc_result_unref (result);
  return ret;
}

static char *
ol_player_xmms2_get_dict_string (xmmsv_t *dict, const char *key)
{
  /* ol_log_func (); */
  ol_assert_ret (dict != NULL, NULL);
  ol_assert_ret (key != NULL, NULL);
  const char *val = NULL;
  if (xmmsv_dict_get_string (dict, key, &val))
  {
    return g_strdup (val);
  }
  return NULL;
}

static int32_t
ol_player_xmms2_get_dict_int (xmmsv_t *dict, const char *key)
{
  /* ol_log_func (); */
  ol_assert_ret (dict != NULL, 0);
  ol_assert_ret (key != NULL, 0);
  int32_t val = 0;
  if (xmmsv_dict_get_int (dict, key, &val))
  {
    return val;
  }
  return 0;
}

static gboolean
ol_player_xmms2_get_music_info (OlMusicInfo *info)
{
  /* ol_log_func (); */
  ol_assert_ret (info != NULL, FALSE);
  if (!ol_player_xmms2_ensure_connection ())
    return FALSE;
  ol_music_info_clear (info);
  int32_t id = ol_player_xmms2_get_currend_id ();
  if (id > 0)
  {
    xmmsc_result_t *result = xmmsc_medialib_get_info (connection, id);
    xmmsc_result_wait (result);
    xmmsv_t *return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value))
    {
      ol_error ("Get music info from XMMS2 failed.");
    }
    else
    {
      xmmsv_t *dict = xmmsv_propdict_to_dict (return_value, NULL);
      info->title = ol_player_xmms2_get_dict_string (dict, "title");
      info->artist = ol_player_xmms2_get_dict_string (dict, "artist");
      info->album = ol_player_xmms2_get_dict_string (dict, "album");
      info->track_number = ol_player_xmms2_get_dict_int (dict, "tracknr");
      info->uri = ol_player_xmms2_get_dict_string (dict, "url");
      ol_logf (OL_DEBUG,
               "%s\n"
               "  title:%s\n"
               "  artist:%s\n"
               "  album:%s\n"
               "  uri:%s\n",
               __FUNCTION__,
               info->title,
               info->artist,
               info->album,
               info->uri);
      xmmsv_unref (dict);
    }
    xmmsc_result_unref (result);
  }
  return TRUE;
}

static gboolean
ol_player_xmms2_get_played_time (int *played_time)
{
  /* ol_log_func (); */
  ol_assert_ret (played_time != NULL, FALSE);
  if (!ol_player_xmms2_ensure_connection ())
    return FALSE;
  xmmsc_result_t *result = xmmsc_playback_playtime (connection);
  xmmsc_result_wait (result);
  xmmsv_t *return_value = xmmsc_result_get_value (result);
  if (xmmsv_is_error (return_value))
  {
    ol_error ("Get played time from XMMS2 failed");
    xmmsc_result_unref (result);
    return FALSE;
  }
  int32_t elapsed = 0;
  xmmsv_get_int (return_value, &elapsed);
  *played_time = elapsed;
  xmmsc_result_unref (result);
  /* ol_debugf ("time: %d\n", *played_time); */
  return TRUE;
}

static gboolean
ol_player_xmms2_get_music_length (int *len)
{
  /* ol_log_func (); */
  ol_assert_ret (len != NULL, FALSE);
  if (!ol_player_xmms2_ensure_connection ())
    return FALSE;
  *len = 0;
  int32_t id = ol_player_xmms2_get_currend_id ();
  if (id > 0)
  {
    xmmsc_result_t *result = xmmsc_medialib_get_info (connection, id);
    xmmsc_result_wait (result);
    xmmsv_t *return_value = xmmsc_result_get_value (result);
    if (xmmsv_is_error (return_value))
    {
      ol_error ("Get music info from XMMS2 failed.");
    }
    else
    {
      xmmsv_t *dict = xmmsv_propdict_to_dict (return_value, NULL);
      *len = ol_player_xmms2_get_dict_int (return_value, "duration");
      xmmsv_unref (dict);
    }
    xmmsc_result_unref (result);
  }
  return TRUE;
}

static int
ol_player_xmms2_get_capacity ()
{
  return OL_PLAYER_STATUS | OL_PLAYER_PLAY | OL_PLAYER_PAUSE | OL_PLAYER_STOP |
    OL_PLAYER_PREV | OL_PLAYER_NEXT | OL_PLAYER_SEEK;
}

static gboolean
ol_player_xmms2_play ()
{
  ol_log_func ();
  if (!ol_player_xmms2_ensure_connection ())
    return FALSE;
  xmmsc_result_unref (xmmsc_playback_start (connection));
  return TRUE;
}

static gboolean
ol_player_xmms2_pause ()
{
  ol_log_func ();
  if (!ol_player_xmms2_ensure_connection ())
    return FALSE;
  xmmsc_result_unref (xmmsc_playback_pause (connection));
  return TRUE;
}

static gboolean
ol_player_xmms2_stop ()
{
  ol_log_func ();
  if (!ol_player_xmms2_ensure_connection ())
    return FALSE;
  xmmsc_result_unref (xmmsc_playback_stop (connection));
  return TRUE;
}

static gboolean
ol_player_xmms2_go_rel (int rel)
{
  ol_log_func ();
  ol_debugf ("  rel:%d\n", rel);
  if (!ol_player_xmms2_ensure_connection ())
    return FALSE;
  xmmsc_result_t *result = xmmsc_playlist_set_next_rel (connection, rel);
  xmmsc_result_wait (result);
  xmmsv_t *return_value = xmmsc_result_get_value (result);
  if (xmmsv_is_error (return_value))
  {
    ol_debug ("Error on setting playlist next");
    return FALSE;
  }
  xmmsc_result_unref (result);

  result = xmmsc_playback_tickle (connection);
  xmmsc_result_wait (result);
  if (xmmsc_result_iserror (result))
  {
    ol_debug ("Error on tickle playback");
    return FALSE;
  }
  xmmsc_result_unref (result);
  return TRUE;
}

static gboolean
ol_player_xmms2_prev ()
{
  return ol_player_xmms2_go_rel (-1);
}

static gboolean
ol_player_xmms2_next ()
{
  return ol_player_xmms2_go_rel (1);
}
  
static gboolean
ol_player_xmms2_seek (int pos_ms)
{
  ol_log_func ();
  if (!ol_player_xmms2_ensure_connection ())
    return FALSE;
#if XMMS_IPC_PROTOCOL_VERSION >= 16
  xmmsc_result_unref (xmmsc_playback_seek_ms (connection, pos_ms, XMMS_PLAYBACK_SEEK_SET));
#else
  xmmsc_result_unref (xmmsc_playback_seek_ms (connection, pos_ms));
#endif
  return TRUE;
}
  
static const char *
_get_icon_path ()
{
  int i;
  for (i = 0; i < ol_get_array_len (icon_paths); i++)
  {
    if (ol_path_is_file (icon_paths[i]))
      return icon_paths[i];
  }
  return NULL;
}

struct OlPlayer*
ol_player_xmms2_get ()
{
  ol_log_func ();
  struct OlPlayer *controller = ol_player_new ("XMMS2");
  ol_player_set_cmd (controller, "xmms2-launcher");
  controller->get_music_info = ol_player_xmms2_get_music_info;
  controller->get_activated = ol_player_xmms2_get_activated;
  controller->get_played_time = ol_player_xmms2_get_played_time;
  controller->get_music_length = ol_player_xmms2_get_music_length;
  controller->get_status = ol_player_xmms2_get_status;
  controller->get_capacity = ol_player_xmms2_get_capacity;
  controller->play = ol_player_xmms2_play;
  controller->pause = ol_player_xmms2_pause;
  controller->stop = ol_player_xmms2_stop;
  controller->prev = ol_player_xmms2_prev;
  controller->next = ol_player_xmms2_next;
  controller->seek = ol_player_xmms2_seek;
  controller->get_icon_path = _get_icon_path;
  return controller;
}
#endif  /* ENABLE_XMMS2 */
