#pragma once
// WMI access through COM. Queries are semisynchronous with a per-call deadline so a
// slow or hung provider can never stall the caller for longer than the timeout. In the
// interactive dashboard WMI is only used from the slow sampling thread.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct IWbemServices;

namespace sm {

struct WmiValue {
  enum class Kind { Null, Bool, Int, UInt, Double, String, Bytes } kind = Kind::Null;
  bool b = false;
  int64_t i = 0;
  uint64_t u = 0;
  double d = 0;
  std::string s;
  std::vector<uint8_t> bytes;

  bool isNull() const { return kind == Kind::Null; }
  // Numeric view (strings holding integers, as WMI returns uint64 as strings).
  bool toUInt(uint64_t& out) const;
};

using WmiRow = std::map<std::string, WmiValue>;

// Initializes COM (MTA) for the current thread for its lifetime.
class ComScope {
 public:
  ComScope();
  ~ComScope();
  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;
  bool ok() const { return ok_; }

 private:
  bool ok_ = false;
  bool uninit_ = false;
};

// Must be called once early in main (process-wide COM security for WMI).
void initComSecurity();

class WmiSession {
 public:
  WmiSession() = default;
  ~WmiSession();
  WmiSession(const WmiSession&) = delete;
  WmiSession& operator=(const WmiSession&) = delete;

  // ns: e.g. L"ROOT\\CIMV2" or L"ROOT\\WMI"
  bool connect(const wchar_t* ns, std::string* error);
  bool connected() const { return svc_ != nullptr; }
  bool query(const wchar_t* wql, std::vector<WmiRow>& rows, std::string* error, unsigned timeoutMs = 4000);

 private:
  IWbemServices* svc_ = nullptr;
};

// Translates WMI HRESULTs into human-readable, honest explanations.
std::string wmiErrorText(long hr);

}  // namespace sm
