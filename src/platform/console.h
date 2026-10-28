#pragma once
// Console/stream output. Text goes to the console through WriteConsoleW (full Unicode
// regardless of code page) and to files/pipes as raw UTF-8. Colors use classic
// SetConsoleTextAttribute; ANSI escape sequences are never emitted.
#include <windows.h>

#include <string>

namespace sm {

enum class Color { Default, Dim, Title, Accent, Good, Warn, Bad, Header };

bool handleIsConsole(HANDLE h);

class Out {
 public:
  // stdHandle: STD_OUTPUT_HANDLE or STD_ERROR_HANDLE.
  Out(DWORD stdHandle, bool allowColor);
  // Writes to an already opened file handle (not owned). Never colored.
  explicit Out(HANDLE file);
  ~Out();
  Out(const Out&) = delete;
  Out& operator=(const Out&) = delete;

  bool isConsole() const { return console_; }
  bool colorEnabled() const { return color_; }
  bool failed() const { return failed_; }

  void write(const std::string& utf8);
  void line(const std::string& utf8 = std::string()) {
    write(utf8);
    write("\r\n");
  }
  void color(Color c);
  void reset();
  // Writes text in a color and restores the default afterwards.
  void colored(Color c, const std::string& utf8) {
    color(c);
    write(utf8);
    reset();
  }

 private:
  HANDLE h_ = nullptr;
  bool console_ = false;
  bool color_ = false;
  bool failed_ = false;
  WORD defaultAttr_ = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
};

struct ConsoleCaps {
  bool isConsole = false;
  unsigned outputCodePage = 0;
  std::string fontFace;   // empty when unknown
  bool fontKnown = false;
  bool trueTypeFont = false;
  bool unicodeBlocks = false;  // box drawing + block characters render correctly
  std::string reason;          // why unicodeBlocks was chosen
  short width = 0, height = 0; // visible window size
};

ConsoleCaps detectConsoleCaps(HANDLE out);

}  // namespace sm
