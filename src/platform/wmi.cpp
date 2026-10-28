#include "platform/wmi.h"

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <wbemidl.h>

#include <cstdio>

#include "platform/win.h"
#include "util/strings.h"

namespace sm {

bool WmiValue::toUInt(uint64_t& out) const {
  switch (kind) {
    case Kind::UInt: out = u; return true;
    case Kind::Int:
      if (i < 0) return false;
      out = static_cast<uint64_t>(i);
      return true;
    case Kind::String: {
      if (s.empty()) return false;
      uint64_t v = 0;
      for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
      }
      out = v;
      return true;
    }
    default: return false;
  }
}

ComScope::ComScope() {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(hr)) {
    ok_ = true;
    uninit_ = true;
  } else if (hr == RPC_E_CHANGED_MODE) {
    ok_ = true;  // already initialized as STA by someone else; usable
  }
}

ComScope::~ComScope() {
  if (uninit_) CoUninitialize();
}

void initComSecurity() {
  // RPC_E_TOO_LATE is fine (already set). Failures only reduce WMI availability.
  CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                       EOAC_NONE, nullptr);
}

std::string wmiErrorText(long hr) {
  switch (static_cast<unsigned long>(hr)) {
    case 0x80041003ul: return "access denied (administrator rights are usually required)";
    case 0x80041010ul: return "WMI class not available on this system";
    case 0x8004100Cul: return "not supported by the installed driver or hardware";
    case 0x8004100Eul: return "WMI namespace not available";
    case 0x80041013ul: return "WMI provider failed to load";
    case 0x80070005ul: return "access denied";
    case 0x00040004ul: return "WMI query timed out";
  }
  char buf[64];
  std::snprintf(buf, sizeof(buf), "WMI error 0x%08lX", static_cast<unsigned long>(hr));
  return buf;
}

WmiSession::~WmiSession() {
  if (svc_) svc_->Release();
}

bool WmiSession::connect(const wchar_t* ns, std::string* error) {
  if (svc_) {
    svc_->Release();
    svc_ = nullptr;
  }
  IWbemLocator* loc = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                reinterpret_cast<void**>(&loc));
  if (FAILED(hr) || !loc) {
    if (error) *error = "WMI locator unavailable (" + wmiErrorText(hr) + ")";
    return false;
  }
  BSTR bns = SysAllocString(ns);
  IWbemServices* svc = nullptr;
  hr = loc->ConnectServer(bns, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &svc);
  SysFreeString(bns);
  loc->Release();
  if (FAILED(hr) || !svc) {
    if (error) *error = wmiErrorText(hr);
    return false;
  }
  CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                    RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
  svc_ = svc;
  return true;
}

static WmiValue convert(const VARIANT& v) {
  WmiValue out;
  switch (v.vt) {
    case VT_BOOL: out.kind = WmiValue::Kind::Bool; out.b = v.boolVal != VARIANT_FALSE; break;
    case VT_I1: out.kind = WmiValue::Kind::Int; out.i = v.cVal; break;
    case VT_I2: out.kind = WmiValue::Kind::Int; out.i = v.iVal; break;
    case VT_I4: out.kind = WmiValue::Kind::Int; out.i = v.lVal; break;
    case VT_I8: out.kind = WmiValue::Kind::Int; out.i = v.llVal; break;
    case VT_UI1: out.kind = WmiValue::Kind::UInt; out.u = v.bVal; break;
    case VT_UI2: out.kind = WmiValue::Kind::UInt; out.u = v.uiVal; break;
    case VT_UI4: out.kind = WmiValue::Kind::UInt; out.u = v.ulVal; break;
    case VT_UI8: out.kind = WmiValue::Kind::UInt; out.u = v.ullVal; break;
    case VT_R4: out.kind = WmiValue::Kind::Double; out.d = v.fltVal; break;
    case VT_R8: out.kind = WmiValue::Kind::Double; out.d = v.dblVal; break;
    case VT_BSTR:
      out.kind = WmiValue::Kind::String;
      if (v.bstrVal) out.s = toUtf8(std::wstring(v.bstrVal, SysStringLen(v.bstrVal)));
      break;
    default:
      if ((v.vt & VT_ARRAY) && (v.vt & VT_TYPEMASK) == VT_UI1 && v.parray) {
        SAFEARRAY* sa = v.parray;
        LONG lo = 0, hi = -1;
        SafeArrayGetLBound(sa, 1, &lo);
        SafeArrayGetUBound(sa, 1, &hi);
        void* data = nullptr;
        if (hi >= lo && SUCCEEDED(SafeArrayAccessData(sa, &data))) {
          const uint8_t* p = static_cast<const uint8_t*>(data);
          out.kind = WmiValue::Kind::Bytes;
          out.bytes.assign(p, p + (hi - lo + 1));
          SafeArrayUnaccessData(sa);
        }
      }
      break;
  }
  return out;
}

bool WmiSession::query(const wchar_t* wql, std::vector<WmiRow>& rows, std::string* error, unsigned timeoutMs) {
  rows.clear();
  if (!svc_) {
    if (error) *error = "WMI not connected";
    return false;
  }
  BSTR lang = SysAllocString(L"WQL");
  BSTR q = SysAllocString(wql);
  IEnumWbemClassObject* en = nullptr;
  HRESULT hr = svc_->ExecQuery(lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
  SysFreeString(lang);
  SysFreeString(q);
  if (FAILED(hr) || !en) {
    if (error) *error = wmiErrorText(hr);
    return false;
  }
  CoSetProxyBlanket(en, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                    RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
  double deadline = win::monotonicSeconds() + timeoutMs / 1000.0;
  bool ok = true;
  for (;;) {
    double remaining = deadline - win::monotonicSeconds();
    if (remaining <= 0) {
      if (error) *error = "WMI query timed out";
      ok = false;
      break;
    }
    IWbemClassObject* obj = nullptr;
    ULONG got = 0;
    hr = en->Next(static_cast<long>(remaining * 1000.0) + 1, 1, &obj, &got);
    if (hr == WBEM_S_TIMEDOUT) {
      if (error) *error = "WMI query timed out";
      ok = false;
      break;
    }
    if (FAILED(hr)) {
      if (error) *error = wmiErrorText(hr);
      ok = false;
      break;
    }
    if (got == 0 || !obj) break;  // WBEM_S_FALSE: end of results
    WmiRow row;
    if (SUCCEEDED(obj->BeginEnumeration(WBEM_FLAG_NONSYSTEM_ONLY))) {
      BSTR name = nullptr;
      VARIANT val;
      VariantInit(&val);
      while (obj->Next(0, &name, &val, nullptr, nullptr) == WBEM_S_NO_ERROR) {
        if (name) {
          row[toUtf8(std::wstring(name, SysStringLen(name)))] = convert(val);
          SysFreeString(name);
          name = nullptr;
        }
        VariantClear(&val);
      }
      obj->EndEnumeration();
    }
    obj->Release();
    rows.push_back(std::move(row));
  }
  en->Release();
  return ok;
}

}  // namespace sm
