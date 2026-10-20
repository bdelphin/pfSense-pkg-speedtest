<?php
/*
 * engine.inc
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
 * Bandwidth measurement against speedtest.net (Ookla) servers:
 *  - latency: TCP "PING" commands on the server port (HTTP fallback)
 *  - download / upload: parallel HTTP transfers driven by curl_multi,
 *    ignoring the first seconds of each phase (TCP ramp-up).
 *
 * This file has no dependency on pfSense and runs on any PHP >= 7.4 CLI
 * with the curl extension.
 */

class SpeedtestException extends Exception {}
class SpeedtestCancelled extends SpeedtestException {}

class SpeedtestEngine {
	const VERSION = '0.1.1';
	const SERVERS_URL = 'https://www.speedtest.net/api/js/servers';
	const CLIENT_URL = 'https://www.speedtest.net/api/js/config-sdk';
	const DOWNLOAD_SIZE = 25000000;
	const UPLOAD_SIZE = 25000000;
	const REPORT_INTERVAL = 0.25;

	/* Source IPv4 address to bind to (multi-WAN), null for the routing table default */
	public $source_ip = null;
	/* Parallel TCP connections per transfer phase */
	public $connections = 8;
	/* Duration of each transfer phase, in seconds */
	public $duration = 10.0;
	/* Initial part of each transfer phase left out of the result, in seconds */
	public $warmup = 2.0;
	public $ping_count = 10;
	public $timeout = 10;

	/* function (string $phase, array $data): progress notifications */
	public $on_progress = null;
	/* function (): bool, return true to abort the test */
	public $should_cancel = null;

	private $chunk = null;

	/*
	 * Nearest servers for the public IP address used to reach speedtest.net,
	 * sorted by distance.
	 */
	public function get_servers($limit = 20, $search = '') {
		$query = array('engine' => 'js', 'limit' => (int)$limit, 'https_functional' => 'true');
		if ($search !== '') {
			$query['search'] = $search;
		}
		$list = $this->get_json(self::SERVERS_URL . '?' . http_build_query($query));
		$servers = array();
		foreach ($list as $s) {
			if (empty($s['id']) || empty($s['host']) || strpos($s['host'], ':') === false) {
				continue;
			}
			$servers[] = array(
				'id' => (string)$s['id'],
				'name' => (string)($s['name'] ?? ''),
				'country' => (string)($s['country'] ?? ''),
				'cc' => (string)($s['cc'] ?? ''),
				'sponsor' => (string)($s['sponsor'] ?? ''),
				'host' => (string)$s['host'],
				'distance' => (int)($s['distance'] ?? 0),
			);
		}
		return $servers;
	}

	public function get_server($id) {
		foreach ($this->get_servers(100) as $s) {
			if ($s['id'] === (string)$id) {
				return $s;
			}
		}
		throw new SpeedtestException(sprintf('Server %s is not available from this location.', $id));
	}

	/* Public IP address and ISP as seen by speedtest.net. */
	public function get_client_info() {
		$info = $this->get_json(self::CLIENT_URL);
		return array(
			'ip' => (string)($info['ipAddress'] ?? ''),
			'isp' => (string)($info['ispName'] ?? ''),
		);
	}

	/* Ping the first $candidates servers and return the one with the lowest latency. */
	public function select_server(array $servers, $candidates = 5) {
		$best = null;
		$best_rtt = INF;
		$errors = array();
		foreach (array_slice($servers, 0, $candidates) as $i => $s) {
			$this->progress('select', array('progress' => $i / $candidates, 'server' => $s));
			try {
				$rtts = $this->ping($s, 3, false, 1.0);
				if (min($rtts) < $best_rtt) {
					$best_rtt = min($rtts);
					$best = $s;
				}
			} catch (SpeedtestCancelled $e) {
				throw $e;
			} catch (SpeedtestException $e) {
				$errors[] = $e->getMessage();
			}
		}
		if ($best === null) {
			throw new SpeedtestException('No reachable server. ' . implode(' ', array_unique($errors)));
		}
		return $best;
	}

	/* Latency (median) and jitter (mean deviation between consecutive samples), in ms. */
	public function measure_latency(array $server) {
		$this->progress('latency', array('progress' => 0));
		$rtts = $this->ping($server, $this->ping_count, true);
		$sorted = $rtts;
		sort($sorted);
		$n = count($sorted);
		$median = ($n % 2) ? $sorted[intdiv($n, 2)] : ($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2;
		$jitter = 0.0;
		for ($i = 1; $i < $n; $i++) {
			$jitter += abs($rtts[$i] - $rtts[$i - 1]);
		}
		$jitter = ($n > 1) ? $jitter / ($n - 1) : 0.0;
		return array('latency' => round($median, 2), 'jitter' => round($jitter, 2));
	}

	public function measure_download(array $server) {
		return $this->transfer($server, false);
	}

	public function measure_upload(array $server) {
		return $this->transfer($server, true);
	}

	/*
	 * Round-trip times in ms, using the Ookla TCP protocol or HTTP as a fallback.
	 * $timeout (seconds) applies to each network operation.
	 */
	public function ping(array $server, $count, $report = false, $timeout = null) {
		$this->resolve($server);
		$timeout = $timeout ?: $this->timeout;
		try {
			return $this->tcp_ping($server, $count, $report, $timeout);
		} catch (SpeedtestCancelled $e) {
			throw $e;
		} catch (SpeedtestException $e) {
			return $this->http_ping($server, $count, $report, $timeout);
		}
	}

	private function tcp_ping(array $server, $count, $report, $timeout) {
		$opts = array('socket' => array());
		if ($this->source_ip) {
			$opts['socket']['bindto'] = $this->source_ip . ':0';
		}
		$sock = @stream_socket_client("tcp://{$server['ip']}:{$server['port']}", $errno, $errstr,
		    $timeout, STREAM_CLIENT_CONNECT, stream_context_create($opts));
		if (!$sock) {
			throw new SpeedtestException(sprintf('Cannot connect to %s: %s.', $server['host'], $errstr));
		}
		stream_set_timeout($sock, (int)$timeout, (int)(fmod($timeout, 1) * 1e6));
		try {
			fwrite($sock, "HI\n");
			if (strpos((string)fgets($sock), 'HELLO') !== 0) {
				throw new SpeedtestException(sprintf('Unexpected reply from %s.', $server['host']));
			}
			$rtts = array();
			for ($i = 0; $i < $count; $i++) {
				$this->check_cancel();
				$t = hrtime(true);
				fwrite($sock, sprintf("PING %d\n", (int)(microtime(true) * 1000)));
				if (strpos((string)fgets($sock), 'PONG') !== 0) {
					throw new SpeedtestException(sprintf('No ping reply from %s.', $server['host']));
				}
				$rtts[] = (hrtime(true) - $t) / 1e6;
				if ($report) {
					$this->progress('latency', array('progress' => ($i + 1) / $count, 'rtt' => end($rtts)));
				}
			}
			@fwrite($sock, "QUIT\n");
			return $rtts;
		} finally {
			fclose($sock);
		}
	}

	private function http_ping(array $server, $count, $report, $timeout) {
		$ch = $this->curl_handle($server, '/speedtest/latency.txt');
		curl_setopt($ch, CURLOPT_RETURNTRANSFER, true);
		curl_setopt($ch, CURLOPT_CONNECTTIMEOUT_MS, (int)($timeout * 1000));
		curl_setopt($ch, CURLOPT_TIMEOUT_MS, (int)($timeout * 1000));
		/* First request opens the connection, it is not part of the samples */
		if (curl_exec($ch) === false) {
			throw new SpeedtestException(sprintf('Cannot reach %s: %s.', $server['host'], curl_error($ch)));
		}
		$rtts = array();
		for ($i = 0; $i < $count; $i++) {
			$this->check_cancel();
			$t = hrtime(true);
			if (curl_exec($ch) === false) {
				throw new SpeedtestException(sprintf('Cannot reach %s: %s.', $server['host'], curl_error($ch)));
			}
			$rtts[] = (hrtime(true) - $t) / 1e6;
			if ($report) {
				$this->progress('latency', array('progress' => ($i + 1) / $count, 'rtt' => end($rtts)));
			}
		}
		return $rtts;
	}

	/*
	 * Keep $this->connections transfers running for $this->duration seconds.
	 * Returns the speed in Mbps computed over the post-warmup part.
	 */
	private function transfer(array $server, $upload) {
		$this->resolve($server);
		$phase = $upload ? 'upload' : 'download';
		if ($upload && $this->chunk === null) {
			$this->chunk = str_repeat(random_bytes(1024), 256);
		}

		$bytes = 0;
		$mh = curl_multi_init();
		$active = array();
		$add = function () use ($server, $upload, $mh, &$active, &$bytes) {
			$ch = $upload ? $this->upload_handle($server, $bytes) : $this->download_handle($server, $bytes);
			curl_multi_add_handle($mh, $ch);
			$active[spl_object_id($ch)] = $ch;
		};

		$failures = 0;
		$last_error = '';
		$samples = array(array(0.0, 0));
		$live = array();
		$mark = null;
		$next_report = 0.0;
		$t0 = microtime(true);
		try {
			for ($i = 0; $i < $this->connections; $i++) {
				$add();
			}
			while (true) {
				curl_multi_exec($mh, $running);
				$elapsed = microtime(true) - $t0;

				while (($info = curl_multi_info_read($mh)) !== false) {
					$ch = $info['handle'];
					$code = curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
					if ($info['result'] !== CURLE_OK || $code != 200) {
						$failures++;
						$last_error = ($info['result'] !== CURLE_OK) ? curl_strerror($info['result']) : "HTTP {$code}";
					}
					curl_multi_remove_handle($mh, $ch);
					unset($active[spl_object_id($ch)]);
					if ($failures > $this->connections * 3) {
						throw new SpeedtestException(sprintf('%s test failed: %s.', ucfirst($phase), $last_error));
					}
					if ($elapsed < $this->duration) {
						$add();
					}
				}

				if ($mark === null && $elapsed >= $this->warmup) {
					$mark = array($elapsed, $bytes);
				}
				if ($elapsed >= $next_report || $elapsed >= $this->duration) {
					$next_report = $elapsed + self::REPORT_INTERVAL;
					$samples[] = array($elapsed, $bytes);
					/* Live speed over a sliding window of about one second */
					$from = $samples[max(0, count($samples) - 5)];
					$mbps = ($elapsed > $from[0]) ? ($bytes - $from[1]) * 8 / ($elapsed - $from[0]) / 1e6 : 0;
					$live[] = round($mbps, 2);
					$this->progress($phase, array(
					    'progress' => min(1, $elapsed / $this->duration),
					    'mbps' => round($mbps, 2),
					    'bytes' => $bytes,
					    'samples' => $live,
					));
					$this->check_cancel();
				}
				if ($elapsed >= $this->duration) {
					break;
				}
				if ($bytes == 0 && $elapsed > $this->timeout) {
					throw new SpeedtestException(sprintf('%s test failed: no data transferred%s.',
					    ucfirst($phase), $last_error ? " ({$last_error})" : ''));
				}
				if (curl_multi_select($mh, 0.05) == -1) {
					usleep(5000);
				}
			}
		} finally {
			foreach ($active as $ch) {
				curl_multi_remove_handle($mh, $ch);
			}
			curl_multi_close($mh);
		}

		if ($mark === null || $elapsed <= $mark[0]) {
			$mark = array(0.0, 0);
		}
		$mbps = ($bytes - $mark[1]) * 8 / ($elapsed - $mark[0]) / 1e6;
		return array('mbps' => round($mbps, 2), 'bytes' => $bytes, 'samples' => $live);
	}

	private function download_handle(array $server, &$bytes) {
		$path = '/download?nocache=' . bin2hex(random_bytes(8)) . '&size=' . self::DOWNLOAD_SIZE;
		$ch = $this->curl_handle($server, $path);
		curl_setopt_array($ch, array(
		    CURLOPT_HTTPHEADER => array('Cache-Control: no-cache'),
		    CURLOPT_WRITEFUNCTION => function ($ch, $data) use (&$bytes) {
			    $n = strlen($data);
			    $bytes += $n;
			    return $n;
		    },
		));
		return $ch;
	}

	private function upload_handle(array $server, &$bytes) {
		$path = '/upload?nocache=' . bin2hex(random_bytes(8));
		$ch = $this->curl_handle($server, $path);
		$sent = 0;
		$chunk = $this->chunk;
		curl_setopt_array($ch, array(
		    CURLOPT_UPLOAD => true,
		    CURLOPT_CUSTOMREQUEST => 'POST',
		    CURLOPT_INFILESIZE => self::UPLOAD_SIZE,
		    CURLOPT_HTTPHEADER => array('Expect:', 'Cache-Control: no-cache',
			'Content-Type: application/octet-stream'),
		    CURLOPT_RETURNTRANSFER => true,
		    CURLOPT_READFUNCTION => function ($ch, $fd, $len) use (&$bytes, &$sent, $chunk) {
			    $n = min($len, self::UPLOAD_SIZE - $sent, strlen($chunk));
			    if ($n <= 0) {
				    return '';
			    }
			    $sent += $n;
			    $bytes += $n;
			    return ($n == strlen($chunk)) ? $chunk : substr($chunk, 0, $n);
		    },
		));
		return $ch;
	}

	/* Easy handle for a speedtest server, pinned to the address used for latency. */
	private function curl_handle(array $server, $path) {
		$ch = curl_init("http://{$server['host']}{$path}");
		curl_setopt_array($ch, array(
		    CURLOPT_USERAGENT => 'pfSense-pkg-speedtest/' . self::VERSION,
		    CURLOPT_RESOLVE => array("{$server['hostname']}:{$server['port']}:{$server['ip']}"),
		    CURLOPT_CONNECTTIMEOUT => $this->timeout,
		    CURLOPT_NOSIGNAL => true,
		    CURLOPT_BUFFERSIZE => 262144,
		));
		if ($this->source_ip) {
			curl_setopt($ch, CURLOPT_INTERFACE, $this->source_ip);
		}
		return $ch;
	}

	private function get_json($url) {
		$ch = curl_init($url);
		curl_setopt_array($ch, array(
		    CURLOPT_USERAGENT => 'pfSense-pkg-speedtest/' . self::VERSION,
		    CURLOPT_RETURNTRANSFER => true,
		    CURLOPT_FOLLOWLOCATION => true,
		    CURLOPT_ENCODING => '',
		    CURLOPT_CONNECTTIMEOUT => $this->timeout,
		    CURLOPT_TIMEOUT => 20,
		    CURLOPT_NOSIGNAL => true,
		    CURLOPT_IPRESOLVE => CURL_IPRESOLVE_V4,
		));
		if ($this->source_ip) {
			curl_setopt($ch, CURLOPT_INTERFACE, $this->source_ip);
		}
		$body = curl_exec($ch);
		if ($body === false) {
			throw new SpeedtestException(sprintf('Cannot reach speedtest.net: %s.', curl_error($ch)));
		}
		$code = curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
		if ($code != 200) {
			throw new SpeedtestException(sprintf('speedtest.net returned HTTP %d.', $code));
		}
		$data = json_decode($body, true);
		if (!is_array($data)) {
			throw new SpeedtestException('Invalid response from speedtest.net.');
		}
		return $data;
	}

	/* Resolve the server once (IPv4) so that every measurement hits the same address. */
	public function resolve(array &$server) {
		if (isset($server['ip'])) {
			return;
		}
		$parts = explode(':', $server['host']);
		$server['hostname'] = $parts[0];
		$server['port'] = (int)($parts[1] ?? 8080);
		$ip = gethostbyname($server['hostname']);
		if (!filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
			throw new SpeedtestException(sprintf('Cannot resolve %s.', $server['hostname']));
		}
		$server['ip'] = $ip;
	}

	private function progress($phase, array $data) {
		if ($this->on_progress) {
			call_user_func($this->on_progress, $phase, $data);
		}
	}

	private function check_cancel() {
		if ($this->should_cancel && call_user_func($this->should_cancel)) {
			throw new SpeedtestCancelled('Test cancelled.');
		}
	}
}
