# Machine-readable output (schema version 1)

All JSON is UTF-8. Numbers are plain JSON numbers (no units in strings); bytes are
integers, rates are bytes per second, percentages are 0..100 with one decimal.
**A missing measurement is always `null`**, never `0` or a placeholder. Field order is
stable. New fields may be added in later versions without changing `schema_version`;
removing or renaming a field increments it.

## Envelope (one-shot commands with `--json`)

```json
{
  "schema_version": 1,
  "tool": "sevenmon",
  "tool_version": "1.0.0",
  "type": "cpu",
  "timestamp": "2026-10-28T07:24:38Z",
  "data": { ... }
}
```

`timestamp` is UTC (ISO 8601) of the last sample. `type` is the command name
(`summary`, `cpu`, `memory`, `gpu`, `disk`, `network`, `battery`, `processes`,
`process`, `sensors`, `hardware`, `doctor`).

## `data` by type

### `cpu`
| Field | Type | Notes |
|-------|------|-------|
| `vendor`, `brand` | string | CPUID |
| `logical_processors` | int | all processor groups |
| `physical_cores`, `packages` | int/null | GetLogicalProcessorInformation |
| `l2_cache_kib`, `l3_cache_kib` | int/null | |
| `max_mhz`, `current_mhz` | int/null | power manager; turbo not reflected (`current_mhz_source`) |
| `usage.total_pct`, `usage.user_pct`, `usage.system_pct`, `usage.idle_pct` | number/null | over the sampling interval |
| `per_core_pct` | array of number/null | index = logical processor (group 0) |
| `temperature_c` | null | always null; see `temperature_note` |
| `notes` | array of string | |

### `memory`
`physical.{total_bytes, available_bytes, used_bytes, used_pct}`,
`commit.{total_bytes, limit_bytes, peak_bytes, used_pct}`,
`kernel.{paged_pool_bytes, nonpaged_pool_bytes}`, `system_cache_bytes`,
`page_files` (array of `{path, total_bytes, in_use_bytes, peak_bytes}` or null if
unknown), `processes.{count, threads, handles, working_set_total_bytes,
private_total_bytes}` (sums over processes sevenmon may open), `notes`.

### `gpu`
`adapters[]`: `name, vendor, vendor_id ("0x10DE"), device_id, software_adapter,
detection ("DXGI 1.1"|"EnumDisplayDevices"), driver_version, driver_date,
driver_provider, dedicated_video_memory_bytes, shared_system_memory_bytes,
sensor_backend ("NVML"|"ADL"|"none"), utilization_pct, memory_controller_pct,
vram_used_bytes, vram_total_bytes, temperature_c, core_clock_mhz, memory_clock_mhz,
power_w, notes[]`. `backends[]`: `{name, available, status}`. `notes[]`.

### `disk`
`volumes[]`: `kind ("volume"), root ("C:\\"), label, file_system, drive_type
(fixed|removable|network|cdrom|ramdisk|unknown), ready, total_bytes, free_bytes,
available_to_user_bytes, used_pct, physical_disks[] (disk numbers), note`.

`physical_disks[]`: `kind ("physical_disk"), number, model, bus_type, removable,
size_bytes, media_type (SSD|HDD|unknown), media_type_source, read_bytes_per_sec,
write_bytes_per_sec, reads_per_sec, writes_per_sec, active_pct,
smart_predict_failure (bool/null), smart_temperature_c, smart_note, perf_note`.

`smart_status`: explanation when SMART is unavailable.

### `network`
`interfaces[]`: `name, description, type (ethernet|wifi|loopback|tunnel|ppp|mobile
broadband|...), status, connected, hardware, mac ("00-11-22-33-44-55"), ipv4[], ipv6[],
link_speed_rx_bps, link_speed_tx_bps (bits/s), rx_bytes_total, tx_bytes_total,
rx_bytes_per_sec, tx_bytes_per_sec, rate_note ("first sample"|"counter reset"|null)`.
`totals.{rx_bytes_per_sec, tx_bytes_per_sec, interfaces_up}`.

### `battery`
`present, ac_online, charging` (bool/null), `charge_pct`,
`estimated_seconds_remaining` (Windows' estimate, see `estimate_source`),
`design_capacity_mwh, full_charge_capacity_mwh, health_pct, health_note, notes[]`.

### `processes`
`total_processes, shown, sort, cpu_pct_basis` and `processes[]` with the fields of
`process` below.

### `process`
`pid, parent_pid, name, cpu_pct (share of all logical CPUs), cpu_time_seconds,
working_set_bytes, peak_working_set_bytes, private_bytes, threads, handles,
priority_class, base_priority, start_time_utc, io_read_bytes, io_write_bytes, path,
access_denied`. Fields that need a process handle are `null` when access is denied.

### `sensors`
`sensors[]`: `name, kind (temperature|load|clock|power), value, unit (C|%|MHz|W),
source (NVML|ADL|SMART (WMI)|ACPI thermal zone (WMI)|none), note`.
CPU core temperature always appears with `value: null` and an explanation; ACPI
thermal zones carry the note "ACPI thermal zone, not CPU core temperature".

### `hardware`
`os.{name, edition, service_pack, version, build, architecture, is_windows_7,
out_of_support}, process_architecture, wow64, computer_name, uptime_seconds,
boot_time_utc, boot_time_source, elevated`,
`firmware.{smbios_version, system_manufacturer, system_product, system_version,
board_manufacturer, board_product, board_version, bios_vendor, bios_version,
bios_date, note}`, `cpu.{brand, vendor, logical_processors, physical_cores, packages,
max_mhz}`, `memory_total_bytes`, `gpus[]`, `disks[]`. OEM placeholder strings such as
"To Be Filled By O.E.M." are reported as `null`.

### `summary`
`system` (as in hardware, without firmware), `cpu.{brand, logical_processors,
physical_cores, usage_pct, current_mhz}`, `memory.{total_bytes, used_bytes, used_pct,
commit_total_bytes, commit_limit_bytes}`, `gpus[]{name, utilization_pct,
temperature_c}`, `volumes[]` (fixed, ready), `io.{disk_read_bytes_per_sec,
disk_write_bytes_per_sec, net_rx_bytes_per_sec, net_tx_bytes_per_sec, interfaces_up}`,
`battery.{present, charge_pct, ac_online}`, `process_count`.

### `doctor`
`checks[]`: `{area, check, status (ok|limited|unavailable|warning|info), detail}`.

## Streaming record (`watch --json`, `export --json`)

One JSON object per line (NDJSON), no envelope:

```json
{"schema_version":1,"type":"sample","timestamp":"2026-10-28T07:24:48Z","cpu_total_pct":2.4,"cpu_user_pct":1.6,"cpu_system_pct":0.8,"cpu_mhz":2100,"mem_total_bytes":16872804352,"mem_used_bytes":611336192,"mem_used_pct":3.6,"commit_total_bytes":611344384,"commit_limit_bytes":16872812544,"commit_used_pct":3.6,"disk_read_bytes_per_sec":null,"disk_write_bytes_per_sec":null,"net_rx_bytes_per_sec":2676,"net_tx_bytes_per_sec":1702,"gpu0_utilization_pct":null,"gpu0_temperature_c":null,"process_count":8,"battery_charge_pct":null,"ac_online":true,"uptime_seconds":1939}
```

(Example captured under Wine, where disk counters are not implemented - hence `null`.)

`disk_*` and `net_*` are sums over physical disks / reported interfaces;
`gpu0_*` is the first adapter that has a reading.

## CSV

CSV output is produced by flattening the same JSON objects: nested object keys are
joined with `.` (`rate.rx`), arrays of scalars are joined with `;` and `null` becomes an
empty field. Which objects become rows:

| Command | Rows |
|---------|------|
| `summary`, `watch --csv`, `export --csv` | one streaming record (fixed columns listed above) |
| `cpu` | `core,usage_pct` with a `total` row followed by one row per logical processor |
| `memory`, `battery`, `process`, `hardware` | one row with the flattened `data` object (nested arrays of objects such as page files are omitted; use JSON for them) |
| `gpu` | one row per adapter |
| `disk` | volumes then physical disks; the `kind` column tells them apart (the header is the union of both) |
| `network` | one row per interface |
| `processes` | one row per process |
| `sensors` | one row per sensor |
| `doctor` | one row per check |
