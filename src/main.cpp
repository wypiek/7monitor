// sevenmon.exe entry point.
#include <winsock2.h>
#include <windows.h>

#include <string>
#include <vector>

#include "app/cli.h"
#include "app/commands.h"
#include "platform/console.h"
#include "platform/interrupt.h"
#include "platform/wmi.h"
#include "util/strings.h"

int wmain(int argc, wchar_t** argv) {
  // Never show "insert a disk" style system dialogs from a console tool.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) args.push_back(sm::toUtf8(std::wstring(argv[i])));
  sm::ParseResult parsed = sm::parseCli(args);
  if (!parsed.ok) {
    sm::Out err(STD_ERROR_HANDLE, false);
    err.line("sevenmon: " + parsed.error);
    err.line("Run 'sevenmon help' for usage.");
    return sm::kExitUsage;
  }

  sm::ComScope com;
  sm::initComSecurity();
  WSADATA wsa;
  bool wsaOk = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;  // only for address formatting; no sockets are opened
  sm::installInterruptHandler();

  int rc = sm::runCommand(parsed.opts);

  if (wsaOk) WSACleanup();
  return rc;
}
