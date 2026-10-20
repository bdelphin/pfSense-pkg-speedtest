<?php
/*
 * speedtest.inc
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
 */

require_once("config.inc");
require_once("interfaces.inc");
require_once("util.inc");
require_once("/usr/local/pkg/speedtest/engine.inc");
require_once("/usr/local/pkg/speedtest/store.inc");

define('SPEEDTEST_RUNNER', '/usr/local/pkg/speedtest/run.php');

function speedtest_install() {
	@mkdir('/var/db/speedtest', 0755, true);
}

function speedtest_deinstall() {
	/* The history in /var/db/speedtest is kept, so that reinstalling or upgrading does not lose it */
	$store = new SpeedtestStore();
	$store->request_cancel();
	$store->remove_run_dir();
}

/*
 * Interfaces a test can be bound to: those with an IPv4 address and a gateway.
 * Returns array(ifname => array('descr' => ..., 'ip' => ...)).
 */
function speedtest_interfaces() {
	$list = array();
	foreach (get_configured_interface_with_descr() as $if => $descr) {
		$ip = get_interface_ip($if);
		if (!is_ipaddrv4($ip) || !interface_has_gateway($if)) {
			continue;
		}
		$list[$if] = array('descr' => $descr, 'ip' => $ip);
	}
	return $list;
}

/* Source address for an interface selection, '' meaning the routing table default. */
function speedtest_source_ip($if) {
	if ($if === '' || $if === null) {
		return '';
	}
	$interfaces = speedtest_interfaces();
	if (!isset($interfaces[$if])) {
		throw new SpeedtestException(gettext('Invalid interface.'));
	}
	return $interfaces[$if]['ip'];
}

function speedtest_servers($if) {
	$engine = new SpeedtestEngine();
	$engine->source_ip = speedtest_source_ip($if) ?: null;
	return $engine->get_servers(20);
}

/* Launch a test in the background. */
function speedtest_start($if, $server) {
	$store = new SpeedtestStore();
	if ($store->is_running()) {
		throw new SpeedtestException(gettext('A speed test is already running.'));
	}
	if ($server !== 'auto' && !ctype_digit((string)$server)) {
		throw new SpeedtestException(gettext('Invalid server.'));
	}
	$source = speedtest_source_ip($if);
	$interfaces = speedtest_interfaces();
	$label = $source ? $interfaces[$if]['descr'] : '';

	/* Visible to the GUI right away, the runner takes over within a second */
	$store->write_status(array('state' => 'running', 'phase' => 'init', 'progress' => 0,
	    'started' => time(), 'interface' => $label));

	$cmd = '/usr/local/bin/php -f ' . SPEEDTEST_RUNNER . ' --' .
	    ' --server=' . escapeshellarg($server) .
	    ($source ? ' --source=' . escapeshellarg($source) : '') .
	    ($label !== '' ? ' --label=' . escapeshellarg($label) : '');
	mwexec_bg($cmd);
}
