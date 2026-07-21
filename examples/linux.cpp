// Copyright (c) 2026 Vladimir Egorov
// This library is licensed under the MIT License.
// See the LICENSE file in the root of the repository for the full license text.

#include "rForth.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/sysinfo.h>
#include <termios.h>
#include <unistd.h>
#include <csignal>
#include <cerrno>

// Not part of the library - a plain app-defined function, registered below as
// the "greet" word to demonstrate forth_dict_add().
void greet()
{
  struct sysinfo info;
  if (sysinfo(&info) == 0) {
    unsigned long total = info.totalram;
    unsigned long free_ram = info.freeram;
    double percent = (double)free_ram / total * 100.0;
    printf("rForth on Linux, RAM %.2f%% free (%lu/%lu KB)\n", percent, free_ram / 1024, total / 1024);
  }
  else {
    printf("rForth on Linux (memory info unavailable)\n");
  }
}

static const Code words[] = {
  CODE("greet", greet()),
};

// Host-side implementation of rForth.h's ForthFile interface, backing the
// library's file-access words (open-file, read-line, etc.) with plain stdio.
class PosixForthFile : public ForthFile {
  FILE* fp;

public:
  explicit PosixForthFile(FILE* f)
    : fp(f)
  {
  }
  void close() override
  {
    fclose(fp);
  }
  long read(char* buf, long len) override
  {
    return (long)fread(buf, 1, len, fp);
  }
  long write(const char* buf, long len) override
  {
    return (long)fwrite(buf, 1, len, fp);
  }
  long read_line(char* buf, long max_len) override
  {
    if (!fgets(buf, max_len, fp)) return -1;
    long n = (long)strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') buf[--n] = '\0';
    return n;
  }
  bool seek(long pos) override
  {
    return fseek(fp, pos, SEEK_SET) == 0;
  }
  long position() override
  {
    return ftell(fp);
  }
  long size() override
  {
    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long end = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return end;
  }
};

// create=true (CREATE-FILE) truncates; create=false (OPEN-FILE) requires the
// file to already exist, so w/o|r/w both map to "r+" to avoid truncating it.
static const char* fam_mode(int fam, bool create)
{
  if (create) return fam == FAM_WO ? "w" : "w+";
  return fam == FAM_RO ? "r" : "r+";
}

// The two hooks rForth.h declares under "to implement" for file access -
// the library calls these, never touching stdio itself.
ForthFile* forth_file_open(const char* path, int fam, bool create)
{
  FILE* fp = fopen(path, fam_mode(fam, create));
  return fp ? new PosixForthFile(fp) : nullptr;
}

bool forth_file_delete(const char* path)
{
  return remove(path) == 0;
}

static struct termios orig_termios;
static bool raw_mode_active = false;

static void restore_terminal()
{
  if (raw_mode_active) tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

static void enable_raw_terminal()
{
  if (!isatty(STDIN_FILENO)) return;
  if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) return;
  atexit(restore_terminal);
  struct termios raw = orig_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  raw_mode_active = true;
}

static bool input_closed = false;
static volatile sig_atomic_t g_sigint_flag = 0;

static void handle_sigint(int)
{
  g_sigint_flag = 1;
  // If a Forth word is blocked in KEY/ACCEPT, raw_getc_interruptible() alone
  // will notice g_sigint_flag and make it throw "User interrupt" itself -
  // read_char() only calls it once fin_cb() is already blocked inside that
  // read(), so the core's own interrupt_requested check in KEY/ACCEPT (which
  // only runs between polls) never gets a chance to observe or consume it.
  // Arming interrupt_requested here as well would leave it set, so it would
  // fire a second time on whatever word runs next. Only forward to the core
  // when nothing is already about to consume this interrupt.
  if (!forth_waiting_input()) forth_request_interrupt();
}

static const char* PROMPT = "> ";
static const size_t HISTORY_LIMIT = 200;
static std::vector<std::string> g_history;

// Reads a single already-processed byte from the terminal: CR is folded into
// LF and DEL is folded into BS so callers only ever deal with one spelling of
// each. Retries on EINTR so a Ctrl+C at the prompt doesn't look like EOF.
static int raw_getc()
{
  unsigned char c;
  ssize_t n;
  do {
    n = read(STDIN_FILENO, &c, 1);
  } while (n < 0 && errno == EINTR);
  if (n <= 0) {
    input_closed = true;
    return -1;
  }
  if (c == '\r') c = '\n';
  if (c == 127) c = 8;
  return c;
}

// Same byte read as raw_getc(), but for spots where a running Forth word is
// blocked waiting on a key (KEY / ACCEPT): a Ctrl+C here should interrupt
// that wait immediately rather than being swallowed like it is at the
// prompt. INPUT_BREAK doubles as both "input closed" and "user interrupt" -
// callers already treat the two identically.
static int raw_getc_interruptible()
{
  unsigned char c;
  for (;;) {
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n > 0) break;
    if (n < 0 && errno == EINTR) {
      if (g_sigint_flag) {
        g_sigint_flag = 0;
        return INPUT_BREAK;
      }
      continue;
    }
    input_closed = true;
    return INPUT_BREAK;
  }
  if (c == '\r') c = '\n';
  if (c == 127) c = 8;
  return c;
}

enum Key {
  KEY_NONE = 0,
  KEY_IGNORE,
  KEY_UP,
  KEY_DOWN,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_HOME,
  KEY_END,
  KEY_DELETE,
};

// Reads one logical key: either a plain byte (returned via plain_char with
// KEY_NONE) or a recognized escape sequence. Unknown CSI sequences (e.g. the
// 200~/201~ bracketed-paste markers some terminals send around pasted text)
// are swallowed as KEY_IGNORE instead of leaking their bytes into the buffer.
static int read_key(int& plain_char)
{
  int c = raw_getc();
  if (c < 0) return -1;
  if (c != 0x1B) {
    plain_char = c;
    return KEY_NONE;
  }

  int c1 = raw_getc();
  if (c1 < 0) return -1;
  if (c1 != '[' && c1 != 'O') {
    plain_char = 0x1B;
    return KEY_NONE;
  }

  int c2 = raw_getc();
  if (c2 < 0) return -1;

  if (c2 >= '0' && c2 <= '9') {
    std::string num;
    num += (char)c2;
    int c3 = c2;
    while (c3 >= '0' && c3 <= '9') {
      c3 = raw_getc();
      if (c3 < 0) return -1;
      if (c3 >= '0' && c3 <= '9') num += (char)c3;
    }
    if (num == "3") return KEY_DELETE;
    if (num == "1" || num == "7") return KEY_HOME;
    if (num == "4" || num == "8") return KEY_END;
    return KEY_IGNORE;
  }

  switch (c2) {
  case 'A':
    return KEY_UP;
  case 'B':
    return KEY_DOWN;
  case 'C':
    return KEY_RIGHT;
  case 'D':
    return KEY_LEFT;
  case 'H':
    return KEY_HOME;
  case 'F':
    return KEY_END;
  default:
    return KEY_IGNORE;
  }
}

struct LineEditor {
  std::string buf;
  size_t cursor = 0;
  int hist_pos = -1;
  std::string saved;

  void redraw() const
  {
    std::cout << "\r" << PROMPT << buf << "\x1b[K";
    size_t back = buf.size() - cursor;
    if (back > 0) std::cout << "\x1b[" << back << "D";
    std::cout.flush();
  }

  void insert(char c)
  {
    buf.insert(buf.begin() + cursor, c);
    cursor++;
    redraw();
  }

  void backspace()
  {
    if (cursor == 0) return;
    buf.erase(cursor - 1, 1);
    cursor--;
    redraw();
  }

  void del_forward()
  {
    if (cursor >= buf.size()) return;
    buf.erase(cursor, 1);
    redraw();
  }

  void kill_to_end()
  {
    buf.erase(cursor);
    redraw();
  }

  void kill_line()
  {
    buf.clear();
    cursor = 0;
    redraw();
  }

  void kill_word_back()
  {
    if (cursor == 0) return;
    size_t end = cursor;
    size_t i = cursor;
    while (i > 0 && isspace((unsigned char)buf[i - 1]))
      i--;
    while (i > 0 && !isspace((unsigned char)buf[i - 1]))
      i--;
    buf.erase(i, end - i);
    cursor = i;
    redraw();
  }

  void move_left()
  {
    if (cursor == 0) return;
    cursor--;
    redraw();
  }

  void move_right()
  {
    if (cursor >= buf.size()) return;
    cursor++;
    redraw();
  }

  void move_home()
  {
    cursor = 0;
    redraw();
  }

  void move_end()
  {
    cursor = buf.size();
    redraw();
  }

  void history_prev()
  {
    if (g_history.empty()) return;
    if (hist_pos == -1) {
      saved = buf;
      hist_pos = (int)g_history.size();
    }
    if (hist_pos == 0) return;
    hist_pos--;
    buf = g_history[hist_pos];
    cursor = buf.size();
    redraw();
  }

  void history_next()
  {
    if (hist_pos == -1) return;
    hist_pos++;
    if (hist_pos >= (int)g_history.size()) {
      hist_pos = -1;
      buf = saved;
    }
    else {
      buf = g_history[hist_pos];
    }
    cursor = buf.size();
    redraw();
  }
};

static void push_history(const std::string& line)
{
  if (line.empty()) return;
  if (!g_history.empty() && g_history.back() == line) return;
  g_history.push_back(line);
  if (g_history.size() > HISTORY_LIMIT) g_history.erase(g_history.begin());
}

// Runs the full editing loop for one input line: cursor movement, backspace
// / delete, history recall, and common readline-style Ctrl bindings. Returns
// once Enter is pressed (or input closes).
static std::string edit_line()
{
  LineEditor ed;
  for (;;) {
    int plain = 0;
    int key = read_key(plain);
    if (key == -1) return ed.buf;

    if (key == KEY_IGNORE) continue;

    if (key == KEY_NONE) {
      switch (plain) {
      case '\n':
        std::cout << "\r\n";
        std::cout.flush();
        push_history(ed.buf);
        return ed.buf;
      case 8:
        ed.backspace();
        break;
      case 1:
        ed.move_home();
        break; // Ctrl-A
      case 5:
        ed.move_end();
        break; // Ctrl-E
      case 2:
        ed.move_left();
        break; // Ctrl-B
      case 6:
        ed.move_right();
        break; // Ctrl-F
      case 21:
        ed.kill_line();
        break; // Ctrl-U
      case 11:
        ed.kill_to_end();
        break; // Ctrl-K
      case 23:
        ed.kill_word_back();
        break; // Ctrl-W
      case 12: // Ctrl-L: redraw
        std::cout << "\x1b[2J\x1b[H";
        ed.redraw();
        break;
      default:
        if (plain >= 32) ed.insert((char)plain);
        break;
      }
      continue;
    }

    switch (key) {
    case KEY_UP:
      ed.history_prev();
      break;
    case KEY_DOWN:
      ed.history_next();
      break;
    case KEY_LEFT:
      ed.move_left();
      break;
    case KEY_RIGHT:
      ed.move_right();
      break;
    case KEY_HOME:
      ed.move_home();
      break;
    case KEY_END:
      ed.move_end();
      break;
    case KEY_DELETE:
      ed.del_forward();
      break;
    default:
      break;
    }
  }
}

// Bridges our line editor into rForth's char-at-a-time input_hook contract.
// While the Forth VM itself is waiting on raw keys (KEY / ACCEPT, used by
// interactive programs), bytes are passed straight through unedited so
// arrow keys etc. reach the running program instead of being intercepted.
static int read_char()
{
  static std::string pending;
  static size_t pending_pos = 0;

  if (pending_pos < pending.size()) return (unsigned char)pending[pending_pos++];
  pending.clear();
  pending_pos = 0;

  if (forth_waiting_input()) {
    int c = raw_getc_interruptible();
    if (c == INPUT_BREAK) return INPUT_BREAK;
    if (c == '\n')
      std::cout << "\r\n";
    else if (c == 8)
      std::cout << "\b \b";
    else
      std::cout << (char)c;
    std::cout.flush();
    return c;
  }

  std::string line = edit_line();
  if (input_closed) return INPUT_BREAK;

  pending = line + "\n";
  pending_pos = 1;
  return (unsigned char)pending[0];
}

int main()
{
  forth_init();
  forth_dict_add(words, sizeof(words) / sizeof(Code));
  greet();
  enable_raw_terminal();

  // signal() installs SA_RESTART on Linux, which would make an interrupted
  // blocking read() silently resume instead of returning EINTR - and
  // raw_getc_interruptible() relies on that EINTR to notice Ctrl+C while a
  // Forth word is blocked in KEY/ACCEPT. sigaction() lets us opt out of that.
  struct sigaction sa {};
  sa.sa_handler = handle_sigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);

  auto rsp_to_con = [](int len, const char* rst) {
    std::cout.write(rst, len);
    std::cout.flush();
  };

  std::cout << PROMPT;
  std::cout.flush();
  while (!input_closed) {
    int r = forth_vm(read_char, rsp_to_con);
    if (input_closed) break;
    if (r == 0) {
      std::cout << PROMPT;
      std::cout.flush();
    }
  }
  return 0;
}
