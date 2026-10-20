<?php
/*
 * run.php
 *
 * part of pfSense (https://www.pfsense.org)
 * Copyright (c) 2026 Baptiste Delphin
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Command line test runner, also launched in the background by the GUI.
 * Progress is published in the status file so that any run (GUI or shell)
 * shows up live on Diagnostics > Speedtest.
 */

require_once(__DIR__ . '/engine.inc');
require_once(__DIR__ . '/store.inc');

const USAGE = <<<EOT
Usage: pfspeedtest [options]

  --server=ID        speedtest.net server ID, or "auto" (default: auto)
  --source=IP        source IPv4 address (test through a specific WAN)
  --label=TEXT       interface name recorded in the history
  --connections=N    parallel connections (default: 8)
  --duration=SEC     duration of the download and upload phases (default: 10)
  --list             list the nearest servers and exit
  --json             print the result as JSON
  --no-save          do not record the result in the history
  -h, --help         show this help

EOT;

$opts = getopt('h', array('server:', 'source:', 'label:', 'connections:', 'duration:',
    'list', 'json', 'no-save', 'help'));
if ($opts === false || isset($opts['h']) || isset($opts['help'])) {
	fwrite(STDOUT, USAGE);
	exit(0);
}

$engine = new SpeedtestEngine();
if (!empty($opts['source'])) {
	if (!filter_var($opts['source'], FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
		fwrite(STDERR, "Invalid source address: {$opts['source']}\n");
		exit(2);
	}
	$engine->source_ip = $opts['source'];
}
if (isset($opts['connections'])) {
	$engine->connections = max(1, min(32, (int)$opts['connections']));
}
if (isset($opts['duration'])) {
	$engine->duration = max(4.0, min(60.0, (float)$opts['duration']));
}
$json = isset($opts['json']);
$tty = !$json && function_exists('posix_isatty') && posix_isatty(STDOUT);

if (isset($opts['list'])) {
	try {
		$servers = $engine->get_servers(20);
	} catch (SpeedtestException $e) {
		fwrite(STDERR, $e->getMessage() . "\n");
		exit(1);
	}
	$cut = function ($s, $w) {
		return function_exists('mb_strimwidth') ? mb_strimwidth($s, 0, $w) : substr($s, 0, $w);
	};
	if ($json) {
		echo json_encode($servers, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE) . "\n";
	} else {
		printf("%8s  %-32s %-24s %8s\n", 'ID', 'Sponsor', 'Location', 'Distance');
		foreach ($servers as $s) {
			printf("%8s  %-32s %-24s %5d km\n", $s['id'], $cut($s['sponsor'], 32),
			    $cut("{$s['name']} ({$s['cc']})", 24), $s['distance']);
		}
	}
	exit(0);
}

$store = new SpeedtestStore();
$lock = $store->acquire_lock();
if ($lock === false) {
	fwrite(STDERR, "A speed test is already running.\n");
	exit(1);
}
$store->clear_cancel();

$status = array(
	'id' => date('YmdHis') . bin2hex(random_bytes(2)),
	'state' => 'running',
	'phase' => 'init',
	'progress' => 0,
	'started' => time(),
	'interface' => (string)($opts['label'] ?? ''),
	'source' => $engine->source_ip,
	'connections' => $engine->connections,
	'server' => null,
	'client' => null,
	'latency' => null,
	'jitter' => null,
	'download' => null,
	'upload' => null,
	'live' => null,
	'samples' => array('download' => array(), 'upload' => array()),
	'error' => null,
);
$store->write_status($status);

$engine->should_cancel = array($store, 'cancel_requested');
$engine->on_progress = function ($phase, $data) use (&$status, $store, $tty) {
	$status['phase'] = $phase;
	$status['progress'] = round($data['progress'] ?? 0, 3);
	$status['live'] = $data['mbps'] ?? null;
	if (isset($data['samples'])) {
		$status['samples'][$phase] = $data['samples'];
	}
	$store->write_status($status);
	if ($tty && isset($data['mbps'])) {
		$bar = str_repeat('#', (int)round($status['progress'] * 20));
		printf("\r%-10s %10.2f Mbps  [%-20s]", ucfirst($phase) . ':', $data['mbps'], $bar);
	}
};

$out = function ($line) use ($json) {
	if (!$json) {
		echo $line . "\n";
	}
};

try {
	try {
		$status['client'] = $engine->get_client_info();
		$out(sprintf('%-10s %s (%s)', 'ISP:', $status['client']['isp'], $status['client']['ip']));
	} catch (SpeedtestCancelled $e) {
		throw $e;
	} catch (SpeedtestException $e) {
		/* Informational only */
	}

	$server_id = (string)($opts['server'] ?? 'auto');
	if ($server_id === '' || $server_id === 'auto') {
		$status['phase'] = 'select';
		$store->write_status($status);
		$server = $engine->select_server($engine->get_servers(10));
	} else {
		$server = $engine->get_server($server_id);
	}
	$engine->resolve($server);
	$status['server'] = $server;
	$out(sprintf('%-10s %s - %s (%s), %d km [id %s]', 'Server:', $server['sponsor'], $server['name'],
	    $server['cc'], $server['distance'], $server['id']));

	$latency = $engine->measure_latency($server);
	$status['latency'] = $latency['latency'];
	$status['jitter'] = $latency['jitter'];
	$out(sprintf('%-10s %.2f ms (jitter %.2f ms)', 'Latency:', $latency['latency'], $latency['jitter']));

	$download = $engine->measure_download($server);
	$status['download'] = $download['mbps'];
	$status['samples']['download'] = $download['samples'];
	$out(sprintf("%s%-10s %.2f Mbps", $tty ? "\r\033[K" : '', 'Download:', $download['mbps']));

	$upload = $engine->measure_upload($server);
	$status['upload'] = $upload['mbps'];
	$status['samples']['upload'] = $upload['samples'];
	$out(sprintf("%s%-10s %.2f Mbps", $tty ? "\r\033[K" : '', 'Upload:', $upload['mbps']));

	$status['state'] = 'done';
	$status['phase'] = 'done';
	$status['progress'] = 1;
	$status['live'] = null;
	$status['finished'] = time();
	if (!isset($opts['no-save'])) {
		$store->add_history(array(
			'id' => $status['id'],
			'time' => $status['started'],
			'interface' => $status['interface'],
			'server' => array_intersect_key($server, array_flip(array('id', 'name', 'cc', 'sponsor', 'host', 'distance'))),
			'client' => $status['client'],
			'latency' => $status['latency'],
			'jitter' => $status['jitter'],
			'download' => $status['download'],
			'upload' => $status['upload'],
		));
	}
	$rc = 0;
} catch (SpeedtestCancelled $e) {
	$status['state'] = 'cancelled';
	$status['live'] = null;
	$out(($tty ? "\r\033[K" : '') . 'Test cancelled.');
	$rc = 130;
} catch (Throwable $e) {
	$status['state'] = 'error';
	$status['live'] = null;
	$status['error'] = $e->getMessage();
	fwrite(STDERR, ($tty ? "\r\033[K" : '') . 'Error: ' . $e->getMessage() . "\n");
	$rc = 1;
}

$store->write_status($status);
$store->clear_cancel();
flock($lock, LOCK_UN);
fclose($lock);

if ($json) {
	$keys = array('id', 'state', 'started', 'interface', 'server', 'client', 'latency', 'jitter', 'download', 'upload', 'error');
	echo json_encode(array_intersect_key($status, array_flip($keys)),
	    JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE) . "\n";
}
exit($rc);
