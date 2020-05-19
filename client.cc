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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sstream>

#include <unistd.h>

#include "ewmh.h"
#include "lwm.h"
#include "xlib.h"

static int popup_width;  // The width of the size-feedback window.

// Returns the total height, in pixels, of the window title bar.
int titleBarHeight() {
  return textHeight() + borderWidth();
}

// closeBounds returns the bounding box of the close icon cross.
// If displayBounds is true, the returned box is the cross itself; if false,
// it's the active area (which extends down to the client window, and across
// to the start of the title bar).
// The reason for the difference is simple usability: particularly on large 4k
// displays, it's tricky to hit the cross itself, and easy to instead click on
// the area below and to the right of the cross, which would result in the
// window being resized. However, resizing from that position seems weird; one
// would more naturally pick the outer edge for such an action, so it makes
// more sense to have that close the window too.
Rect closeBounds(bool displayBounds) {
  const int quarter = (borderWidth() + textHeight()) / 4;
  const int cMin = quarter + 2;
  const int cMax = displayBounds ? 3 * quarter : titleBarHeight();
  return Rect{cMin, cMin, cMax, cMax};
}

Rect titleBarBounds(int windowWidth) {
  const int x = titleBarHeight();
  return Rect{x, topBorderWidth(), windowWidth - x, titleBarHeight()};
}

std::ostream& operator<<(std::ostream& os, const Client& c) {
  os << WinID(c.window);
  if (c.parent) {
    os << " (frame=" << WinID(c.parent) << ")";
  }
  if (c.trans) {
    os << " (trans=" << WinID(c.trans) << ")";
  }
  os << " outer=" << c.FrameRect() << " inner=" << c.ContentRect() << " ";
  if (c.hidden) {
    os << "(";
  }
  os << "\"" << c.Name() << "\"";
  if (c.hidden) {
    os << ")";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const WinID& w) {
  os << "0x" << std::hex << w.w << std::dec;
  return os;
}

// Our use of -1 for the x/y min value on the left and top edges, and the +1
// on the width/height for the right/bottom edges looks funny. The reason for
// doing this is because our furniture window includes a 1 pixel border, which
// exists outside our normal window coordinates. If we don't add these -/+1
// hacks, we end up with the wrong behaviour when the pointer is within this
// border, which looks funny.
Rect Client::EdgeBounds(Edge e) const {
  const Rect fr = FrameRect();
  const int inset = titleBarHeight();
  Rect res{inset, inset, fr.width() - inset, fr.height() - inset};
  if (isLeftEdge(e)) {
    res.xMin = -1;
    res.xMax = inset;
  } else if (isRightEdge(e)) {
    res.xMin = fr.width() - inset;
    res.xMax = fr.width() + 1;
  }
  if (isTopEdge(e)) {
    res.yMin = -1;
    res.yMax = inset;
  } else if (isBottomEdge(e)) {
    res.yMin = fr.height() - inset;
    res.yMax = fr.height() + 1;
  }
  return res;
}

// Truncate names to this many characters (UTF8 characters, naturally). Much
// simpler than trying to calculate the 'best' length based on the render text
// width, which is quite unnecessary anyway.
static constexpr int maxMenuNameChars = 100;

std::string Client::MenuName() const {
  if (name_.size() <= maxMenuNameChars) {
    return name_;
  }
  int chars = 0;
  int uniLeft = 0;
  for (int i = 0; i < name_.size(); i++) {
    if (uniLeft && --uniLeft)
      continue;  // Skip trailing UTF8 only.
    char ch = name_[i];
    chars++;
    if (chars == maxMenuNameChars) {
      // i must be at the start of a unicode character (or ascii), and we've
      // seen as many visible characters as we wanted.
      return name_.substr(0, i) + "...";
    }
    int mask = 0xf8;
    int val = 0xf0;
    uniLeft = 4;
    while (mask && uniLeft) {
      if ((ch & mask) == val) {
        break;
      }
      uniLeft--;
      mask = (mask << 1) & 0xff;
      val = (val << 1) & 0xff;
    }
  }
  // Dropped off the end? The name must be under maxMenuNameChars UTF8
  // characters in length then.
  return name_;
}

void Client::Hide() {
  LScr::I->GetHider()->Hide(this);
}

void Client::Unhide() {
  LScr::I->GetHider()->Unhide(this);
}

Edge Client::EdgeAt(Window w, int x, int y) const {
  if (w != parent) {
    return EContents;
  }
  if (closeBounds(false).contains(x, y)) {  // false -> get action bounds.
    return EClose;
  }
  if (titleBarBounds(FrameRect().width()).contains(x, y)) {
    return ENone;  // Rename to ETitleBar.
  }
  const std::vector<Edge> movementEdges{ETopLeft, ETop,        ETopRight,
                                        ERight,   ELeft,       EBottomLeft,
                                        EBottom,  EBottomRight};
  for (Edge e : movementEdges) {
    if (EdgeBounds(e).contains(x, y)) {
      return e;
    }
  }
  return ENone;
}

void Client::SetIcon(xlib::ImageIcon* icon) {
  if (icon) {
    icon_ = icon;
  }
}

void focusChildrenOf(Client* c, Window parent) {
  xlib::WindowTree wtree = xlib::WindowTree::Query(dpy, parent);
  for (Window win : wtree.children) {
    const XWindowAttributes attr = xlib::XGetWindowAttributes(win);
    if (attr.all_event_masks & FocusChangeMask) {
      LOGD(c) << "  Focusing child " << WinID(win);
      XSetInputFocus(dpy, win, RevertToPointerRoot, CurrentTime);
    }
  }
}

void Client::FocusGained() {
  if (framed && Resources::I->ClickToFocus()) {
    // In click-to-focus mode, our FocusLost function will grab button events
    // on the client's window. We must relinquish this grabbing when we gain
    // focus, otherwise the client itself won't get the events when it is
    // focused.
    XUngrabButton(dpy, AnyButton, AnyModifier, window);
  }
  DrawBorder();
}

void Client::FocusLost() {
  if (framed && Resources::I->ClickToFocus()) {
    // In click-to-focus mode, we need to intercept button clicks within the
    // client window, so we can give the window focus. While some applications,
    // notably java apps, will grab input focus when clicked on, xterm and
    // many others do not. Thus, we need to grab click notifications ourselves
    // so that we can properly support click-to-focus.
    XGrabButton(dpy, AnyButton, AnyModifier, window, false,
                ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
                GrabModeSync, None, None);
  }
  DrawBorder();
}

void Client::DrawBorder() {
  if (parent == LScr::I->Root() || parent == 0 || !framed ||
      wstate.fullscreen) {
    return;
  }
  const bool active = HasFocus();

  XSetWindowBackground(
      dpy, parent,
      active ? LScr::I->ActiveBorder() : LScr::I->InactiveBorder());
  XClearWindow(dpy, parent);

  // Cross for the close icon.
  const Rect r = closeBounds(true);  // true -> get display bounds.
  const GC close_gc = LScr::I->GetCloseIconGC(active);
  XDrawLine(dpy, parent, close_gc, r.xMin, r.yMin, r.xMax, r.yMax);
  XDrawLine(dpy, parent, close_gc, r.xMin, r.yMax, r.xMax, r.yMin);
  const int bw = borderWidth();
  const int quarter = (titleBarHeight()) / 4;
  if (active) {
    // Give the title a nice background, and differentiate it from the
    // rest of the furniture to show it acts differently (moves the window
    // rather than resizing it).
    // However, skip the top few pixels if the 'topBorderWidth' is non-zero, to
    // show where the resize handle is.
    const int topBW = topBorderWidth();
    const int x = bw + 3 * quarter;
    const int w = FrameRect().width() - 2 * x;
    const int h = textHeight() + bw - topBW;
    XFillRectangle(dpy, parent, LScr::I->GetTitleGC(), x, topBW, w, h);
  }

  // Find where the title stuff is going to go.
  int x = bw + 2 + (3 * quarter);
  int y = bw / 2 + g_font->ascent;

  // Do we have an icon? If so, draw it to the left of the title text.
  if (Icon() && Resources::I->AppIconInWindowTitle()) {
    if (active) {
      Icon()->PaintActive(parent, x, 0, titleBarHeight(), titleBarHeight());
    } else {
      Icon()->PaintInactive(parent, x, 0, titleBarHeight(), titleBarHeight());
    }
    x += titleBarHeight();  // Title bar text must come after.
  }

  // Draw window title.
  XftColor* color = active ? &g_font_active_title : &g_font_inactive_title;
  drawString(parent, x, y, Name(), color);
}

Rect Client::FrameRect() const {
  Rect res = content_rect_;
  if (!framed) {
    return res;
  }
  return FrameFromContentRect(res);
}

Rect Client::ContentRectRelative() const {
  int subX = content_rect_.xMin - borderWidth();
  int subY = content_rect_.yMin - titleBarHeight();
  return Rect::Translate(content_rect_, Point{-subX, -subY});
}

// static
Rect Client::ContentFromFrameRect(const Rect& r) {
  Rect res = r;
  const int bw = borderWidth();
  res.xMin += bw;
  res.yMin += titleBarHeight();
  res.xMax -= bw;
  res.yMax -= bw;
  return res;
}

// static
Rect Client::FrameFromContentRect(const Rect& r) {
  Rect res = r;
  const int bw = borderWidth();
  res.xMin -= bw;
  res.yMin -= titleBarHeight();
  res.xMax += bw;
  res.yMax += bw;
  return res;
}

void Client_Remove(Client* c) {
  if (c == 0) {
    return;
  }
  if (c->parent != LScr::I->Root()) {
    XDestroyWindow(dpy, c->parent);
  }
  LScr::I->Remove(c);
  ewmh_set_client_list();
  ewmh_set_strut();
}

std::string makeSizeString(int x, int y) {
  std::ostringstream buf;
  buf << x << " x " << y;
  return buf.str();
}

void Client_SizeFeedback() {
  // Make the popup 10% wider than the widest string it needs to show.
  popup_width =
      textWidth(makeSizeString(DisplayWidth(dpy, 0), DisplayHeight(dpy, 0)));
  popup_width += popup_width / 10;

  // Put the popup in the right place to report on the window's size.
  const MousePos mp = getMousePosition();
  xlib::XMoveResizeWindow(LScr::I->Popup(), mp.x + 8, mp.y + 8, popup_width,
                          textHeight() + 1);
  xlib::XMapRaised(LScr::I->Popup());

  // Ensure that the popup contents get redrawn. Eventually, the function
  // size_expose will get called to do the actual redraw.
  XClearArea(dpy, LScr::I->Popup(), 0, 0, 0, 0, true);
}

void size_expose() {
  Client* c = LScr::I->GetFocuser()->GetFocusedClient();
  if (!c) {
    return;
  }
  const std::string text = c->SizeString();
  const int x = (popup_width - textWidth(text)) / 2;
  drawString(LScr::I->Popup(), x, g_font->ascent + 1, text,
             &g_font_popup_colour);
}

std::string Client::SizeString() const {
  return makeSizeString(x_limiter_.DisplayableSize(content_rect_.width()),
                        y_limiter_.DisplayableSize(content_rect_.height()));
}

void Client_Lower(Client* c) {
  if (c == 0) {
    return;
  }
  xlib::XLowerWindow(c->window);
  if (c->framed) {
    xlib::XLowerWindow(c->parent);
  }
  ewmh_set_client_list();
}

void Client_Raise(Client* c) {
  if (c == 0) {
    return;
  }
  if (c->framed) {
    xlib::XRaiseWindow(c->parent);
  }
  xlib::XRaiseWindow(c->window);

  for (auto it : LScr::I->Clients()) {
    Client* tr = it.second;
    if (tr->trans != c->window && !(c->framed && tr->trans == c->parent)) {
      continue;
    }
    if (tr->framed) {
      xlib::XRaiseWindow(tr->parent);
    }
    xlib::XRaiseWindow(tr->window);
  }
  ewmh_set_client_list();
}

void Client_Close(Client* c) {
  if (c == 0) {
    return;
  }
  // Terminate the client nicely if possible. Be brutal otherwise.
  if (c->proto & Pdelete) {
    xlib::SendClientMessage(c->window, wm_protocols, wm_delete, CurrentTime);
  } else {
    XKillClient(dpy, c->window);
  }
}

void Client::SetState(int state) {
  long data[2];

  data[0] = (long)state;
  data[1] = (long)None;

  state_ = state;
  XChangeProperty(dpy, window, wm_state, wm_state, 32, PropModeReplace,
                  (unsigned char*)data, 2);
  ewmh_set_state(this);
}

extern void Client_ResetAllCursors() {
  for (auto it : LScr::I->Clients()) {
    Client* c = it.second;
    if (!c->framed) {
      continue;
    }
    XSetWindowAttributes attr{};
    attr.cursor = LScr::I->Cursors()->Root();
    xlib::XChangeWindowAttributes(c->parent, CWCursor, &attr);
    c->cursor = ENone;
  }
}

extern void Client_FreeAll() {
  // Take a copy of the client pointers, as releasing the clients may mutate
  // the underlying clients map.
  std::vector<Client*> clients;
  for (auto it : LScr::I->Clients()) {
    clients.push_back(it.second);
  }
  for (auto c : clients) {
    c->Release();
  }
}

Client::Client(Window w,
               const XWindowAttributes& attr,
               const DimensionLimiter& x_limiter,
               const DimensionLimiter& y_limiter)
    : window(w),
      parent(LScr::I->Root()),
      content_rect_(Rect::From<const XWindowAttributes&>(attr)),
      x_limiter_(x_limiter),
      y_limiter_(y_limiter),
      original_border_width_(attr.border_width) {}

void Client::Release() {
  // Reparent the client window to the root, to elide our furniture window.
  const Rect cr = ContentRect();
  LOGD(this) << "Client::Release " << cr;
  if (!framed) {
    return;
  }
  xlib::XReparentWindow(window, LScr::I->Root(), cr.xMin, cr.yMin);
  if (hidden) {
    // The window was iconised, so map it back into view so it isn't lost
    // forever, but lower it so it doesn't jump all over the foreground.
    xlib::XMapWindow(window);
    xlib::XLowerWindow(window);
  }

  // Give it back its initial border width.
  XWindowChanges wc{};
  wc.border_width = original_border_width_;
  xlib::XConfigureWindow(window, CWBorderWidth, &wc);
}

void Client::EnterFullScreen() {
  pre_full_screen_content_rect_ = content_rect_;
  // For now, just find the 'main screen' and use that.
  // Ideally, we'd actually try to find the largest contiguous rectangle, as
  // someone might be using two identical-sized monitors next to each other
  // to get a bigger view of what they're killing, but for now we'll save that
  // for another day.
  const Rect scr = LScr::I->GetPrimaryVisibleArea(false);  // Without struts.
  content_rect_ = scr;
  if (framed) {
    xlib::XMoveResizeWindow(parent, FrameRect());
  }
  xlib::XMoveResizeWindow(window, content_rect_);
  xlib::XRaiseWindow(framed ? parent : window);
  // SendConfigureNotify();
}

void Client::ExitFullScreen() {
  content_rect_ = pre_full_screen_content_rect_;
  if (framed) {
    xlib::XMoveResizeWindow(parent, FrameRect());
  }
  xlib::XMoveResizeWindow(window, content_rect_);
  // SendConfigureNotify();
}

void Client::SendConfigureNotify() {
  XConfigureEvent ce{};
  ce.type = ConfigureNotify;
  ce.event = window;
  ce.window = window;
  content_rect_.To(ce);
  ce.border_width = framed ? 0 : original_border_width_;
  ce.above = None;
  ce.override_redirect = 0;
  LOGD(this) << "Sending config notify, r=" << content_rect_ << " to "
             << WinID(window);
  XSendEvent(dpy, window, false, StructureNotifyMask, (XEvent*)&ce);
}

void Client::SetName(const char* c, int len) {
  name_ = (c && len) ? std::string(c, len) : "";
}

bool Client::HasFocus() const {
  return this == LScr::I->GetFocuser()->GetFocusedClient();
}

// static
Client* Client::FocusedClient() {
  return LScr::I->GetFocuser()->GetFocusedClient();
}

Rect Client::LimitResize(const Rect& suggested) {
  Rect res = suggested;
  x_limiter_.Limit(content_rect_.xMin, content_rect_.xMax, res.xMin, res.xMax);
  y_limiter_.Limit(content_rect_.yMin, content_rect_.yMax, res.yMin, res.yMax);
  return res;
}

void Client::MoveTo(const Rect& new_content_rect) {
  if (content_rect_.width() != new_content_rect.width() ||
      content_rect_.height() != new_content_rect.height()) {
    LOGF() << "Invalid move from " << content_rect_ << " to "
           << new_content_rect << " (size mismatch)";
  }
  if (content_rect_.xMin == new_content_rect.xMin &&
      content_rect_.yMin == new_content_rect.yMin) {
    return;  // Move to same place. AKA NOP.
  }
  content_rect_ = new_content_rect;
  if (framed) {
    Rect frame_rect = FrameRect();
    xlib::XMoveWindow(parent, frame_rect.origin());
  }
  // Do I need to send a configure notify? According to this:
  // https://tronche.com/gui/x/xlib/events/window-state-change/configure.html
  // ...it looks like the job of the X server itself.
  LOGD(this) << "MoveTo " << new_content_rect;
  SendConfigureNotify();
}

void Client::MoveResizeTo(const Rect& new_content_rect) {
  if (new_content_rect == content_rect_) {
    // Nothing to do.
    return;
  }
  const bool move_client = content_rect_.origin() != new_content_rect.origin();
  content_rect_ = new_content_rect;
  if (framed) {
    xlib::XMoveResizeWindow(parent, FrameRect());
    if (move_client) {
      // Client was resized towards the top/left. We need to move and resize it
      // so it stays within the right offset of its frame. Coordinates are
      // relative to the frame window.
      const Rect fr = FrameRect();
      Rect r = Rect::Translate(content_rect_, Point{-fr.xMin, -fr.yMin});
      xlib::XMoveResizeWindow(window, r);
    } else {
      // Client was only resized at the bottom and/or right. No move necessary.
      xlib::XResizeWindow(window, content_rect_.area());
    }
  } else {
    xlib::XMoveResizeWindow(window, content_rect_);
  }
  // Do I need to send a configure notify? According to this:
  // https://tronche.com/gui/x/xlib/events/window-state-change/configure.html
  // ...it looks like the job of the X server itself.
  LOGD(this) << "MoveResizeTo " << new_content_rect;
  SendConfigureNotify();
}

void Client::FurnishAt(const Rect& rect) {
  content_rect_ = rect;
  LScr::I->Furnish(this);
}

void Focuser::EnterWindow(Window w, Time time) {
  // If the window being entered is still part of the same client, we do
  // nothing. This avoids giving focus to a window in the following situation:
  // 1: Mouse pointer is over window X.
  // 2: Window Y is opened and is given focus.
  // 3: Mouse pointer is moved such that it crosses into a different window in
  //    the client of X.
  // In this situation, window Y should still keep focus.
  Client* c = LScr::I->GetClient(w);
  LOGD(c) << "EnterWindow " << WinID(w) << " at " << time;
  const Window le = last_entered_;
  last_entered_ = w;
  if (!c || (c == LScr::I->GetClient(le))) {
    return;  // No change in pointed-at client, so we have nothing to do.
  }
  if (!Resources::I->ClickToFocus()) {
    FocusClient(c, time);
  }
}

void Focuser::UnfocusClient(Client* c) {
  const bool had_focus = c->HasFocus();
  RemoveFromHistory(c);
  if (!had_focus) {
    return;
  }
  // The given client used to have input focus; give focus to the next in line.
  if (focus_history_.empty()) {
    return;  // No one left to give focus to.
  }
  ReallyFocusClient(focus_history_.front(), CurrentTime, true);
}

void Focuser::FocusClient(Client* c, Time time) {
  // If this window is already focused, ignore.
  if (!c->HasFocus()) {
    ReallyFocusClient(c, time, true);
    // Old LWM seems to always have raised the window being focused, so let's
    // copy that. Maybe it should be a separate resource option though?
    if (Resources::I->ClickToFocus()) {
      Client_Raise(c);
    }
  } else {
    LOGD(c) << "Ignoring FocusClient request";
  }
}

void Focuser::ReallyFocusClient(Client* c, Time time, bool give_focus) {
  Client* was_focused = GetFocusedClient();
  RemoveFromHistory(c);
  focus_history_.push_front(c);

  XDeleteProperty(dpy, LScr::I->Root(), ewmh_atom[_NET_ACTIVE_WINDOW]);
  // There was a check for 'c->IsHidden()' here. Needed?
  if (give_focus) {
    if (c->accepts_focus) {
      // If the top-level window accepts focus, we must only give focus to it,
      // not to its children. Google Chrome (a web browser) won't work if we
      // also give focus to its children, as it now has a child window which,
      // if given focus, will just drop everything on the floor. The effect of
      // this is to make Chrome windows impossible to type into (nor use
      // hotkeys in) if they lose then regain focus. When a window is newly
      // opened it will respond to keypresses, but not on focus regain.
      LOGD(c) << "Focusing main window " << WinID(c->window);
      XSetInputFocus(dpy, c->window, RevertToPointerRoot, CurrentTime);
      if (c->proto & Ptakefocus) {
        xlib::SendClientMessage(c->window, wm_protocols, wm_take_focus, time);
      }
    } else if (c->proto & Ptakefocus) {
      // Main window doesn't accept focus, but there's an indication that its
      // children may. This is the case for Java apps, which have two windows
      // inside the main window, one called 'FocusProxy' and the other called
      // 'Content window'. We want to give focus to the FocusProxy, but there
      // doesn't seem an obvious way to determine which child is the right one,
      // so let's just ping them all.
      focusChildrenOf(c, c->window);
    } else {
      // FIXME: is this sensible?
      XSetInputFocus(dpy, None, RevertToPointerRoot, CurrentTime);
    }
  }
  XChangeProperty(dpy, LScr::I->Root(), ewmh_atom[_NET_ACTIVE_WINDOW],
                  XA_WINDOW, 32, PropModeReplace, (unsigned char*)&c->window,
                  1);

  if (was_focused && (was_focused != c)) {
    was_focused->FocusLost();
  }
  c->FocusGained();
}

void Focuser::RemoveFromHistory(Client* c) {
  for (std::list<Client*>::iterator it = focus_history_.begin();
       it != focus_history_.end(); it++) {
    if (*it == c) {
      focus_history_.erase(it);
      return;
    }
  }
}

Client* Focuser::GetFocusedClient() {
  if (focus_history_.empty()) {
    return nullptr;
  }
  return focus_history_.front();
}
