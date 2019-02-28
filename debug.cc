#include "ewmh.h"
#include "lwm.h"
#include "xlib.h"

using namespace std;

// String manipulation functions.
// Yes, these are crude and inefficient, and involve a lot of string copying.
// However, this is a command-line debug interface, so I don't care.

// This is basically strtok in C++. Returns the bit of victim up to the next
// space, or end, removing the same part from victim. Yes, it modifies its
// argument. Do I care? No.
string nextToken(string& victim) {
  const size_t sep = victim.find(' ');
  string res = victim.substr(0, sep);
  if (sep == string::npos) {
    victim = "";
  } else {
    victim = victim.substr(sep);
  }
  while (!victim.empty() && victim[0] == ' ') {
    victim = victim.substr(1);
  }
  return res;
}

Rect fullScreenRect() {
  return Rect{0, 0, DisplayWidth(dpy, 0), DisplayHeight(dpy, 0)};
}

unsigned long deadColour() {
  XColor colour, exact;
  XAllocNamedColor(dpy, DefaultColormap(dpy, LScr::kOnlyScreenIndex), "grey",
                   &colour, &exact);
  return colour.pixel;
}

void DebugCLI::cmdXRandr(string line) {
  if (line == "?") {
    cout << "With struts:    " << LScr::I->VisibleAreas(true) << "\n";
    cout << "Without struts: " << LScr::I->VisibleAreas(false) << "\n";
    return;
  }
  vector<Rect> rects;
  while (!line.empty()) {
    const string tok = nextToken(line);
    Rect r = Rect::Parse(tok);
    if (r.empty()) {
      LOGE() << "Failed to parse rect '" << tok << "'";
    } else {
      rects.push_back(r);
    }
  }
  if (rects.empty()) {
    rects.push_back(fullScreenRect());
  }
  cout << "Setting visible areas to " << rects << "\n";
  resetDeadZones(rects);
  LScr::I->SetVisibleAreas(rects);
}

static string windowEnding(int num) {
  if (num == 0) {
    return "s.";
  } else if (num == 1) {
    return ":";
  }
  return "s:";
}

bool DebugCLI::disableDebugging(Window w) {
  map<Window, string>::iterator it = debug_windows_.find(w);
  if (it == debug_windows_.end()) {
    return false;
  }
  Log("D", __FILE__, __LINE__, 0, true)
      << it->second << ": Debugging disabled for client";
  debug_windows_.erase(it);
  return true;
}

void DebugCLI::cmdDbg(string line) {
  if (line == "help") {
    cout << "Usage:\n";
    cout << "  dbg ?             list of debug-enabled things\n";
    cout << "  dbg 0x123 foo     debug window 0x123, debug label foo\n";
    cout << "  dbg off 0x123     remove debugging from window 0x123\n";
    cout << "  dbg off foo       remove debugging from window with label foo\n";
    cout << "  dbg off           remove debugging from everything\n";
    cout << "  dbg auto          auto-enable debugging of new windows\n";
    cout << "  dbg noauto        disable auto-debugging\n";
    return;
  }
  if (line == "?" || line == "") {
    cout << "Debug enabled for " << debug_windows_.size() << " window"
         << windowEnding(debug_windows_.size()) << "\n";
    for (const auto& kv : debug_windows_) {
      Client* c = LScr::I->GetClient(kv.first);
      cout << "  0x" << hex << kv.first << dec << "/" << kv.second << " : ";
      if (c) {
        cout << *c;
      } else {
        cout << "(null Client)";
      }
      cout << "\n";
    }
    return;
  }
  const string tok = nextToken(line);
  if (tok == "off") {
    const string tok = nextToken(line);
    if (tok.empty()) {
      vector<Window> ws;
      for (const auto& kv : debug_windows_) {
        ws.push_back(kv.first);
      }
      for (Window w : ws) {
        disableDebugging(w);
      }
      cout << "Removed all debug clients\n";
      return;
    }
    if (tok[0] == '0') {
      const Window w = Window(strtol(tok.c_str(), nullptr, 0));
      if (disableDebugging(w)) {
        return;
      }
    }
    // Remove by name.
    for (const auto& kv : debug_windows_) {
      if (kv.second == tok) {
        disableDebugging(kv.first);
        return;
      }
    }
    cout << "No debug-enabled client found matching '" << tok << "'\n";
    return;
  }
  if (tok == "auto") {
    debug_new_ = true;
    cout << "Auto-debug enabled for new windows\n";
    return;
  }
  if (tok == "noauto") {
    debug_new_ = false;
    cout << "Auto-debug disabled for new windows\n";
    return;
  }
  // If we get here, we're enabling a new client.
  const Window w = Window(strtol(tok.c_str(), nullptr, 0));
  const Client* c = LScr::I->GetClient(w);
  if (!c) {
    cout << "Unknown client for 0x" << hex << w << dec << " (" << tok << ")\n";
    return;
  }
  string name = nextToken(line);
  if (name.empty()) {
    name = tok;
  }
  debug_windows_[w] = name;
  LOGD(c) << "Debugging enabled for client";
}

static void cmdLS() {
  for (const auto& kv : LScr::I->Clients()) {
    cout << *(kv.second) << "\n";
  }
}

// We maintain an internal pointer to the only possible instance of a DebugCLI,
// so we can provide nice global functions.
static DebugCLI* debugCLI;

DebugCLI::DebugCLI() {
  LOGF_IF(debugCLI) << "Only one DebugCLI may be created";
  debugCLI = this;
  cout << "Debug CLI enabled. Will listen for commands on stdin.\n";
  cout << "Type 'help' for help\n> " << flush;
}

// static
bool DebugCLI::DebugEnabled(const Client* c) {
  if (!debugCLI || !c) {
    return false;
  }
  return debugCLI->debugEnabled(c);
}

bool DebugCLI::debugEnabled(const Client* c) {
  return debug_windows_.count(c->window) || debug_windows_.count(c->parent);
}

// static
string DebugCLI::NameFor(const Client* c) {
  if (!debugCLI || !c) {
    return "";
  }
  return debugCLI->nameFor(c);
}

string DebugCLI::nameFor(const Client* c) {
  map<Window, string>::iterator it = debug_windows_.find(c->window);
  if (it == debug_windows_.end()) {
    it = debug_windows_.find(c->parent);
  }
  if (it == debug_windows_.end()) {
    return "";
  }
  return it->second;
}

// static
void DebugCLI::NotifyClientAdd(Client* c) {
  if (!debugCLI || !c || !debugCLI->debug_new_) {
    return;
  }
  // Auto-enable debugging for new windows is active, so do so.
  // Keep a counter, so we can construct a good name.
  static int name_counter;
  char buf[64];
  snprintf(buf, sizeof(buf), "auto%d", name_counter++);
  debugCLI->debug_windows_[c->window] = buf;
  LOGD(c) << "Debugging auto-enabled for client";
}

// static
void DebugCLI::NotifyClientRemove(Client* c) {
  if (!debugCLI || !c) {
    return;
  }
  // Client has gone away, disable debugging for it.
  debugCLI->disableDebugging(c->window);
  if (c->framed) {
    debugCLI->disableDebugging(c->parent);
  }
}

void DebugCLI::resetDeadZones(const vector<Rect>& visible) {
  vector<Rect> dead(1, fullScreenRect());
  for (const Rect& vis : visible) {
    vector<Rect> new_dead;
    for (const Rect& d : dead) {
      const Rect i = Rect::Intersect(vis, d);
      if (i.empty()) {
        new_dead.push_back(d);
        continue;
      }
      // There's definitely an intersection.
      // Full-width rect above the visible area.
      if (i.yMin > d.yMin) {
        new_dead.push_back(Rect{d.xMin, d.yMin, d.xMax, i.yMin});
      }
      // Full-width rect below the visible area.
      if (i.yMax < d.yMax) {
        new_dead.push_back(Rect{d.xMin, i.yMax, d.xMax, d.yMax});
      }
      // Left of the visible area.
      if (i.xMin > d.xMin) {
        new_dead.push_back(Rect{d.xMin, i.yMin, i.xMin, i.yMax});
      }
      // Right of the visible area.
      if (i.xMax < d.xMax) {
        new_dead.push_back(Rect{i.xMax, i.yMin, d.xMax, i.yMax});
      }
    }
    dead = new_dead;
  }
  for (Window w : dead_zones_) {
    XDestroyWindow(dpy, w);
  }
  dead_zones_.clear();

  const unsigned long dead_colour = deadColour();
  for (const Rect& r : dead) {
    const Window w =
        XCreateSimpleWindow(dpy, LScr::I->Root(), r.xMin, r.yMin, r.width(),
                            r.height(), 0, dead_colour, dead_colour);
    XMapRaised(dpy, w);
    dead_zones_.push_back(w);
  }
  cout << "Inaccessible areas are: " << dead << "\n";
}

void DebugCLI::Read() {
  char buf[1024];
  ssize_t bytes = read(STDIN_FILENO, buf, sizeof(buf));
  if (bytes == sizeof(buf)) {
    LOGE() << "A whole " << bytes << " bytes on one line? You're crazy.";
    return;
  }
  string line;
  for (int i = 0; i < sizeof(buf) && buf[i] != '\n'; i++) {
    line.push_back(buf[i]);
  }
  const string& cmd = nextToken(line);
  if (cmd == "xrandr") {
    cmdXRandr(line);
  } else if (cmd == "ls") {
    cmdLS();
  } else if (cmd == "dbg") {
    cmdDbg(line);
  } else if (cmd == "help") {
    cout << "Available commands:\n";
    cout << "  dbg     enable/disable per-client debug messages\n";
    cout << "  help    print this help message\n";
    cout << "  ls      list active clients\n";
    cout << "  xrandr  simulate xrandr desktop screen config changes\n";
  } else {
    cout << "Didn't understand command '" << cmd << "'\n";
  }
  // Print the prompt again, so we look like we're listening.
  cout << "> " << flush;
}
