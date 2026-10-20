<?php
/*
 * store.inc
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
 * Files shared between the GUI and the background test process:
 *  - <run_dir>/status.json   state of the current (or last) test, rewritten atomically
 *  - <run_dir>/lock          held by the running test process
 *  - <run_dir>/cancel        created by the GUI to request a cancellation
 *  - <db_dir>/history.json   last results, newest first
 */

class SpeedtestStore {
	const HISTORY_MAX = 100;
	/* A running status not refreshed for this long belongs to a dead process */
	const STALE_AFTER = 30;

	public $run_dir;
	public $db_dir;

	public function __construct($run_dir = null, $db_dir = null) {
		$this->run_dir = $run_dir ?: (getenv('SPEEDTEST_RUN_DIR') ?: '/var/run/speedtest');
		$this->db_dir = $db_dir ?: (getenv('SPEEDTEST_DB_DIR') ?: '/var/db/speedtest');
	}

	public function read_status() {
		$status = $this->read_json("{$this->run_dir}/status.json");
		if (empty($status)) {
			return array('state' => 'idle');
		}
		if ($status['state'] === 'running' && time() - (int)($status['updated'] ?? 0) > self::STALE_AFTER) {
			$status['state'] = 'error';
			$status['error'] = 'The test process stopped unexpectedly.';
		}
		return $status;
	}

	public function write_status(array $status) {
		$status['updated'] = time();
		$this->write_json("{$this->run_dir}/status.json", $status);
	}

	/* Returns the lock handle, to keep open while the test runs, or false if a test is running. */
	public function acquire_lock() {
		$this->ensure_dir($this->run_dir);
		$fp = fopen("{$this->run_dir}/lock", 'c');
		if (!$fp || !flock($fp, LOCK_EX | LOCK_NB)) {
			return false;
		}
		return $fp;
	}

	public function is_running() {
		$fp = $this->acquire_lock();
		if ($fp === false) {
			return true;
		}
		fclose($fp);
		return false;
	}

	public function request_cancel() {
		$this->ensure_dir($this->run_dir);
		touch("{$this->run_dir}/cancel");
	}

	public function cancel_requested() {
		clearstatcache(true, "{$this->run_dir}/cancel");
		return file_exists("{$this->run_dir}/cancel");
	}

	public function clear_cancel() {
		@unlink("{$this->run_dir}/cancel");
	}

	public function history() {
		return $this->read_json("{$this->db_dir}/history.json") ?: array();
	}

	public function add_history(array $entry) {
		$this->update_history(function ($history) use ($entry) {
			array_unshift($history, $entry);
			return array_slice($history, 0, self::HISTORY_MAX);
		});
	}

	public function delete_history($id) {
		$this->update_history(function ($history) use ($id) {
			return array_values(array_filter($history, function ($e) use ($id) {
				return ($e['id'] ?? '') !== $id;
			}));
		});
	}

	public function clear_history() {
		$this->update_history(function () {
			return array();
		});
	}

	public function remove_run_dir() {
		foreach (glob("{$this->run_dir}/*") ?: array() as $f) {
			@unlink($f);
		}
		@rmdir($this->run_dir);
	}

	private function update_history(callable $fn) {
		$this->ensure_dir($this->db_dir);
		$fp = fopen("{$this->db_dir}/history.lock", 'c');
		flock($fp, LOCK_EX);
		$this->write_json("{$this->db_dir}/history.json", $fn($this->history()));
		flock($fp, LOCK_UN);
		fclose($fp);
	}

	private function read_json($path) {
		$data = @file_get_contents($path);
		$data = ($data === false) ? null : json_decode($data, true);
		return is_array($data) ? $data : null;
	}

	private function write_json($path, $data) {
		$this->ensure_dir(dirname($path));
		$tmp = $path . '.' . getmypid() . '.tmp';
		file_put_contents($tmp, json_encode($data));
		rename($tmp, $path);
	}

	private function ensure_dir($dir) {
		if (!is_dir($dir)) {
			@mkdir($dir, 0755, true);
		}
	}
}
