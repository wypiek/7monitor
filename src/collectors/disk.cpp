#include "collectors/disk.h"

#include <windows.h>
#include <winioctl.h>

#include <cstring>

#include "platform/win.h"
#include "util/rates.h"
#include "util/strings.h"

namespace sm {

std::string busTypeName(int t) {
  switch (t) {
    case 1: return "SCSI";
    case 2: return "ATAPI";
    case 3: return "ATA";
    case 4: return "IEEE 1394";
    case 5: return "SSA";
    case 6: return "Fibre Channel";
    case 7: return "USB";
    case 8: return "RAID";
    case 9: return "iSCSI";
    case 10: return "SAS";
    case 11: return "SATA";
    case 12: return "SD";
    case 13: return "MMC";
    case 14: return "Virtual";
    case 15: return "File-backed virtual";
    case 16: return "Storage Spaces";
    case 17: return "NVMe";
    default: return "Unknown";
  }
}

namespace {

std::wstring physicalDrivePath(unsigned n) { return L"\\\\.\\PhysicalDrive" + std::to_wstring(n); }

win::Handle openNoAccess(const std::wstring& path) {
  // Zero desired access: no read/write rights requested, works for standard users.
  HANDLE h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  return win::Handle(h == INVALID_HANDLE_VALUE ? nullptr : h);
}

std::string descriptorString(const std::vector<unsigned char>& buf, DWORD offset) {
  if (offset == 0 || offset >= buf.size()) return std::string();
  const char* p = reinterpret_cast<const char*>(buf.data() + offset);
  size_t max = buf.size() - offset;
  return sanitize(trim(std::string(p, strnlen(p, max))));
}

bool readPerf(HANDLE h, DiskPerfRaw& raw) {
  DISK_PERFORMANCE dp;
  DWORD ret = 0;
  memset(&dp, 0, sizeof(dp));
  if (!DeviceIoControl(h, IOCTL_DISK_PERFORMANCE, nullptr, 0, &dp, sizeof(dp), &ret, nullptr)) return false;
  raw.ok = true;
  raw.bytesRead = static_cast<uint64_t>(dp.BytesRead.QuadPart);
  raw.bytesWritten = static_cast<uint64_t>(dp.BytesWritten.QuadPart);
  raw.readCount = dp.ReadCount;
  raw.writeCount = dp.WriteCount;
  raw.idleTime = static_cast<uint64_t>(dp.IdleTime.QuadPart);
  raw.queryTime = static_cast<uint64_t>(dp.QueryTime.QuadPart);
  raw.t = win::monotonicSeconds();
  return true;
}

// Local definitions (identical layout to the SDK) so the code builds with SDKs that
// predate the Windows 7 additions.
struct SeekPenaltyDescriptor {
  DWORD Version;
  DWORD Size;
  BOOLEAN IncursSeekPenalty;
};

}  // namespace

std::vector<VolumeInfo> queryVolumes() {
  std::vector<VolumeInfo> out;
  wchar_t drives[512];
  DWORD n = GetLogicalDriveStringsW(511, drives);
  if (n == 0 || n > 511) return out;
  // Never let Windows show "There is no disk in the drive" dialogs.
  UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
  for (const wchar_t* p = drives; *p; p += wcslen(p) + 1) {
    VolumeInfo v;
    v.root = toUtf8(std::wstring(p));
    switch (GetDriveTypeW(p)) {
      case DRIVE_FIXED: v.driveType = "fixed"; break;
      case DRIVE_REMOVABLE: v.driveType = "removable"; break;
      case DRIVE_REMOTE: v.driveType = "network"; break;
      case DRIVE_CDROM: v.driveType = "cdrom"; break;
      case DRIVE_RAMDISK: v.driveType = "ramdisk"; break;
      default: v.driveType = "unknown"; break;
    }
    wchar_t label[MAX_PATH + 1] = {}, fs[MAX_PATH + 1] = {};
    if (GetVolumeInformationW(p, label, MAX_PATH, nullptr, nullptr, nullptr, fs, MAX_PATH)) {
      v.ready = true;
      v.label = sanitize(toUtf8(std::wstring(label)));
      v.fileSystem = toUtf8(std::wstring(fs));
    } else {
      DWORD e = GetLastError();
      v.note = e == ERROR_NOT_READY ? "no media" : win::errorText(e);
    }
    if (v.ready) {
      ULARGE_INTEGER avail, total, freeAll;
      if (GetDiskFreeSpaceExW(p, &avail, &total, &freeAll)) {
        v.totalBytes = total.QuadPart;
        v.freeBytes = freeAll.QuadPart;
        v.availableToUserBytes = avail.QuadPart;
        if (total.QuadPart) v.usedPct = 100.0 * static_cast<double>(total.QuadPart - freeAll.QuadPart) /
                                        static_cast<double>(total.QuadPart);
      }
    }
    if (v.driveType == "fixed" || v.driveType == "removable") {
      std::wstring dev = L"\\\\.\\" + std::wstring(p, 2);  // \\.\C:
      win::Handle h = openNoAccess(dev);
      if (h.valid()) {
        // A volume may span several disks (dynamic/spanned volumes).
        std::vector<unsigned char> buf(sizeof(VOLUME_DISK_EXTENTS) + 16 * sizeof(DISK_EXTENT));
        DWORD ret = 0;
        if (DeviceIoControl(h.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf.data(),
                            static_cast<DWORD>(buf.size()), &ret, nullptr)) {
          auto* ext = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buf.data());
          for (DWORD i = 0; i < ext->NumberOfDiskExtents && i < 16; ++i) {
            unsigned d = ext->Extents[i].DiskNumber;
            bool dup = false;
            for (unsigned x : v.diskNumbers) dup = dup || x == d;
            if (!dup) v.diskNumbers.push_back(d);
          }
        } else {
          STORAGE_DEVICE_NUMBER sdn;
          if (DeviceIoControl(h.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &sdn, sizeof(sdn), &ret, nullptr) &&
              sdn.DeviceType == FILE_DEVICE_DISK)
            v.diskNumbers.push_back(sdn.DeviceNumber);
        }
      }
    }
    out.push_back(v);
  }
  SetErrorMode(oldMode);
  return out;
}

std::vector<PhysicalDiskInfo> queryPhysicalDisks() {
  std::vector<PhysicalDiskInfo> out;
  unsigned misses = 0;
  for (unsigned n = 0; n < 64 && misses < 8; ++n) {
    win::Handle h = openNoAccess(physicalDrivePath(n));
    if (!h.valid()) {
      ++misses;  // numbering can have gaps after hot-unplug
      continue;
    }
    misses = 0;
    PhysicalDiskInfo d;
    d.number = n;

    STORAGE_PROPERTY_QUERY q;
    memset(&q, 0, sizeof(q));
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    std::vector<unsigned char> buf(1024);
    DWORD ret = 0;
    if (DeviceIoControl(h.get(), IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), buf.data(),
                        static_cast<DWORD>(buf.size()), &ret, nullptr) &&
        ret >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
      buf.resize(ret);
      auto* sd = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buf.data());
      std::string vendor = descriptorString(buf, sd->VendorIdOffset);
      std::string product = descriptorString(buf, sd->ProductIdOffset);
      d.model = trim(vendor.empty() || startsWith(product, vendor) ? product : vendor + " " + product);
      d.busType = busTypeName(static_cast<int>(sd->BusType));
      d.removable = sd->RemovableMedia != FALSE;
    }
    if (d.model.empty()) d.model = "PhysicalDrive" + std::to_string(n);

    DISK_GEOMETRY_EX geo;
    if (DeviceIoControl(h.get(), IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geo, sizeof(geo), &ret, nullptr))
      d.sizeBytes = static_cast<uint64_t>(geo.DiskSize.QuadPart);

    // Seek-penalty query (StorageDeviceSeekPenaltyProperty = 7, added in Windows 7).
    STORAGE_PROPERTY_QUERY sq;
    memset(&sq, 0, sizeof(sq));
    sq.PropertyId = static_cast<STORAGE_PROPERTY_ID>(7);
    sq.QueryType = PropertyStandardQuery;
    SeekPenaltyDescriptor sp;
    memset(&sp, 0, sizeof(sp));
    if (DeviceIoControl(h.get(), IOCTL_STORAGE_QUERY_PROPERTY, &sq, sizeof(sq), &sp, sizeof(sp), &ret, nullptr) &&
        ret >= sizeof(sp)) {
      d.mediaType = sp.IncursSeekPenalty ? "HDD" : "SSD";
      d.mediaTypeSource = "seek penalty descriptor";
      if (d.busType == "USB" || d.busType == "Virtual" || d.busType == "File-backed virtual") {
        d.mediaType = "unknown";
        d.mediaTypeSource = "seek penalty reported, but not reliable for " + d.busType + " devices";
      }
    } else {
      DWORD e = GetLastError();
      d.mediaTypeSource = "seek penalty query failed: " + win::errorText(e);
    }

    if (!readPerf(h.get(), d.raw)) d.perfNote = "IOCTL_DISK_PERFORMANCE failed: " + win::errorText(GetLastError());
    out.push_back(d);
  }
  return out;
}

void computeDiskRates(const DiskPerfRaw& prev, const DiskPerfRaw& cur, PhysicalDiskInfo& out) {
  out.readBps.reset();
  out.writeBps.reset();
  out.activePct.reset();
  out.readIops.reset();
  out.writeIops.reset();
  if (!prev.ok || !cur.ok) return;
  double dt = cur.t - prev.t;
  out.readBps = perSecond(counterDelta64(prev.bytesRead, cur.bytesRead), dt);
  out.writeBps = perSecond(counterDelta64(prev.bytesWritten, cur.bytesWritten), dt);
  out.readIops = perSecond(counterDelta32(prev.readCount, cur.readCount), dt);
  out.writeIops = perSecond(counterDelta32(prev.writeCount, cur.writeCount), dt);
  out.activePct =
      diskActivePercent(counterDelta64(prev.idleTime, cur.idleTime), counterDelta64(prev.queryTime, cur.queryTime));
}

void refreshDiskPerf(std::vector<PhysicalDiskInfo>& disks) {
  for (auto& d : disks) {
    win::Handle h = openNoAccess(physicalDrivePath(d.number));
    DiskPerfRaw cur;
    if (h.valid() && readPerf(h.get(), cur)) {
      computeDiskRates(d.raw, cur, d);
      d.raw = cur;
      d.perfNote.clear();
    } else {
      computeDiskRates(DiskPerfRaw(), DiskPerfRaw(), d);  // clears rates
      d.raw = DiskPerfRaw();
      d.perfNote = h.valid() ? "IOCTL_DISK_PERFORMANCE failed" : "disk no longer present";
    }
  }
}

}  // namespace sm
