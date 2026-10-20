<?php
/* php -S router: the package page from the build dir, everything else from the pfSense www tree */
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
if ($path === '/' || $path === '/diag_speedtest.php') {
	chdir(getenv('PKG_ROOT') . '/usr/local/www');
	require(getenv('PKG_ROOT') . '/usr/local/www/diag_speedtest.php');
	return true;
}
return false;
