# Command reference

```
sevenmon [command] [arguments] [options]
```

Options may appear before or after the command. Values can be given as `--opt value` or
`--opt=value`. Commands are case-insensitive. `/?` is accepted as `--help`.

## Commands

| Command | Description |
|---------|-------------|
| *(none)*, `summary` | System overview: OS, uptime, CPU, RAM, commit, GPU, fixed volumes, disk and network throughput, battery, process count. |
| `help [command]` | General help or help for one command. |
| `version` | Version (`--json` for a JSON object including architecture). |
| `cpu` | Model, vendor, topology, caches, frequency, total/user/system/idle and per-core utilization. |
| `memory` | Physical RAM, commit charge (RAM + page files), kernel pools, page files, process totals. |
| `gpu` | Display adapters, vendor/device IDs, driver version/date, VRAM, and NVML/ADL sensors. `--verbose` shows backend status. |
| `disk` | Volumes (label, FS, type, size, free, used %, backing disks) and physical disks (model, bus, SSD/HDD, size, throughput, active %, SMART). |
| `network` | Interfaces (status, link speed, throughput, totals, IPv4). `--verbose` adds MAC/IPv6/description, `--all` includes filter/virtual interfaces. |
| `battery` | AC status, charge, charging, Windows' remaining-time estimate, capacities and health when the driver reports them. |
| `processes` | Process table. `--sort cpu|memory|pid|name`, `--top n`, `--filter text`. |
| `process <PID>` | One process: path, parent, CPU %, CPU time, memory, threads, handles, priority, start time, I/O. |
| `sensors` | Every sensor with value, unit, source and an explanation when unavailable. |
| `hardware` | Windows edition/version, computer name, uptime, boot time, SMBIOS system/board/BIOS, CPU/RAM/GPU/disk inventory. |
| `watch` | Interactive dashboard. When stdout is not a console, or with `--json`/`--csv`, streams records instead (see below). |
| `export` | Records samples to a file: CSV (default) or JSON lines (`--json`). |
| `doctor` | Probes every data source and reports `ok`, `limited`, `unavailable`, `warning` or `info` with the reason. |

## Options

| Option | Applies to | Meaning |
|--------|-----------|---------|
| `--json` | all | JSON. One-shot commands print one pretty-printed document; `watch`/`export` write one compact object per line (NDJSON). |
| `--csv` | all | CSV (RFC 4180, CRLF line endings, header row). |
| `--no-color` | text, dashboard | No console colors. |
| `--plain` | text, dashboard | ASCII only and no colors (for raster fonts or screen readers). |
| `--interval <ms>` | all | Sampling interval, 100..3600000. Default 500 ms for one-shot commands (the measurement window for rates), 1000 ms for `watch`/`export`. |
| `--count <n>` | `watch`, `export` | Number of samples (dashboard: refreshes) before exiting. |
| `--duration <s>` | `watch`, `export` | Sampling duration; converted to `ceil(duration*1000/interval)` samples. |
| `--sort <key>` | `processes`, `watch` | `cpu` (default), `memory` (working set), `pid`, `name`. |
| `--top <n>` | `processes` | Limit the number of processes shown. |
| `--filter <text>` | `processes`, `watch` | Case-insensitive substring match on the process name. |
| `--output <path>`, `-o` | all | Write output to a file instead of stdout (UTF-8, paths with spaces and non-ASCII characters are supported). For `export` this may be a directory. |
| `--append` | with `--output` | Append instead of replacing. For CSV the header is written only if the file is empty. |
| `--all` | `network` | Include filter, loopback and down virtual interfaces. |
| `--verbose`, `-v` | text | Additional details and diagnostic notes. |
| `--help`, `-h`, `--version`, `-V` | | |

Invalid combinations are rejected with exit code 2, for example `--json --csv`,
`--count` on a one-shot command, `--sort` on `cpu`, or `--append` without `--output`.

## How one-shot rates are measured

Commands that report rates take a first sample, collect slow sources (WMI, GPU
libraries, volumes), wait for the remainder of `--interval`, take a second sample and
report the difference. With the default 500 ms the command finishes in about half a
second plus WMI time. Longer intervals give steadier averages.

## `watch`: interactive vs. streaming

* **Console + text format**: the interactive dashboard. `--count`/`--duration` make it
  exit automatically (useful for demos and tests).
* **Redirected stdout, `--json` or `--csv`**: streaming. One record is written per
  interval: NDJSON for `--json`, CSV rows (header once) for `--csv`, and a one-line text
  summary when redirected without a format. The first record is written after one full
  interval so it already contains real rates. Streaming runs until `--count`/`--duration`
  is reached, Ctrl+C is pressed, or the reader closes the pipe. `--output` is allowed
  with `--json`/`--csv`.

`export` is the file-oriented variant: it always writes to a file (generated name
`sevenmon-YYYYMMDD-HHMMSS.csv`/`.jsonl` in the current directory or in the directory
given by `--output`), defaults to CSV and one sample, prints progress on stderr when
stderr is a console, and reports the file name at the end.

Both use the same fixed-column record (`jsonSample`, see json-schema.md), so a CSV file
keeps identical columns across runs and appends.

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | Success (also when the dashboard or a `watch` stream is ended with Ctrl+C or Q). |
| 1 | Runtime failure (for example the dashboard could not access the console). |
| 2 | Invalid command line. |
| 3 | Not found (`process <PID>` for a PID that does not exist or exited). |
| 4 | Output could not be written (`--output` path invalid, disk full). A `watch` stream whose reader closes the pipe ends normally with 0. |
| 130 | `export` interrupted with Ctrl+C before `--count`/`--duration` was reached (the file is complete up to the last full record). |

## Dashboard keys

| Key | Action |
|-----|--------|
| `Q`, `Ctrl+C` | Quit and restore the console |
| `R` | Sample now and refresh slow sources |
| `O` / `1` | Overview |
| `P` | Process list |
| `C`, `M`, `G`, `D`, `N` | CPU, memory, GPU, disk, network views |
| `H`, `?` | Help |
| `Up`, `Down`, `PgUp`, `PgDn`, `Home`, `End` | Move the process selection (the selection follows the PID when the list re-sorts) |
| `Enter` | Details of the selected process |
| `S` | Cycle sort: cpu, memory, pid, name |
| `/` | Edit the name filter (Enter apply, Esc cancel); a PID also matches |
| `K` | Terminate the selected/displayed process; requires `Y` to confirm, anything else cancels. Protected processes report "Access is denied". |
| `+`, `-` | Slower / faster refresh (250 ms .. 60 s) |
| `Space` | Pause / resume sampling |
| `Esc` | Back (detail -> list, clear filter, otherwise overview) |

Process termination exists only in the dashboard and only behind the `Y/N`
confirmation. There is no command-line kill.

## Output encoding

* Console: Unicode through `WriteConsoleW`, independent of the code page. Characters the
  console font lacks are shown as `?` by Windows.
* Redirected output and files: UTF-8 without BOM. Note that `type file.txt` in a
  Windows 7 console interprets files in the OEM code page; use `--plain`-friendly
  ASCII data or open the file in an editor if names contain non-ASCII characters.
