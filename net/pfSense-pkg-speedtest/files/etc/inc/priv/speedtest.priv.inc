<?php
/*
 * speedtest.priv.inc
 *
 * part of pfSense-pkg-speedtest
 * Licensed under the Apache License, Version 2.0
 */

global $priv_list;

$priv_list['page-diagnostics-speedtest'] = array();
$priv_list['page-diagnostics-speedtest']['name'] = "WebCfg - Diagnostics: Speedtest";
$priv_list['page-diagnostics-speedtest']['descr'] = "Allow access to the 'Diagnostics: Speedtest' page.";
$priv_list['page-diagnostics-speedtest']['match'] = array();
$priv_list['page-diagnostics-speedtest']['match'][] = "diag_speedtest.php*";
