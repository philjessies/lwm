/*
 * lwm, a window manager for X11
 * Copyright (C) 1997-2016 Elliott Hughes, James Carter
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/* --- Administrator-configurable defaults. --- */

#define DEFAULT_TITLE_FONT                                                     \
  "-*-lucida-bold-r-normal-sans-14-*-*-*-p-*-iso10646-1"
#define DEFAULT_POPUP_FONT                                                     \
  "-*-lucida-medium-r-normal-sans-12-*-*-*-p-*-iso10646-1"
#define DEFAULT_TERMINAL "xterm"
#define DEFAULT_BORDER 6

#define HIDE_BUTTON Button3
#define MOVE_BUTTON Button2
#define RESHAPE_BUTTON Button1

// MOVING_BUTTON_MASK describes the bits which are set in the mouse statis mask
// value while either of the mouse buttons we can use for dragging/reshaping
// is down.
#define MOVING_BUTTON_MASK (Button1Mask | Button2Mask)

#define EDGE_RESIST 32

/* --- End of administrator-configurable defaults. --- */

/*
 * Window manager mode. wm is in one of five modes: it's either getting
 * user input to move/reshape a window, getting user input to make a
 * selection from the menu, waiting for user input to confirm a window close
 * (by releasing a mouse button after prssing it in a window's box),
 * waiting for user input to confirm a window hide (by releasing a mouse
 * button after prssing it in a window's frame),
 * or it's `idle' --- responding to events arriving
 * from the server, but not directly interacting with the user.
 * OK, so I lied: there's a sixth mode so that we can tell when wm's still
 * initialising.
 */
enum Mode {
  wm_initialising,
  wm_idle,
  wm_reshaping,
  wm_menu_up,
  wm_closing_window,
  wm_hiding_window
};

/** Window internal state. Yuck. */
enum IState { IPendingReparenting, INormal };

/**
* Focus mode, may me (and defaults to) enter where entering a window gives
* that window input focus, or click where a window must be explicitly clicked
* to give it the focus.
*/
enum FocusMode { focus_enter, focus_click };

/**
* Window edge, used in resizing. The `edge' ENone is used to signify a
* window move rather than a resize. The code is sufficiently similar that
* this isn't a special case to be treated separately.
*/
enum Edge {
  ETopLeft,
  ETop,
  ETopRight,
  ERight,
  ENone,
  ELeft,
  EBottomLeft,
  EBottom,
  EBottomRight,
  E_LAST
};

/**
* EWMH direction for _NET_WM_MOVERESIZE
*/
enum EWMHDirection {
  DSizeTopLeft,
  DSizeTop,
  DSizeTopRight,
  DSizeRight,
  DSizeBottomRight,
  DSizeBottom,
  DSizeBottomLeft,
  DSizeLeft,
  DMove,
  DSizeKeyboard,
  DMoveKeyboard
};

/**
* EWMH window type. See section 5.6 of the EWMH specification (1.2).
* WTypeNone indicates that no EWMH window type as been set and MOTIF
* hints should be used instead.
*/
enum EWMHWindowType {
  WTypeDesktop,
  WTypeDock,
  WTypeToolbar,
  WTypeMenu,
  WTypeUtility,
  WTypeSplash,
  WTypeDialog,
  WTypeNormal,
  WTypeNone
};

/**
* EWMH window state, See section 5.7 of the EWMH specification (1.2).
* lwm does not support all states. _NET_WM_STATE_HIDDEN is taken from
* Client.hidden.
*/
struct EWMHWindowState {
  Bool skip_taskbar;
  Bool skip_pager;
  Bool fullscreen;
  Bool above;
  Bool below;
};

/**
* EWMH "strut", or area on each edge of the screen reserved for docking
* bars/panels.
*/
struct EWMHStrut {
  unsigned int left;
  unsigned int right;
  unsigned int top;
  unsigned int bottom;
};

/**
* Screen information.
*/
struct ScreenInfo {
  Window root;
  Window popup;
  Window ewmh_compat;

  int display_width;  /* The width of the screen. */
  int display_height; /* The height of the screen. */
  EWMHStrut strut;    /* reserved areas */

  GC gc;      /* The default GC. */
  GC gc_thin; /* The default GC but with thinner lines. */
  GC menu_gc; /* The GC for the popup window (menu). */
  GC size_gc; /* The GC for the popup window (sizing). */

  unsigned long black; /* Black pixel value. */
  unsigned long white; /* White pixel value. */
  unsigned long gray;  /* Gray pixel value. */

  Cursor root_cursor;
  Cursor box_cursor;

  Cursor cursor_map[E_LAST];

  Bool ewmh_set_client_list; /* hack to prevent recursion */

  char *display_spec;
};

struct Client {
  Window window; /* Client's window. */
  Window parent; /* Window manager frame. */
  Window trans;  /* Window that client is a transient for. */

  Bool framed; /* True is lwm is maintaining a frame */

  Client *next; /* Next window in client list. */

  int border; /* Client's original border width. */

  XSizeHints size;        /* Client's current geometry information. */
  XSizeHints return_size; /* Client's old geometry information. */
  int state;              /* Window state. See ICCCM and <X11/Xutil.h> */

  Bool hidden; /* True if this client is hidden. */
  IState internal_state;
  int proto;

  int accepts_focus; /* Does this window want keyboard events? */

  char *name; /* Name used for title in frame. */
  int namelen;
  char *menu_name; /* Name used in root popup */
  int menu_namelen;
  Bool name_utf8;

  ScreenInfo *screen;

  Edge cursor; /* indicates which cursor is being used for parent window */

  EWMHWindowType wtype;
  EWMHWindowState wstate;
  EWMHStrut strut; /* reserved areas */
  
  // Holds a generated description, for debugging purposes, up to the given max
  // length. Call 'Client_Describe' to populate this and return.
  char *debug_desc;

  /* Colourmap scum. */
  Colormap cmap;
  int ncmapwins;
  Window *cmapwins;
  Colormap *wmcmaps;
};

/*
 * c->proto is a bitarray of these
 */
enum { Pdelete = 1, Ptakefocus = 2 };

/*
 * This should really have been in X.h --- if you select both ButtonPress
 * and ButtonRelease events, the server makes an automatic grab on the
 * pressed button for you. This is almost always exactly what you want.
 */
#define ButtonMask (ButtonPressMask | ButtonReleaseMask)

/* lwm.cc */
extern Mode mode;
extern int start_x;
extern int start_y;
extern Display *dpy;
extern int screen_count;
extern ScreenInfo *screens;
extern ScreenInfo *current_screen;
extern XFontSet font_set;
extern XFontSetExtents *font_set_ext;
extern XFontSet popup_font_set;
extern XFontSetExtents *popup_font_set_ext;
extern Atom _mozilla_url;
extern Atom motif_wm_hints;
extern Atom wm_state;
extern Atom wm_change_state;
extern Atom wm_protocols;
extern Atom wm_delete;
extern Atom wm_take_focus;
extern Atom wm_colormaps;
extern Atom compound_text;
extern Bool shape;
extern int shape_event;
extern char *argv0;
extern Bool forceRestart;
extern void shell(ScreenInfo *, int, int, int);
extern void sendConfigureNotify(Client *);
extern int titleHeight(void);
extern int titleWidth(XFontSet font_set, Client *c);
extern int popupHeight(void);
extern int popupWidth(char *string, int string_length);
extern int ascent(XFontSetExtents *font_set_ext);
extern ScreenInfo *getScreenFromRoot(Window);
extern void scanWindowTree(int);

// Debugging support (in lwm.cc).
extern Bool debug_configure_notify;  // -d=c
extern Bool debug_all_events;        // -d=e
extern Bool debug_focus;             // -d=f
extern Bool debug_map;               // -d=m
extern Bool debug_property_notify;   // -d=p
extern Bool printDebugPrefix(char const* filename, int line);

#define DBG_IF(cond, fmt, ...)                                                 \
    do {                                                                       \
      if (cond && printDebugPrefix(__FILE__, __LINE__)) {                      \
        fprintf(stderr, fmt, ##__VA_ARGS__);                                   \
        fputc('\n', stderr);                                                   \
      }                                                                        \
    } while (0)

#define DBG(fmt, ...) DBG_IF(1, fmt, ##__VA_ARGS__)

/* client.cc */
extern Client *client_head(void);
extern Edge interacting_edge;
extern Client *Client_Get(Window);
extern Client *Client_Add(Window, Window);
extern void Client_MakeSane(Client *, Edge, int *, int *, int *, int *);
extern void Client_DrawBorder(Client *, int);
extern void setactive(Client *, int, long);
extern void Client_SizeFeedback(void);
extern void size_expose(void);
extern void Client_ReshapeEdge(Client *, Edge);
extern void Client_Move(Client *);
extern void Client_SetState(Client *, int);
extern void Client_Raise(Client *);
extern void Client_Lower(Client *);
extern void Client_Close(Client *);
extern void Client_Remove(Client *);
extern void Client_FreeAll(void);
extern void Client_ColourMap(XEvent *);
extern void Client_EnterFullScreen(Client *c);
extern void Client_ExitFullScreen(Client *c);
extern void Client_Focus(Client *c, Time time);
extern void Client_ResetAllCursors(void);
extern void Client_Name(Client *c, const char *name, Bool is_utf8);
extern const char* Client_Describe(Client *c);
extern int hidden(Client *);
extern int withdrawn(Client *);
extern int normal(Client *);
extern void update_client_list(ScreenInfo *screen);
extern Client *current;

/* cursor.cc */
extern Cursor getEdgeCursor(Edge edge);
extern void initialiseCursors(int);

/* disp.cc */
extern void dispatch(XEvent *);
extern void reshaping_motionnotify(XEvent *);

/* error.cc */
extern int ignore_badwindow;
extern int errorHandler(Display *, XErrorEvent *);
extern void panic(const char *);

/* manage.cc */
extern void getWindowName(Client *);
extern void getNormalHints(Client *);
extern Bool motifWouldDecorate(Client *);
extern void manage(Client *, int);
extern void withdraw(Client *);
extern void cmapfocus(Client *);
extern void getColourmaps(Client *);
extern void getTransientFor(Client *);
extern void Terminate(int);

/* mouse.cc */
struct MousePos {
  int x;
  int y;
  // For mask values, see:
  // https://tronche.com/gui/x/xlib/events/keyboard-pointer/keyboard-pointer.html
  unsigned int modMask;
};

extern MousePos getMousePosition();
extern void hide(Client *);
extern void unhidec(Client *, int);
extern int menu_whichitem(int, int);
extern void menuhit(XButtonEvent *);
extern void unhide(int, int);
extern void menu_expose(void);
extern void menu_motionnotify(XEvent *);
extern void menu_buttonrelease(XEvent *);

/* shape.cc */
extern int shapeEvent(XEvent *);
extern int serverSupportsShapes(void);
extern int isShaped(Window);
extern void setShape(Client *);

/* resource.cc */
extern const char *font_name;
extern const char *popup_font_name;
extern const char *btn1_command;
extern const char *btn2_command;
extern int border;
extern FocusMode focus_mode;
extern char *sdup(char *);
extern void parseResources(void);

/* session.cc */
extern int ice_fd;
extern void session_init(int argc, char *argv[]);
extern void session_process(void);
extern void session_end(void);

/* ewmh.cc */
extern Atom ewmh_atom[];
extern void ewmh_init(void);
extern void ewmh_init_screens(void);
extern EWMHWindowType ewmh_get_window_type(Window w);
extern Bool ewmh_get_window_name(Client *c);
extern Bool ewmh_hasframe(Client *c);
extern void ewmh_set_state(Client *c);
extern void ewmh_get_state(Client *c);
extern void ewmh_change_state(Client *c, unsigned long action,
                              unsigned long atom);
extern void ewmh_set_allowed(Client *c);
extern void ewmh_set_client_list(ScreenInfo *screen);
extern void ewmh_get_strut(Client *c);
extern void ewmh_set_strut(ScreenInfo *screen);
extern char const *ewmh_atom_name(Atom at);

// geometry.cc
extern Bool isLeftEdge(Edge e);
extern Bool isRightEdge(Edge e);
extern Bool isTopEdge(Edge e);
extern Bool isBottomEdge(Edge e);
