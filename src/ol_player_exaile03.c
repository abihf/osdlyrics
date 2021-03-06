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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ol_player_exaile03.h"
#include "ol_player.h"
#include "ol_utils.h"
#include "ol_utils_dbus.h"
#include "ol_elapse_emulator.h"
#include "ol_debug.h"

static const char service[] = "org.exaile.Exaile";
static const char path[] = "/org/exaile/Exaile";
static const char interface[] = "org.exaile.Exaile";
static const char play[] = "Play";
static const char play_pause[] = "PlayPause";
static const char stop[] = "Stop";
static const char next[] = "Next";
static const char previous[] = "Prev";
static const char get_title[] = "title";
static const char get_artist[] = "artist";
static const char get_album[] = "album";
static const char get_uri[] = "__loc";
/* static const char get_cover_path[] = "get_cover_path"; */
static const char get_state[] = "GetState";
static const char duration[] = "__length";
static const char current_position[] = "CurrentPosition";
static const char get_track_attr[] = "GetTrackAttr";
static const char set_track_attr[] = "SetTrackAttr";
static const char change_volume[] = "ChangeVolume";
static const char query[] = "Query";
static const char *icon_paths[] = {
  "/usr/share/pixmaps/exaile.png",
  "/usr/local/share/pixmaps/exaile.png",
};

static DBusGConnection *connection = NULL;
static DBusGProxy *proxy = NULL;
static GError *error = NULL;
static OlElapseEmulator *elapse_emulator = NULL;

static gboolean ol_player_exaile03_get_music_info (OlMusicInfo *info);
static gboolean ol_player_exaile03_get_played_time (int *played_time);
static gboolean ol_player_exaile03_get_music_length (int *len);
static gboolean ol_player_exaile03_get_activated ();
static gboolean ol_player_exaile03_init_dbus ();
static enum OlPlayerStatus ol_player_exaile03_get_status ();
static int ol_player_exaile03_get_capacity ();
static gboolean ol_player_exaile03_play ();
static gboolean ol_player_exaile03_pause ();
static gboolean ol_player_exaile03_stop ();
static gboolean ol_player_exaile03_prev ();
static gboolean ol_player_exaile03_next ();
static enum OlPlayerStatus ol_player_exaile03_parse_status (const char *status);
static const char *_get_icon_path ();

static enum OlPlayerStatus
ol_player_exaile03_parse_status (const char *status)
{
  if (strcmp (status, "playing") == 0)
    return OL_PLAYER_PLAYING;
  else if (strcmp (status, "paused") == 0)
    return OL_PLAYER_PAUSED;
  else if (strcmp (status, "stopped") == 0)
    return OL_PLAYER_STOPPED;
  return OL_PLAYER_UNKNOWN;
}

static enum OlPlayerStatus
ol_player_exaile03_get_status ()
{
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
      return OL_PLAYER_ERROR;
  char *buf = NULL;
  enum OlPlayerStatus ret = OL_PLAYER_UNKNOWN;
  if (ol_dbus_get_string (proxy, get_state, &buf)) /* Exaile 0.3.1 or later*/
  {
    ret = ol_player_exaile03_parse_status (buf);
    g_free (buf);
  }
  else                          /* Exaile 0.3.0*/
  {
    ol_dbus_get_string (proxy, query, &buf);
    char status[30];
    if (buf != NULL)
    {
      if (strcmp (buf, "Not playing.") == 0)
        ret = OL_PLAYER_STOPPED;
      else if (strstr (buf, "status:") != NULL)
      {
        sscanf (buf, "status: %[^,],", status);
        ret = ol_player_exaile03_parse_status (status);
      }
      g_free (buf);
    }
  }
  return ret;
}

static gboolean
ol_player_exaile03_get_music_info (OlMusicInfo *info)
{
  /* ol_log_func (); */
  ol_assert_ret (info != NULL, FALSE);
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
    {
      ol_debug ("Initialize dbus proxy failed\n");
      return FALSE;
    }
  enum OlPlayerStatus status = ol_player_exaile03_get_status ();
  ol_debugf ("  status: %d\n", (int)status);
  if (status == OL_PLAYER_PLAYING || status == OL_PLAYER_PAUSED)
  {
    ol_music_info_clear (info);
    /* gets the title of current music */
    if (!ol_dbus_get_string_with_str_arg (proxy,
                                          get_track_attr,
                                          get_title,
                                          &info->title))
    {
      ol_error ("  Get title failed");
    }
    /* gets the artist of current music */
    if (!ol_dbus_get_string_with_str_arg (proxy,
                                          get_track_attr,
                                          get_artist,
                                          &info->artist))
    {
      ol_error ("  Get artist failed");
    }
    /* gets the album of current music */
    if (!ol_dbus_get_string_with_str_arg (proxy,
                                          get_track_attr,
                                          get_album,
                                          &info->album))
    {
      ol_error ("  Get album failed");
    }
    /* gets the location of the file */
    if (!ol_dbus_get_string_with_str_arg (proxy,
                                          get_track_attr,
                                          get_uri,
                                          &info->uri))
    {
      ol_error ("  Get track number failed");
    }
    /* ol_debugf ("%s\n" */
    /*            "  title:%s\n" */
    /*            "  artist:%s\n" */
    /*            "  album:%s\n" */
    /*            "  uri:%s\n", */
    /*            __FUNCTION__, */
    /*            info->title, */
    /*            info->artist, */
    /*            info->album, */
    /*            info->uri); */
    return TRUE;
  }
  else if (status == OL_PLAYER_STOPPED)
  {
    return TRUE;
  }
  else
  {
    ol_errorf ("  unknown status\n");
    return FALSE;
  }
}

static gboolean
ol_player_exaile03_get_played_time (int *played_time)
{
  /* ol_log_func (); */
  char *posstr = NULL;
  int minute, second;
  int exaile03_time;
  if (played_time == NULL)
    return FALSE;
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
      return FALSE;
  enum OlPlayerStatus status = ol_player_exaile03_get_status ();
  if (status == OL_PLAYER_PLAYING || status == OL_PLAYER_PAUSED)
  {
    if (!ol_dbus_get_string (proxy, current_position, &posstr))
      return FALSE;
    if (posstr == NULL)
      return FALSE;
    sscanf (posstr, "%d:%d", &minute, &second);
    g_free (posstr);
    exaile03_time = (minute * 60 + second) * 1000;
    if (elapse_emulator == NULL)
    {
      elapse_emulator = g_new (OlElapseEmulator, 1);
      if (elapse_emulator != NULL)
        ol_elapse_emulator_init (elapse_emulator, exaile03_time, 1000);
    }
    if (elapse_emulator != NULL)
    {
      if (status == OL_PLAYER_PLAYING)
        *played_time = ol_elapse_emulator_get_real_ms (elapse_emulator, exaile03_time);
      else /* if (status == OL_PLAYER_PAUSED)  */
        *played_time = ol_elapse_emulator_get_last_ms (elapse_emulator, exaile03_time);
    }
    else
      *played_time = exaile03_time;
  }
  else
  {
    *played_time = -1;
  }
  return TRUE;
}

static gboolean
ol_player_exaile03_get_music_length (int *len)
{
  /* ol_log_func (); */
  if (len == NULL)
    return FALSE;
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
      return FALSE;
  gchar *buf = NULL;
  enum OlPlayerStatus status = ol_player_exaile03_get_status ();
  if (status == OL_PLAYER_PLAYING || status == OL_PLAYER_PAUSED)
  {
    if (!ol_dbus_get_string_with_str_arg (proxy, get_track_attr, duration, &buf))
    {
      return FALSE;
    }
    int sec, usec;
    if (sscanf (buf, "%d.%d", &sec, &usec) == 2)
    {
      *len = sec * 1000 + usec / 1000000;
    }
    g_free (buf);
    return TRUE;
  }
  else if (status == OL_PLAYER_STOPPED)
  {
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

static gboolean
ol_player_exaile03_get_activated ()
{
  /* ol_log_func (); */
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
      return FALSE;
  gchar *buf = NULL;
  if (ol_dbus_get_string (proxy, query, &buf))
  {
    return TRUE;
  }
  else
  {
    ol_debugf ("exaile 0.3  get activated failed\n");
    return FALSE;
  }
}

static gboolean
ol_player_exaile03_init_dbus ()
{
  /* ol_log_func (); */
  if (connection == NULL)
  {
    connection = dbus_g_bus_get (DBUS_BUS_SESSION,
                               &error);
    if (connection == NULL)
    {
      ol_debugf ("get connection failed: %s\n", error->message);
      g_error_free(error);
      error = NULL;
      return FALSE;
    }
    if (proxy != NULL)
      g_object_unref (proxy);
    proxy = NULL;
  }
  if (proxy == NULL)
  {
    proxy = dbus_g_proxy_new_for_name_owner (connection, service, path, interface, &error);
    if (proxy == NULL)
    {
      ol_debugf ("get proxy failed: %s\n", error->message);
      g_error_free (error);
      error = NULL;
      return FALSE;
    }
  }
  return TRUE;
}

static int
ol_player_exaile03_get_capacity ()
{
  return OL_PLAYER_STATUS | OL_PLAYER_PLAY | OL_PLAYER_STOP |
    OL_PLAYER_PAUSE | OL_PLAYER_PREV | OL_PLAYER_NEXT;
}

static gboolean
ol_player_exaile03_stop ()
{
  ol_logf (OL_DEBUG, "%s\n", __FUNCTION__);
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
      return FALSE;
  return ol_dbus_invoke (proxy, stop);
}

static gboolean
ol_player_exaile03_play ()
{
  ol_logf (OL_DEBUG, "%s\n", __FUNCTION__);
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
      return FALSE;
  enum OlPlayerStatus status = ol_player_exaile03_get_status ();
  if (status == OL_PLAYER_ERROR)
    return FALSE;
  switch (status)
  {
  case OL_PLAYER_PAUSED:
    return ol_dbus_invoke (proxy, play_pause);
  case OL_PLAYER_PLAYING:
    return TRUE;
  case OL_PLAYER_STOPPED:
  default:
    return ol_dbus_invoke (proxy, play);
  }
}

static gboolean
ol_player_exaile03_pause ()
{
  ol_logf (OL_DEBUG, "%s\n", __FUNCTION__);
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
      return FALSE;
  enum OlPlayerStatus status = ol_player_exaile03_get_status ();
  if (status == OL_PLAYER_ERROR)
    return FALSE;
  if (status == OL_PLAYER_PLAYING)
    return ol_dbus_invoke (proxy, play_pause);
  return TRUE;
}

static gboolean
ol_player_exaile03_prev ()
{
  ol_logf (OL_DEBUG, "%s\n", __FUNCTION__);
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
      return FALSE;
  return ol_dbus_invoke (proxy, previous);
}

static gboolean
ol_player_exaile03_next ()
{
  ol_logf (OL_DEBUG, "%s\n", __FUNCTION__);
  if (connection == NULL || proxy == NULL)
    if (!ol_player_exaile03_init_dbus ())
      return FALSE;
  return ol_dbus_invoke (proxy, next);
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
ol_player_exaile03_get ()
{
  ol_logf (OL_DEBUG, "%s\n",__FUNCTION__);
  struct OlPlayer *controller = ol_player_new ("Exaile 0.3");
  ol_player_set_cmd (controller, "exaile");
  controller->get_music_info = ol_player_exaile03_get_music_info;
  controller->get_activated = ol_player_exaile03_get_activated;
  controller->get_played_time = ol_player_exaile03_get_played_time;
  controller->get_music_length = ol_player_exaile03_get_music_length;
  controller->get_capacity = ol_player_exaile03_get_capacity;
  controller->get_status = ol_player_exaile03_get_status;
  controller->play = ol_player_exaile03_play;
  controller->pause = ol_player_exaile03_pause;
  controller->prev = ol_player_exaile03_prev;
  controller->next = ol_player_exaile03_next;
  controller->seek = NULL;
  controller->stop = ol_player_exaile03_stop;
  controller->get_icon_path = _get_icon_path;
  return controller;
}

