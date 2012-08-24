/* desktop session recorder
 * Copyright (C) 2009 Benjamin Otte <otte@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "byzanzlayercursor.h"

#include <gdk/gdkx.h>
#include <stdint.h>

G_DEFINE_TYPE (ByzanzLayerCursor, byzanz_layer_cursor, BYZANZ_TYPE_LAYER)

static void
byzanz_layer_cursor_read_cursor (ByzanzLayerCursor *clayer)
{
  Display *dpy = GDK_DISPLAY_XDISPLAY (gdk_window_get_display (BYZANZ_LAYER (clayer)->recorder->window));

  clayer->cursor_next = XFixesGetCursorImage (dpy);
  if (clayer->cursor_next)
    g_hash_table_insert (clayer->cursors, clayer->cursor_next, clayer->cursor_next);
}

static gboolean
byzanz_layer_cursor_event (ByzanzLayer * layer,
                           GdkXEvent *   gdkxevent)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (layer);
  XFixesCursorNotifyEvent *event = gdkxevent;

  if (event->type == layer->recorder->fixes_event_base + XFixesCursorNotify) {
    XFixesCursorImage hack;

    hack.cursor_serial = event->cursor_serial;
    clayer->cursor_next = g_hash_table_lookup (clayer->cursors, &hack);
    if (clayer->cursor_next == NULL)
      byzanz_layer_cursor_read_cursor (clayer);
    if (clayer->cursor_next != clayer->cursor)
      byzanz_layer_invalidate (layer);
    return TRUE;
  }

  return FALSE;
}

static gboolean
byzanz_layer_cursor_poll (gpointer data)
{
  ByzanzLayerCursor *clayer = data;
  int x, y;
  GdkDevice *device;
  GdkDeviceManager *device_manager;
  GdkDisplay *display;
  GdkWindow *window;

  window = BYZANZ_LAYER (clayer)->recorder->window;
  display = gdk_window_get_display (window);
  device_manager = gdk_display_get_device_manager (display);
  device = gdk_device_manager_get_client_pointer (device_manager);
  gdk_window_get_device_position (window, device, &x, &y, NULL);
  if (x == clayer->cursor_x &&
      y == clayer->cursor_y)
    return TRUE;

  clayer->poll_source = 0;
  byzanz_layer_invalidate (BYZANZ_LAYER (clayer));
  return FALSE;
}

static void
byzanz_layer_cursor_setup_poll (ByzanzLayerCursor *clayer)
{
  if (clayer->poll_source != 0)
    return;

  /* FIXME: Is 10ms ok or is it too much? */
  clayer->poll_source = g_timeout_add (10, byzanz_layer_cursor_poll, clayer);
}

static void
byzanz_recorder_invalidate_cursor (cairo_region_t *region, XFixesCursorImage *cursor, int x, int y)
{
  cairo_rectangle_int_t cursor_rect;

  if (cursor == NULL)
    return;

  cursor_rect.x = x - cursor->xhot;
  cursor_rect.y = y - cursor->yhot;
  cursor_rect.width = cursor->width;
  cursor_rect.height = cursor->height;

  cairo_region_union_rectangle (region, &cursor_rect);
}

static cairo_region_t *
byzanz_layer_cursor_snapshot (ByzanzLayer *layer)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (layer);
  cairo_region_t *region, *area;
  int x, y;
  GdkDevice *device;
  GdkDeviceManager *device_manager;
  GdkDisplay *display;
  GdkWindow *window;

  window = layer->recorder->window;
  display = gdk_window_get_display (window);
  device_manager = gdk_display_get_device_manager (display);
  device = gdk_device_manager_get_client_pointer (device_manager);
  gdk_window_get_device_position (window, device, &x, &y, NULL);
  if (x == clayer->cursor_x &&
      y == clayer->cursor_y &&
      clayer->cursor_next == clayer->cursor)
    return NULL;

  region = cairo_region_create ();
  byzanz_recorder_invalidate_cursor (region, clayer->cursor, clayer->cursor_x, clayer->cursor_y);
  byzanz_recorder_invalidate_cursor (region, clayer->cursor_next, x, y);
  area = cairo_region_create_rectangle (&layer->recorder->area);
  cairo_region_intersect (region, area);
  cairo_region_destroy (area);

  clayer->cursor = clayer->cursor_next;
  clayer->cursor_x = x;
  clayer->cursor_y = y;
  byzanz_layer_cursor_setup_poll (clayer);

  return region;
}

static uint32_t*
uint32_pixels_from_cursor(XFixesCursorImage *cursor)
{
  int i;
  uint32_t *pixels = g_malloc(cursor->width * cursor->height * sizeof(uint32_t));

  for (i = 0; i < (cursor->height * cursor->width); i++) {
    pixels[i] = (uint32_t)cursor->pixels[i];
  }

  return pixels;
}

static void
byzanz_layer_cursor_render (ByzanzLayer *layer,
                            cairo_t *    cr)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (layer);
  XFixesCursorImage *cursor = clayer->cursor;
  cairo_surface_t *cursor_surface;
  uint32_t *pixels;

  if (clayer->cursor == NULL)
    return;

  pixels = uint32_pixels_from_cursor(cursor);
  if (pixels == NULL)
    return;

  cursor_surface = cairo_image_surface_create_for_data ((guchar *) pixels,
      CAIRO_FORMAT_ARGB32, cursor->width, cursor->height, cursor->width * 4);
  
  cairo_save (cr);
  cairo_translate (cr, clayer->cursor_x, clayer->cursor_y);
  cairo_set_source_surface (cr, cursor_surface, -(double) cursor->xhot, -(double) cursor->yhot);

  cairo_paint (cr);
  cairo_restore (cr);

  cairo_surface_destroy (cursor_surface);
  g_free(pixels);
}

static void
byzanz_layer_cursor_finalize (GObject *object)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (object);
  GdkWindow *window = BYZANZ_LAYER (object)->recorder->window;
  Display *dpy = GDK_DISPLAY_XDISPLAY (gdk_window_get_display (window));

  XFixesSelectCursorInput (dpy, GDK_WINDOW_XID (window), 0);

  g_hash_table_destroy (clayer->cursors);

  if (clayer->poll_source != 0) {
    g_source_remove (clayer->poll_source);
  }

  G_OBJECT_CLASS (byzanz_layer_cursor_parent_class)->finalize (object);
}

static void
byzanz_layer_cursor_constructed (GObject *object)
{
  ByzanzLayerCursor *clayer = BYZANZ_LAYER_CURSOR (object);
  GdkWindow *window = BYZANZ_LAYER (object)->recorder->window;
  Display *dpy = GDK_DISPLAY_XDISPLAY (gdk_window_get_display (window));

  XFixesSelectCursorInput (dpy, GDK_WINDOW_XID (window), XFixesDisplayCursorNotifyMask);
  byzanz_layer_cursor_read_cursor (clayer);
  byzanz_layer_cursor_setup_poll (clayer);

  G_OBJECT_CLASS (byzanz_layer_cursor_parent_class)->constructed (object);
}

static void
byzanz_layer_cursor_class_init (ByzanzLayerCursorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ByzanzLayerClass *layer_class = BYZANZ_LAYER_CLASS (klass);

  object_class->constructed = byzanz_layer_cursor_constructed;
  object_class->finalize = byzanz_layer_cursor_finalize;

  layer_class->event = byzanz_layer_cursor_event;
  layer_class->snapshot = byzanz_layer_cursor_snapshot;
  layer_class->render = byzanz_layer_cursor_render;
}

static guint
byzanz_cursor_hash (gconstpointer key)
{
  return (guint) ((const XFixesCursorImage *) key)->cursor_serial;
}

static gboolean
byzanz_cursor_equal (gconstpointer c1, gconstpointer c2)
{
  return ((const XFixesCursorImage *) c1)->cursor_serial == 
    ((const XFixesCursorImage *) c2)->cursor_serial;
}

static void
byzanz_layer_cursor_init (ByzanzLayerCursor *clayer)
{
  clayer->cursors = g_hash_table_new_full (byzanz_cursor_hash,
      byzanz_cursor_equal, NULL, (GDestroyNotify) XFree);
}

