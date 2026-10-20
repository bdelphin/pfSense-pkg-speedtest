<?php
/*
 * Development stub of pfSense's guiconfig.inc: loads the real Form classes
 * and fakes the few system functions used by the package.
 */

$pfsense_www = getenv('PFSENSE_WWW');
spl_autoload_register(function ($class) use ($pfsense_www) {
	$file = "{$pfsense_www}/classes/" . str_replace('_', '/', $class) . '.class.php';
	if (file_exists($file)) {
		require_once($file);
	}
});

define('COLLAPSIBLE', 0x08);
define('SEC_CLOSED', 0x04);
define('SEC_OPEN', 0x00);

if (!function_exists('gettext')) {
	function gettext($s) { return $s; }
}

function get_configured_interface_with_descr() {
	return array('wan' => 'WAN', 'opt1' => 'WAN2_FIBER', 'lan' => 'LAN');
}

function get_interface_ip($if) {
	return array('wan' => '0.0.0.0', 'opt1' => '0.0.0.0', 'lan' => '192.168.1.1')[$if];
}

function interface_has_gateway($if) {
	return $if !== 'lan';
}

function is_ipaddrv4($ip) {
	return (bool)filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4);
}

function mwexec_bg($cmd) {
	exec("nohup {$cmd} > /dev/null 2>&1 &");
}
