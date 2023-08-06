/* SPDX-FileCopyrightText: 2005-2007 Blender Foundation
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup wm
 *
 * Cursor pixmap and cursor utility functions to change the cursor.
 */

#include <cstdio>
#include <cstring>

#include "GHOST_C-api.h"

#include "BLI_utildefines.h"

#include "BLI_sys_types.h"

#include "DNA_listBase.h"
#include "DNA_userdef_types.h"
#include "DNA_workspace_types.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_main.h"

#include "WM_api.hh"
#include "WM_types.hh"
#include "wm_cursors.hh"
#include "wm_window.hh"

/* Blender custom cursor. */
struct BCursor {
  char *bitmap;
  char *mask;
  char hotx;
  char hoty;
  bool can_invert_color;
};

static BCursor *BlenderCursor[WM_CURSOR_NUM] = {nullptr};

/* Blender cursor to GHOST standard cursor conversion. */
static GHOST_TStandardCursor convert_to_ghost_standard_cursor(WMCursorType curs)
{
  switch (curs) {
    case WM_CURSOR_DEFAULT:
      return GHOST_kStandardCursorDefault;
    case WM_CURSOR_WAIT:
      return GHOST_kStandardCursorWait;
    case WM_CURSOR_EDIT:
    case WM_CURSOR_CROSS:
      return GHOST_kStandardCursorCrosshair;
    case WM_CURSOR_X_MOVE:
      return GHOST_kStandardCursorLeftRight;
    case WM_CURSOR_Y_MOVE:
      return GHOST_kStandardCursorUpDown;
    case WM_CURSOR_COPY:
      return GHOST_kStandardCursorCopy;
    case WM_CURSOR_HAND:
      return GHOST_kStandardCursorMove;
    case WM_CURSOR_H_SPLIT:
      return GHOST_kStandardCursorHorizontalSplit;
    case WM_CURSOR_V_SPLIT:
      return GHOST_kStandardCursorVerticalSplit;
    case WM_CURSOR_STOP:
      return GHOST_kStandardCursorStop;
    case WM_CURSOR_KNIFE:
      return GHOST_kStandardCursorKnife;
    case WM_CURSOR_NSEW_SCROLL:
      return GHOST_kStandardCursorNSEWScroll;
    case WM_CURSOR_NS_SCROLL:
      return GHOST_kStandardCursorNSScroll;
    case WM_CURSOR_EW_SCROLL:
      return GHOST_kStandardCursorEWScroll;
    case WM_CURSOR_EYEDROPPER:
      return GHOST_kStandardCursorEyedropper;
    case WM_CURSOR_N_ARROW:
      return GHOST_kStandardCursorUpArrow;
    case WM_CURSOR_S_ARROW:
      return GHOST_kStandardCursorDownArrow;
    case WM_CURSOR_PAINT:
      return GHOST_kStandardCursorCrosshairA;
    case WM_CURSOR_DOT:
      return GHOST_kStandardCursorCrosshairB;
    case WM_CURSOR_CROSSC:
      return GHOST_kStandardCursorCrosshairC;
    case WM_CURSOR_ERASER:
      return GHOST_kStandardCursorEraser;
    case WM_CURSOR_ZOOM_IN:
      return GHOST_kStandardCursorZoomIn;
    case WM_CURSOR_ZOOM_OUT:
      return GHOST_kStandardCursorZoomOut;
    case WM_CURSOR_TEXT_EDIT:
      return GHOST_kStandardCursorText;
    case WM_CURSOR_PAINT_BRUSH:
      return GHOST_kStandardCursorPencil;
    case WM_CURSOR_E_ARROW:
      return GHOST_kStandardCursorRightArrow;
    case WM_CURSOR_W_ARROW:
      return GHOST_kStandardCursorLeftArrow;
    default:
      return GHOST_kStandardCursorCustom;
  }
}

static void window_set_custom_cursor(
    wmWindow *win, const uchar mask[16][2], const uchar bitmap[16][2], int hotx, int hoty)
{
  GHOST_SetCustomCursorShape(static_cast<GHOST_WindowHandle>(win->ghostwin),
                             (uint8_t *)bitmap,
                             (uint8_t *)mask,
                             16,
                             16,
                             hotx,
                             hoty,
                             true);
}

static void window_set_custom_cursor_ex(wmWindow *win, BCursor *cursor)
{
  GHOST_SetCustomCursorShape(static_cast<GHOST_WindowHandle>(win->ghostwin),
                             (uint8_t *)cursor->bitmap,
                             (uint8_t *)cursor->mask,
                             16,
                             16,
                             cursor->hotx,
                             cursor->hoty,
                             cursor->can_invert_color);
}

void WM_cursor_set(wmWindow *win, int curs)
{
  if (win == nullptr || G.background) {
    return; /* Can't set custom cursor before Window init */
  }

  if (curs == WM_CURSOR_DEFAULT && win->modalcursor) {
    curs = win->modalcursor;
  }

  if (curs == WM_CURSOR_NONE) {
    GHOST_SetCursorVisibility(static_cast<GHOST_WindowHandle>(win->ghostwin), false);
    return;
  }

  GHOST_SetCursorVisibility(static_cast<GHOST_WindowHandle>(win->ghostwin), true);

  if (win->cursor == curs) {
    return; /* Cursor is already set */
  }

  win->cursor = curs;

  if (curs < 0 || curs >= WM_CURSOR_NUM) {
    BLI_assert_msg(0, "Invalid cursor number");
    return;
  }

  GHOST_TStandardCursor ghost_cursor = convert_to_ghost_standard_cursor(WMCursorType(curs));

  if (ghost_cursor != GHOST_kStandardCursorCustom &&
      GHOST_HasCursorShape(static_cast<GHOST_WindowHandle>(win->ghostwin), ghost_cursor))
  {
    /* Use native GHOST cursor when available. */
    GHOST_SetCursorShape(static_cast<GHOST_WindowHandle>(win->ghostwin), ghost_cursor);
  }
  else {
    BCursor *bcursor = BlenderCursor[curs];
    if (bcursor) {
      /* Use custom bitmap cursor. */
      window_set_custom_cursor_ex(win, bcursor);
    }
    else {
      /* Fallback to default cursor if no bitmap found. */
      GHOST_SetCursorShape(static_cast<GHOST_WindowHandle>(win->ghostwin),
                           GHOST_kStandardCursorDefault);
    }
  }
}

bool WM_cursor_set_from_tool(wmWindow *win, const ScrArea *area, const ARegion *region)
{
  if (region && !ELEM(region->regiontype, RGN_TYPE_WINDOW, RGN_TYPE_PREVIEW)) {
    return false;
  }

  bToolRef_Runtime *tref_rt = (area && area->runtime.tool) ? area->runtime.tool->runtime : nullptr;
  if (tref_rt && tref_rt->cursor != WM_CURSOR_DEFAULT) {
    if (win->modalcursor == 0) {
      WM_cursor_set(win, tref_rt->cursor);
      win->cursor = tref_rt->cursor;
      return true;
    }
  }
  return false;
}

void WM_cursor_modal_set(wmWindow *win, int val)
{
  if (win->lastcursor == 0) {
    win->lastcursor = win->cursor;
  }
  win->modalcursor = val;
  WM_cursor_set(win, val);
}

void WM_cursor_modal_restore(wmWindow *win)
{
  win->modalcursor = 0;
  if (win->lastcursor) {
    WM_cursor_set(win, win->lastcursor);
  }
  win->lastcursor = 0;
}

void WM_cursor_wait(bool val)
{
  if (!G.background) {
    wmWindowManager *wm = static_cast<wmWindowManager *>(G_MAIN->wm.first);
    wmWindow *win = static_cast<wmWindow *>(wm ? wm->windows.first : nullptr);

    for (; win; win = win->next) {
      if (val) {
        WM_cursor_modal_set(win, WM_CURSOR_WAIT);
      }
      else {
        WM_cursor_modal_restore(win);
      }
    }
  }
}

void WM_cursor_grab_enable(wmWindow *win,
                           const eWM_CursorWrapAxis wrap,
                           const rcti *wrap_region,
                           const bool hide)
{
  int _wrap_region_buf[4];
  int *wrap_region_screen = nullptr;

  /* Only grab cursor when not running debug.
   * It helps not to get a stuck WM when hitting a break-point. */
  GHOST_TGrabCursorMode mode = GHOST_kGrabNormal;
  GHOST_TAxisFlag mode_axis = GHOST_TAxisFlag(GHOST_kAxisX | GHOST_kAxisY);

  if (wrap_region) {
    wrap_region_screen = _wrap_region_buf;
    wrap_region_screen[0] = wrap_region->xmin;
    wrap_region_screen[1] = wrap_region->ymax;
    wrap_region_screen[2] = wrap_region->xmax;
    wrap_region_screen[3] = wrap_region->ymin;
    wm_cursor_position_to_ghost_screen_coords(win, &wrap_region_screen[0], &wrap_region_screen[1]);
    wm_cursor_position_to_ghost_screen_coords(win, &wrap_region_screen[2], &wrap_region_screen[3]);
  }

  if (hide) {
    mode = GHOST_kGrabHide;
  }
  else if (wrap != WM_CURSOR_WRAP_NONE) {
    mode = GHOST_kGrabWrap;

    if (wrap == WM_CURSOR_WRAP_X) {
      mode_axis = GHOST_kAxisX;
    }
    else if (wrap == WM_CURSOR_WRAP_Y) {
      mode_axis = GHOST_kAxisY;
    }
  }

  if ((G.debug & G_DEBUG) == 0) {
    if (win->ghostwin) {
      if (win->eventstate->tablet.is_motion_absolute == false) {
        GHOST_SetCursorGrab(static_cast<GHOST_WindowHandle>(win->ghostwin),
                            mode,
                            mode_axis,
                            wrap_region_screen,
                            nullptr);
      }

      win->grabcursor = mode;
    }
  }
}

void WM_cursor_grab_disable(wmWindow *win, const int mouse_ungrab_xy[2])
{
  if ((G.debug & G_DEBUG) == 0) {
    if (win && win->ghostwin) {
      if (mouse_ungrab_xy) {
        int mouse_xy[2] = {mouse_ungrab_xy[0], mouse_ungrab_xy[1]};
        wm_cursor_position_to_ghost_screen_coords(win, &mouse_xy[0], &mouse_xy[1]);
        GHOST_SetCursorGrab(static_cast<GHOST_WindowHandle>(win->ghostwin),
                            GHOST_kGrabDisable,
                            GHOST_kAxisNone,
                            nullptr,
                            mouse_xy);
      }
      else {
        GHOST_SetCursorGrab(static_cast<GHOST_WindowHandle>(win->ghostwin),
                            GHOST_kGrabDisable,
                            GHOST_kAxisNone,
                            nullptr,
                            nullptr);
      }

      win->grabcursor = GHOST_kGrabDisable;
    }
  }
}

static void wm_cursor_warp_relative(wmWindow *win, int x, int y)
{
  /* NOTE: don't use wmEvent coords because of continuous grab #36409. */
  int cx, cy;
  wm_cursor_position_get(win, &cx, &cy);
  WM_cursor_warp(win, cx + x, cy + y);
}

bool wm_cursor_arrow_move(wmWindow *win, const wmEvent *event)
{
  /* TODO: give it a modal keymap? Hard coded for now */

  if (win && event->val == KM_PRESS) {
    /* Must move at least this much to avoid rounding in WM_cursor_warp. */
    float fac = GHOST_GetNativePixelSize(static_cast<GHOST_WindowHandle>(win->ghostwin));

    if (event->type == EVT_UPARROWKEY) {
      wm_cursor_warp_relative(win, 0, fac);
      return true;
    }
    if (event->type == EVT_DOWNARROWKEY) {
      wm_cursor_warp_relative(win, 0, -fac);
      return true;
    }
    if (event->type == EVT_LEFTARROWKEY) {
      wm_cursor_warp_relative(win, -fac, 0);
      return true;
    }
    if (event->type == EVT_RIGHTARROWKEY) {
      wm_cursor_warp_relative(win, fac, 0);
      return true;
    }
  }
  return false;
}

void WM_cursor_time(wmWindow *win, int nr)
{
  /* 10 8x8 digits */
  const char number_bitmaps[10][8] = {
      {0, 56, 68, 68, 68, 68, 68, 56},
      {0, 24, 16, 16, 16, 16, 16, 56},
      {0, 60, 66, 32, 16, 8, 4, 126},
      {0, 124, 32, 16, 56, 64, 66, 60},
      {0, 32, 48, 40, 36, 126, 32, 32},
      {0, 124, 4, 60, 64, 64, 68, 56},
      {0, 56, 4, 4, 60, 68, 68, 56},
      {0, 124, 64, 32, 16, 8, 8, 8},
      {0, 60, 66, 66, 60, 66, 66, 60},
      {0, 56, 68, 68, 120, 64, 68, 56},
  };
  uchar mask[16][2];
  uchar bitmap[16][2] = {{0}};

  if (win->lastcursor == 0) {
    win->lastcursor = win->cursor;
  }

  memset(&mask, 0xFF, sizeof(mask));

  /* print number bottom right justified */
  for (int idx = 3; nr && idx >= 0; idx--) {
    const char *digit = number_bitmaps[nr % 10];
    int x = idx % 2;
    int y = idx / 2;

    for (int i = 0; i < 8; i++) {
      bitmap[i + y * 8][x] = digit[i];
    }
    nr /= 10;
  }

  window_set_custom_cursor(win, mask, bitmap, 7, 7);
  /* Unset current cursor value so it's properly reset to wmWindow.lastcursor. */
  win->cursor = 0;
}

/**
 * Custom Cursor Description
 * =========================
 *
 * Each bit represents a pixel, so 1 byte = 8 pixels,
 * the bytes go Left to Right. Top to bottom
 * the bits in a byte go right to left
 * (ie;  0x01, 0x80  represents a line of 16 pix with the first and last pix set.)
 *
 * - A 0 in the bitmap = white, a 1 black
 * - a 0 in the mask   = transparent pix.
 *
 * This type of cursor is 16x16 pixels only.
 *
 * ----
 *
 * There is a nice Python GUI utility that can be used for drawing cursors in
 * this format in the Blender source distribution, in
 * `./tools/utils/make_cursor_gui.py` .
 *
 * Start it with the command `python3 make_cursor_gui.py`
 * It will copy its output to the console when you press 'Do it'.
 */

/**
 * Because defining a cursor mixes declarations and executable code
 * each cursor needs its own scoping block or it would be split up
 * over several hundred lines of code. To enforce/document this better
 * I define 2 pretty brain-dead macros so it's obvious what the extra "[]"
 * are for */

#define BEGIN_CURSOR_BLOCK \
  { \
    ((void)0)
#define END_CURSOR_BLOCK \
  } \
  ((void)0)

void wm_init_cursor_data()
{
  /********************** NW_ARROW Cursor **************************/
  BEGIN_CURSOR_BLOCK;
  static char nw_bitmap[] = {
      0x00, 0x00, 0x02, 0x00, 0x06, 0x00, 0x0e, 0x00, 0x1e, 0x00, 0x3e,
      0x00, 0x7e, 0x00, 0xfe, 0x00, 0xfe, 0x01, 0xfe, 0x03, 0xfe, 0x07,
      0x7e, 0x00, 0x6e, 0x00, 0xc6, 0x00, 0xc2, 0x00, 0x00, 0x00,
  };

  static char nw_mask[] = {
      0x03, 0x00, 0x07, 0x00, 0x0f, 0x00, 0x1f, 0x00, 0x3f, 0x00, 0x7f,
      0x00, 0xff, 0x00, 0xff, 0x01, 0xff, 0x03, 0xff, 0x07, 0xff, 0x0f,
      0xff, 0x0f, 0xff, 0x00, 0xef, 0x01, 0xe7, 0x01, 0xc3, 0x00,
  };

  static BCursor NWArrowCursor = {
      nw_bitmap,
      nw_mask,
      0,
      0,
      true,
  };

  BlenderCursor[WM_CURSOR_DEFAULT] = &NWArrowCursor;
  BlenderCursor[WM_CURSOR_COPY] = &NWArrowCursor;
  BlenderCursor[WM_CURSOR_NW_ARROW] = &NWArrowCursor;
  END_CURSOR_BLOCK;

  /************************ NS_ARROW Cursor *************************/
  BEGIN_CURSOR_BLOCK;
  static char ns_bitmap[] = {
      0x00, 0x00, 0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0x80,
      0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00,
      0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00, 0x00,
  };

  static char ns_mask[] = {
      0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0xfc,
      0x1f, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xfc, 0x1f,
      0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00,
  };

  static BCursor NSArrowCursor = {
      ns_bitmap,
      ns_mask,
      7,
      7,
      true,
  };

  BlenderCursor[WM_CURSOR_Y_MOVE] = &NSArrowCursor;
  BlenderCursor[WM_CURSOR_NS_ARROW] = &NSArrowCursor;
  END_CURSOR_BLOCK;

  /********************** EW_ARROW Cursor *************************/
  BEGIN_CURSOR_BLOCK;
  static char ew_bitmap[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x08, 0x18,
      0x18, 0x1c, 0x38, 0xfe, 0x7f, 0x1c, 0x38, 0x18, 0x18, 0x10, 0x08,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  static char ew_mask[] = {
      0x00, 0x00, 0x00, 0x00, 0x20, 0x04, 0x30, 0x0c, 0x38, 0x1c, 0x3c,
      0x3c, 0xfe, 0x7f, 0xff, 0xff, 0xfe, 0x7f, 0x3c, 0x3c, 0x38, 0x1c,
      0x30, 0x0c, 0x20, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  static BCursor EWArrowCursor = {
      ew_bitmap,
      ew_mask,
      7,
      7,
      true,
  };

  BlenderCursor[WM_CURSOR_X_MOVE] = &EWArrowCursor;
  BlenderCursor[WM_CURSOR_EW_ARROW] = &EWArrowCursor;
  END_CURSOR_BLOCK;

  /********************** Wait Cursor *****************************/
  BEGIN_CURSOR_BLOCK;
  static char wait_bitmap[] = {
      0x00, 0x00, 0x00, 0x00, 0xf0, 0x07, 0xf0, 0x07, 0xb0, 0x06, 0x60,
      0x03, 0xc0, 0x01, 0x80, 0x00, 0x80, 0x00, 0xc0, 0x01, 0x60, 0x03,
      0x30, 0x06, 0x10, 0x04, 0xf0, 0x07, 0x00, 0x00, 0x00, 0x00,
  };

  static char wait_mask[] = {
      0xfc, 0x1f, 0xfc, 0x1f, 0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f, 0xf0,
      0x07, 0xe0, 0x03, 0xc0, 0x01, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07,
      0xf8, 0x0f, 0xf8, 0x0f, 0xf8, 0x0f, 0xfc, 0x1f, 0xfc, 0x1f,
  };

  static BCursor WaitCursor = {
      wait_bitmap,
      wait_mask,
      7,
      7,
      false,
  };

  BlenderCursor[WM_CURSOR_WAIT] = &WaitCursor;
  END_CURSOR_BLOCK;

  /********************** Mute Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char mute_bitmap[] = {
      0x00, 0x00, 0x22, 0x00, 0x14, 0x00, 0x08, 0x03, 0x14, 0x03, 0x22,
      0x03, 0x00, 0x03, 0x00, 0x03, 0xf8, 0x7c, 0xf8, 0x7c, 0x00, 0x03,
      0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x00,
  };

  static char mute_mask[] = {
      0x63, 0x00, 0x77, 0x00, 0x3e, 0x03, 0x1c, 0x03, 0x3e, 0x03, 0x77,
      0x03, 0x63, 0x03, 0x80, 0x07, 0xfc, 0xfc, 0xfc, 0xfc, 0x80, 0x07,
      0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x03,
  };

  static BCursor MuteCursor = {
      mute_bitmap,
      mute_mask,
      9,
      8,
      true,
  };

  BlenderCursor[WM_CURSOR_MUTE] = &MuteCursor;
  END_CURSOR_BLOCK;

  /****************** Normal Cross Cursor ************************/
  BEGIN_CURSOR_BLOCK;
  static char cross_bitmap[] = {
      0x00, 0x00, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80,
      0x01, 0x00, 0x00, 0x3e, 0x7c, 0x3e, 0x7c, 0x00, 0x00, 0x80, 0x01,
      0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00,
  };

  static char cross_mask[] = {
      0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03, 0xc0,
      0x03, 0x7f, 0xfe, 0x7f, 0xfe, 0x7f, 0xfe, 0xff, 0xff, 0xc0, 0x03,
      0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03,
  };

  static BCursor CrossCursor = {
      cross_bitmap,
      cross_mask,
      7,
      7,
      false,
  };

  BlenderCursor[WM_CURSOR_EDIT] = &CrossCursor;
  BlenderCursor[WM_CURSOR_CROSS] = &CrossCursor;
  END_CURSOR_BLOCK;

  /****************** Painting Cursor ************************/
  BEGIN_CURSOR_BLOCK;
  static char paint_bitmap[] = {
      0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x8f, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00,
  };

  static char paint_mask[] = {
      0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0x00, 0x00, 0x00,
      0x00, 0x8f, 0x78, 0xcf, 0x79, 0x8f, 0x78, 0x00, 0x00, 0x00, 0x00,
      0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0x00, 0x00,
  };

  static BCursor PaintCursor = {
      paint_bitmap,
      paint_mask,
      7,
      7,
      false,
  };

  BlenderCursor[WM_CURSOR_PAINT] = &PaintCursor;
  END_CURSOR_BLOCK;

  /********************** Dot Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char dot_bitmap[] = {
      0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x8f, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00,
  };

  static char dot_mask[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x80, 0x00, 0xc0, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  static BCursor DotCursor = {
      dot_bitmap,
      dot_mask,
      7,
      7,
      false,
  };

  BlenderCursor[WM_CURSOR_DOT] = &DotCursor;
  END_CURSOR_BLOCK;

  /************* Minimal Crosshair Cursor ***************/
  BEGIN_CURSOR_BLOCK;
  static char crossc_bitmap[] = {
      0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
      0x00, 0x80, 0x00, 0x55, 0x55, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00,
      0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
  };

  static char crossc_mask[] = {
      0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80,
      0x00, 0x80, 0x00, 0x7f, 0x7f, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00,
      0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x00, 0x00,
  };

  static BCursor CrossCursorC = {
      crossc_bitmap,
      crossc_mask,
      7,
      7,
      false,
  };

  BlenderCursor[WM_CURSOR_CROSSC] = &CrossCursorC;
  END_CURSOR_BLOCK;

  /********************** Knife Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char knife_bitmap[] = {
      0x00, 0x00, 0x00, 0x40, 0x00, 0x60, 0x00, 0x30, 0x00, 0x18, 0x00,
      0x0c, 0x00, 0x06, 0x00, 0x0f, 0x80, 0x07, 0xc0, 0x03, 0xe0, 0x01,
      0xf0, 0x00, 0x78, 0x00, 0x3c, 0x00, 0x0e, 0x00, 0x00, 0x00,
  };

  static char knife_mask[] = {
      0x00, 0x40, 0x00, 0xe0, 0x00, 0xf0, 0x00, 0x78, 0x00, 0x3c, 0x00,
      0x1e, 0x00, 0x0f, 0x80, 0x1f, 0xc0, 0x0f, 0xe0, 0x07, 0xf0, 0x03,
      0xf8, 0x01, 0xfc, 0x00, 0x7e, 0x00, 0x3f, 0x00, 0x0f, 0x00,
  };

  static BCursor KnifeCursor = {
      knife_bitmap,
      knife_mask,
      0,
      15,
      false,
  };

  BlenderCursor[WM_CURSOR_KNIFE] = &KnifeCursor;
  END_CURSOR_BLOCK;

  /********************** Loop Select Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char vloop_bitmap[] = {
      0x00, 0x00, 0x7e, 0x00, 0x3e, 0x00, 0x1e, 0x00, 0xfe, 0xf0, 0x96,
      0x9f, 0x92, 0x90, 0xf0, 0xf0, 0x20, 0x40, 0x20, 0x40, 0x20, 0x40,
      0x20, 0x40, 0xf0, 0xf0, 0x90, 0x90, 0x90, 0x9f, 0xf0, 0xf0,
  };

  static char vloop_mask[] = {
      0xff, 0x01, 0xff, 0x00, 0x7f, 0x00, 0x3f, 0x00, 0xff, 0xf0, 0xff,
      0xff, 0xf7, 0xff, 0xf3, 0xf0, 0x61, 0x60, 0x60, 0x60, 0x60, 0x60,
      0x60, 0x60, 0xf0, 0xf0, 0xf0, 0xff, 0xf0, 0xff, 0xf0, 0xf0,
  };

  static BCursor VLoopCursor = {
      vloop_bitmap,
      vloop_mask,
      0,
      0,
      false,
  };

  BlenderCursor[WM_CURSOR_VERTEX_LOOP] = &VLoopCursor;
  END_CURSOR_BLOCK;

  /********************** TextEdit Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char textedit_bitmap[] = {
      0x00, 0x00, 0x70, 0x07, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80,
      0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x80, 0x00,
      0x80, 0x00, 0x80, 0x00, 0x80, 0x00, 0x70, 0x07, 0x00, 0x00,
  };

  static char textedit_mask[] = {
      0x70, 0x07, 0xf8, 0x0f, 0xf0, 0x07, 0xc0, 0x01, 0xc0, 0x01, 0xc0,
      0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01, 0xc0, 0x01,
      0xc0, 0x01, 0xc0, 0x01, 0xf0, 0x07, 0xf8, 0x0f, 0x70, 0x07,
  };

  static BCursor TextEditCursor = {
      textedit_bitmap,
      textedit_mask,
      7,
      7,
      false,
  };

  BlenderCursor[WM_CURSOR_TEXT_EDIT] = &TextEditCursor;
  END_CURSOR_BLOCK;

  /********************** Paintbrush Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char paintbrush_bitmap[] = {
      0x00, 0x00, 0x00, 0x30, 0x00, 0x78, 0x00, 0x74, 0x00, 0x2e, 0x00,
      0x1f, 0x80, 0x0f, 0xc0, 0x07, 0xe0, 0x03, 0xf0, 0x01, 0xf8, 0x00,
      0x7c, 0x00, 0x3e, 0x00, 0x1e, 0x00, 0x0e, 0x00, 0x00, 0x00,
  };

  static char paintbrush_mask[] = {
      0x00, 0x30, 0x00, 0x78, 0x00, 0xfc, 0x00, 0xfe, 0x00, 0x7f, 0x80,
      0x3f, 0xc0, 0x1f, 0xe0, 0x0f, 0xf0, 0x07, 0xf8, 0x03, 0xfc, 0x01,
      0xfe, 0x00, 0x7f, 0x00, 0x3f, 0x00, 0x1f, 0x00, 0x0f, 0x00,
  };

  static BCursor PaintBrushCursor = {
      paintbrush_bitmap,
      paintbrush_mask,
      0,
      15,
      false,
  };

  BlenderCursor[WM_CURSOR_PAINT_BRUSH] = &PaintBrushCursor;
  END_CURSOR_BLOCK;

  /********************** Eraser Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char eraser_bitmap[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
      0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0xfc, 0x07,
      0xfe, 0x03, 0xfe, 0x01, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  static char eraser_mask[] = {
      0x00, 0x00, 0x00, 0x04, 0x00, 0x0e, 0x00, 0x1f, 0x80, 0x3f, 0xc0,
      0x7f, 0xe0, 0xff, 0xf0, 0x7f, 0xf8, 0x3f, 0xfc, 0x1f, 0xfe, 0x0f,
      0xff, 0x07, 0xff, 0x03, 0xff, 0x01, 0xff, 0x00, 0x00, 0x00,
  };

  static BCursor EraserCursor = {
      eraser_bitmap,
      eraser_mask,
      0,
      14,
      false,
  };

  BlenderCursor[WM_CURSOR_ERASER] = &EraserCursor;
  END_CURSOR_BLOCK;

  /********************** Hand Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char hand_bitmap[] = {
      0x00, 0x00, 0x80, 0x01, 0x80, 0x0d, 0x98, 0x6d, 0xb8, 0x6d, 0xb0,
      0x6d, 0xb0, 0x6d, 0xe0, 0x6f, 0xe6, 0x7f, 0xee, 0x7f, 0x7c, 0x35,
      0x78, 0x35, 0x70, 0x15, 0x60, 0x15, 0xc0, 0x1f, 0xc0, 0x1f,
  };

  static char hand_mask[] = {
      0x80, 0x01, 0xc0, 0x0f, 0xd8, 0x7f, 0xfc, 0xff, 0xfc, 0xff, 0xf8,
      0xff, 0xf8, 0xff, 0xf6, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f,
      0xfc, 0x7f, 0xf8, 0x3f, 0xf0, 0x3f, 0xe0, 0x3f, 0xe0, 0x3f,
  };

  static BCursor HandCursor = {
      hand_bitmap,
      hand_mask,
      8,
      8,
      false,
  };

  BlenderCursor[WM_CURSOR_HAND] = &HandCursor;
  END_CURSOR_BLOCK;

  /********************** NSEW Scroll Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char nsewscroll_bitmap[] = {
      0x00, 0x00, 0x80, 0x01, 0xc0, 0x03, 0x40, 0x02, 0x00, 0x00, 0x00,
      0x00, 0x0c, 0x30, 0x06, 0x60, 0x06, 0x60, 0x0c, 0x30, 0x00, 0x00,
      0x00, 0x00, 0x40, 0x02, 0xc0, 0x03, 0x80, 0x01, 0x00, 0x00,
  };

  static char nsewscroll_mask[] = {
      0x80, 0x01, 0xc0, 0x03, 0xe0, 0x07, 0xe0, 0x07, 0x40, 0x02, 0x0c,
      0x30, 0x1e, 0x78, 0x0f, 0xf0, 0x0f, 0xf8, 0x1e, 0x78, 0x0c, 0x30,
      0x40, 0x02, 0xe0, 0x07, 0xe0, 0x07, 0xc0, 0x03, 0x80, 0x01,
  };

  static BCursor NSEWScrollCursor = {
      nsewscroll_bitmap,
      nsewscroll_mask,
      7,
      7,
      true,
  };

  BlenderCursor[WM_CURSOR_NSEW_SCROLL] = &NSEWScrollCursor;
  END_CURSOR_BLOCK;

  /********************** NS Scroll Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char nsscroll_bitmap[] = {
      0x00, 0x00, 0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0x70, 0x07, 0x20,
      0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x02,
      0x70, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00, 0x00,
  };

  static char nsscroll_mask[] = {
      0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0x70,
      0x07, 0x20, 0x02, 0x00, 0x00, 0x00, 0x00, 0x20, 0x02, 0x70, 0x07,
      0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00,
  };

  static BCursor NSScrollCursor = {
      nsscroll_bitmap,
      nsscroll_mask,
      7,
      7,
      true,
  };

  BlenderCursor[WM_CURSOR_NS_SCROLL] = &NSScrollCursor;
  END_CURSOR_BLOCK;

  /********************** EW Scroll Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char ewscroll_bitmap[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x08, 0x38,
      0x1c, 0x1c, 0x38, 0x0e, 0x70, 0x1c, 0x38, 0x38, 0x1c, 0x10, 0x08,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  static char ewscroll_mask[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x08, 0x38, 0x1c, 0x7c,
      0x3e, 0x3e, 0x7c, 0x1f, 0xf8, 0x3e, 0x7c, 0x7c, 0x3e, 0x38, 0x1c,
      0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  static BCursor EWScrollCursor = {
      ewscroll_bitmap,
      ewscroll_mask,
      7,
      7,
      true,
  };

  BlenderCursor[WM_CURSOR_EW_SCROLL] = &EWScrollCursor;
  END_CURSOR_BLOCK;

  /********************** Eyedropper Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char eyedropper_bitmap[] = {
      0x00, 0x00, 0x00, 0x60, 0x00, 0x70, 0x00, 0x3a, 0x00, 0x17, 0x00,
      0x0e, 0x00, 0x1d, 0x80, 0x0b, 0xc0, 0x01, 0xe0, 0x00, 0x70, 0x00,
      0x38, 0x00, 0x1c, 0x00, 0x0c, 0x00, 0x02, 0x00, 0x00, 0x00,
  };

  static char eyedropper_mask[] = {
      0x00, 0x60, 0x00, 0xf0, 0x00, 0xfa, 0x00, 0x7f, 0x80, 0x3f, 0x00,
      0x1f, 0x80, 0x3f, 0xc0, 0x1f, 0xe0, 0x0b, 0xf0, 0x01, 0xf8, 0x00,
      0x7c, 0x00, 0x3e, 0x00, 0x1e, 0x00, 0x0f, 0x00, 0x03, 0x00,
  };

  static BCursor EyedropperCursor = {
      eyedropper_bitmap,
      eyedropper_mask,
      0,
      15,
      false,
  };

  BlenderCursor[WM_CURSOR_EYEDROPPER] = &EyedropperCursor;
  END_CURSOR_BLOCK;

  /********************** Swap Area Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char swap_bitmap[] = {
      0xc0, 0xff, 0x40, 0x80, 0x40, 0xbc, 0x40, 0xb8, 0x40, 0xb8, 0x40,
      0xa4, 0x00, 0x82, 0xfe, 0x81, 0x7e, 0x81, 0xbe, 0xfd, 0xda, 0x01,
      0xe2, 0x01, 0xe2, 0x01, 0xc2, 0x01, 0xfe, 0x01, 0x00, 0x00,
  };

  static char swap_mask[] = {
      0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0, 0xff, 0xc0,
      0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x03,
      0xff, 0x03, 0xff, 0x03, 0xff, 0x03, 0xff, 0x03, 0xff, 0x03,
  };

  static BCursor SwapCursor = {
      swap_bitmap,
      swap_mask,
      7,
      7,
      false,
  };

  BlenderCursor[WM_CURSOR_SWAP_AREA] = &SwapCursor;
  END_CURSOR_BLOCK;

  /********************** Vertical Split Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char vsplit_bitmap[] = {
      0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x88,
      0x11, 0x8c, 0x31, 0x86, 0x61, 0x86, 0x61, 0x8c, 0x31, 0x88, 0x11,
      0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01, 0x80, 0x01,
  };

  static char vsplit_mask[] = {
      0xe0, 0x07, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03, 0xc8, 0x13, 0xdc,
      0x3b, 0xde, 0x7b, 0xcf, 0xf3, 0xcf, 0xf3, 0xde, 0x7b, 0xdc, 0x3b,
      0xc8, 0x13, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03, 0xe0, 0x07,
  };

  static BCursor VSplitCursor = {
      vsplit_bitmap,
      vsplit_mask,
      7,
      7,
      true,
  };

  BlenderCursor[WM_CURSOR_V_SPLIT] = &VSplitCursor;
  END_CURSOR_BLOCK;

  /********************** Horizontal Split Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char hsplit_bitmap[] = {
      0x00, 0x00, 0x80, 0x01, 0xc0, 0x03, 0x60, 0x06, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x60, 0x06, 0xc0, 0x03, 0x80, 0x01, 0x00, 0x00,
  };

  static char hsplit_mask[] = {
      0x80, 0x01, 0xc0, 0x03, 0xe0, 0x07, 0xf0, 0x0f, 0x60, 0x06, 0x01,
      0x80, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01, 0x80,
      0x60, 0x06, 0xf0, 0x0f, 0xe0, 0x07, 0xc0, 0x03, 0x80, 0x01,
  };

  static BCursor HSplitCursor = {
      hsplit_bitmap,
      hsplit_mask,
      7,
      7,
      true,
  };

  BlenderCursor[WM_CURSOR_H_SPLIT] = &HSplitCursor;
  END_CURSOR_BLOCK;

  /********************** North Arrow Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char narrow_bitmap[] = {
      0x00, 0x00, 0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8,
      0x0f, 0x7c, 0x1f, 0x3e, 0x3e, 0x1c, 0x1c, 0x08, 0x08, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  static char narrow_mask[] = {
      0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x0f, 0xfc,
      0x1f, 0xfe, 0x3f, 0x7f, 0x7f, 0x3e, 0x3e, 0x1c, 0x1c, 0x08, 0x08,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  static BCursor NArrowCursor = {
      narrow_bitmap,
      narrow_mask,
      7,
      5,
      true,
  };

  BlenderCursor[WM_CURSOR_N_ARROW] = &NArrowCursor;
  END_CURSOR_BLOCK;

  /********************** South Arrow Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char sarrow_bitmap[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x08, 0x08, 0x1c, 0x1c, 0x3e, 0x3e, 0x7c, 0x1f, 0xf8, 0x0f,
      0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00, 0x00,
  };

  static char sarrow_mask[] = {
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,
      0x08, 0x1c, 0x1c, 0x3e, 0x3e, 0x7f, 0x7f, 0xfe, 0x3f, 0xfc, 0x1f,
      0xf8, 0x0f, 0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00,
  };

  static BCursor SArrowCursor = {
      sarrow_bitmap,
      sarrow_mask,
      7,
      10,
      true,
  };

  BlenderCursor[WM_CURSOR_S_ARROW] = &SArrowCursor;
  END_CURSOR_BLOCK;

  /********************** East Arrow Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char earrow_bitmap[] = {
      0x00, 0x00, 0x00, 0x01, 0x80, 0x03, 0xc0, 0x07, 0x80, 0x0f, 0x00,
      0x1f, 0x00, 0x3e, 0x00, 0x7c, 0x00, 0x3e, 0x00, 0x1f, 0x80, 0x0f,
      0xc0, 0x07, 0x80, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
  };

  static char earrow_mask[] = {
      0x00, 0x01, 0x80, 0x03, 0xc0, 0x07, 0xe0, 0x0f, 0xc0, 0x1f, 0x80,
      0x3f, 0x00, 0x7f, 0x00, 0xfe, 0x00, 0x7f, 0x80, 0x3f, 0xc0, 0x1f,
      0xe0, 0x0f, 0xc0, 0x07, 0x80, 0x03, 0x00, 0x01, 0x00, 0x00,
  };

  static BCursor EArrowCursor = {
      earrow_bitmap,
      earrow_mask,
      10,
      7,
      true,
  };

  BlenderCursor[WM_CURSOR_E_ARROW] = &EArrowCursor;
  END_CURSOR_BLOCK;

  /********************** West Arrow Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char warrow_bitmap[] = {
      0x00, 0x00, 0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x01, 0xf8,
      0x00, 0x7c, 0x00, 0x3e, 0x00, 0x7c, 0x00, 0xf8, 0x00, 0xf0, 0x01,
      0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
  };

  static char warrow_mask[] = {
      0x80, 0x00, 0xc0, 0x01, 0xe0, 0x03, 0xf0, 0x07, 0xf8, 0x03, 0xfc,
      0x01, 0xfe, 0x00, 0x7f, 0x00, 0xfe, 0x00, 0xfc, 0x01, 0xf8, 0x03,
      0xf0, 0x07, 0xe0, 0x03, 0xc0, 0x01, 0x80, 0x00, 0x00, 0x00,
  };

  static BCursor WArrowCursor = {
      warrow_bitmap,
      warrow_mask,
      5,
      7,
      true,
  };

  BlenderCursor[WM_CURSOR_W_ARROW] = &WArrowCursor;
  END_CURSOR_BLOCK;

  /********************** Stop Sign Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char stop_bitmap[] = {
      0x00, 0x00, 0xe0, 0x07, 0xf8, 0x1f, 0x1c, 0x3c, 0x3c, 0x30, 0x76,
      0x70, 0xe6, 0x60, 0xc6, 0x61, 0x86, 0x63, 0x06, 0x67, 0x0e, 0x6e,
      0x0c, 0x3c, 0x3c, 0x38, 0xf8, 0x1f, 0xe0, 0x07, 0x00, 0x00,
  };

  static char stop_mask[] = {
      0xe0, 0x07, 0xf8, 0x1f, 0xfc, 0x3f, 0xfe, 0x7f, 0x7e, 0x7c, 0xff,
      0xf8, 0xff, 0xf1, 0xef, 0xf3, 0xcf, 0xf7, 0x8f, 0xff, 0x1f, 0xff,
      0x3e, 0x7e, 0xfe, 0x7f, 0xfc, 0x3f, 0xf8, 0x1f, 0xe0, 0x07,
  };

  static BCursor StopCursor = {
      stop_bitmap,
      stop_mask,
      7,
      7,
      false,
  };

  BlenderCursor[WM_CURSOR_STOP] = &StopCursor;
  END_CURSOR_BLOCK;

  /********************** Zoom In Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char zoomin_bitmap[] = {
      0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0xf8, 0x03, 0xb8, 0x03, 0xbc,
      0x07, 0x0c, 0x06, 0xbc, 0x07, 0xb8, 0x03, 0xf8, 0x0b, 0xe0, 0x14,
      0x00, 0x22, 0x00, 0x44, 0x00, 0x88, 0x00, 0x90, 0x00, 0x60,
  };

  static char zoomin_mask[] = {
      0x00, 0x00, 0xe0, 0x00, 0xf8, 0x03, 0xfc, 0x07, 0xfc, 0x07, 0xfe,
      0x0f, 0xfe, 0x0f, 0xfe, 0x0f, 0xfc, 0x07, 0xfc, 0x0f, 0xf8, 0x1f,
      0xe0, 0x3e, 0x00, 0x7c, 0x00, 0xf8, 0x00, 0xf0, 0x00, 0x60,
  };

  static BCursor ZoomInCursor = {
      zoomin_bitmap,
      zoomin_mask,
      6,
      6,
      false,
  };

  BlenderCursor[WM_CURSOR_ZOOM_IN] = &ZoomInCursor;
  END_CURSOR_BLOCK;

  /********************** Zoom Out Cursor ***********************/
  BEGIN_CURSOR_BLOCK;
  static char zoomout_bitmap[] = {
      0x00, 0x00, 0x00, 0x00, 0xe0, 0x00, 0xf8, 0x03, 0xf8, 0x03, 0xfc,
      0x07, 0x0c, 0x06, 0xfc, 0x07, 0xf8, 0x03, 0xf8, 0x0b, 0xe0, 0x14,
      0x00, 0x22, 0x00, 0x44, 0x00, 0x88, 0x00, 0x90, 0x00, 0x60,
  };

  static char zoomout_mask[] = {
      0x00, 0x00, 0xe0, 0x00, 0xf8, 0x03, 0xfc, 0x07, 0xfc, 0x07, 0xfe,
      0x0f, 0xfe, 0x0f, 0xfe, 0x0f, 0xfc, 0x07, 0xfc, 0x0f, 0xf8, 0x1f,
      0xe0, 0x3e, 0x00, 0x7c, 0x00, 0xf8, 0x00, 0xf0, 0x00, 0x60,
  };

  static BCursor ZoomOutCursor = {
      zoomout_bitmap,
      zoomout_mask,
      6,
      6,
      false,
  };

  BlenderCursor[WM_CURSOR_ZOOM_OUT] = &ZoomOutCursor;
  END_CURSOR_BLOCK;

  /********************** Area Pick Cursor ***********************/
  BEGIN_CURSOR_BLOCK;

  static char pick_area_bitmap[] = {
      0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0xfe, 0x00, 0x10,
      0x00, 0x10, 0x00, 0x10, 0x00, 0x00, 0xbf, 0x00, 0x81, 0x00, 0x81,
      0x00, 0x81, 0x00, 0x81, 0x00, 0x81, 0x00, 0x80, 0x00, 0xff,
  };

  static char pick_area_mask[] = {
      0x38, 0x00, 0x38, 0x00, 0x38, 0x00, 0xff, 0x01, 0xff, 0x01, 0xff,
      0x01, 0x38, 0x00, 0xb8, 0x7f, 0xb8, 0xff, 0x80, 0xc1, 0x80, 0xc1,
      0x80, 0xc1, 0x80, 0xc1, 0x80, 0xc1, 0x80, 0xff, 0x00, 0xff,
  };

  static BCursor PickAreaCursor = {
      pick_area_bitmap,
      pick_area_mask,
      4,
      4,
      false,
  };

  BlenderCursor[WM_CURSOR_PICK_AREA] = &PickAreaCursor;
  END_CURSOR_BLOCK;

  /********************** Put the cursors in the array ***********************/
}
