# pfSense-pkg-speedtest

Bandwidth test for pfSense, against the [speedtest.net](https://www.speedtest.net/) server network.
It adds a **Diagnostics > Speedtest** page and a `pfspeedtest` shell command.

- Latency, jitter, download and upload, with live progress
- Automatic server selection (lowest latency among the nearest servers) or manual choice
- Multi-WAN: bind the test to a given WAN interface
- History of the last 100 results
- Pure PHP, no binary and no dependency: works on every pfSense architecture

## Installation

From **Diagnostics > Command Prompt** or an SSH shell (option 8):

```sh
fetch -o - https://raw.githubusercontent.com/bdelphin/pfSense-pkg-speedtest/HEAD/install.sh | sh
```

The script builds a regular `pkg` package on the firewall from the port in
[`net/pfSense-pkg-speedtest`](net/pfSense-pkg-speedtest) and installs it with `pkg add`.
The package then shows up in **System > Package Manager > Installed Packages**, and can be
removed from there, with `pkg delete pfSense-pkg-speedtest` or with `sh install.sh uninstall`.
Since it is not in the official repository, the Package Manager flags it in red as
"installed, but not available on remote repository": this is expected.

Running the same command again upgrades the package. The history is kept.

> pfSense reinstalls the packages of its configuration from the official repository after a
> system upgrade. This package is not in that repository yet, so run the installation command
> again after upgrading pfSense.

## Command line

```
# pfspeedtest
ISP:       Free (82.67.106.146)
Server:    Scaleway - Paris (FR), 1 km [id 61933]
Latency:   5.30 ms (jitter 0.30 ms)
Download:  939.59 Mbps
Upload:    945.40 Mbps
```

`pfspeedtest --help` lists the options (`--server`, `--source`, `--list`, `--json`...).
Tests started from the shell show up live on the GUI page and are recorded in the history.

## How it works

The measurement follows the speedtest.net protocol:

1. The nearest servers come from `https://www.speedtest.net/api/js/servers`, located from the
   public IP address of the firewall (of the selected WAN).
2. Latency: 10 `PING` commands over a TCP connection to the server (HTTP fallback).
   Latency is the median, jitter the mean difference between consecutive samples.
3. Download and upload: 8 parallel HTTP connections for 10 seconds each. The first 2 seconds
   (TCP ramp-up) are left out of the result.

Transfers are handled by libcurl (`curl_multi`), so PHP only counts bytes: a gigabit link is
saturated in both directions with a few percent of CPU. Tests run in the background
(`/usr/local/pkg/speedtest/run.php`) and publish their progress in
`/var/run/speedtest/status.json`; the history lives in `/var/db/speedtest/history.json`.

The results measure the throughput of the firewall itself. On small appliances, they can be lower
than what LAN clients get through the firewall.

## Layout

```
install.sh                        one-line installer (builds the package on the firewall)
net/pfSense-pkg-speedtest/        FreeBSD port, in the format of github.com/pfsense/FreeBSD-ports
  Makefile, pkg-descr, pkg-plist
  files/usr/local/pkg/speedtest.xml          package definition (Diagnostics menu entry)
  files/usr/local/pkg/speedtest.inc          pfSense glue (interfaces, background start)
  files/usr/local/pkg/speedtest/engine.inc   measurement engine (no pfSense dependency)
  files/usr/local/pkg/speedtest/store.inc    status, lock and history files
  files/usr/local/pkg/speedtest/run.php      test runner (CLI and background)
  files/usr/local/www/diag_speedtest.php     GUI page
  files/etc/inc/priv/speedtest.priv.inc      user privilege "WebCfg - Diagnostics: Speedtest"
dev/                              local preview of the GUI page
```

## Development

`dev/serve.sh` serves the page on <http://localhost:8080/> with PHP >= 8.1 and the curl
extension. It downloads the pfSense GUI sources for the real CSS, JavaScript and `Form`
classes, and stubs the system functions. Tests run for real from the development machine.
Append `?theme=dark` to the URL for the dark theme.

The engine can also be used alone:

```sh
SPEEDTEST_RUN_DIR=/tmp/st SPEEDTEST_DB_DIR=/tmp/st \
	php net/pfSense-pkg-speedtest/files/usr/local/pkg/speedtest/run.php
```

## License

Apache License 2.0, like pfSense and its packages.
This project is not affiliated with Ookla or Netgate. Speedtest® is a trademark of Ookla, LLC.
