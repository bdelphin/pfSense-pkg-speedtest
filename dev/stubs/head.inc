<?php $theme = (($_GET['theme'] ?? $_COOKIE['theme'] ?? '') === 'dark') ? 'pfSense-dark.css' : 'pfSense.css'; ?>
<!DOCTYPE html>
<html lang="en">
<head>
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<link rel="stylesheet" href="/vendor/font-awesome/css/all.min.css">
	<link rel="stylesheet" href="/css/<?=$theme?>" />
	<title>pfSense - <?=htmlspecialchars(implode(': ', $pgtitle))?></title>
	<script type="text/javascript">var events = events || [];</script>
</head>
<body id="2">
<nav id="topmenu" class="navbar navbar-static-top navbar-inverse">
	<div class="container">
		<div class="navbar-header"><a class="navbar-brand" href="#"><?php include(getenv('PFSENSE_WWW') . '/logo.svg'); ?></a></div>
		<ul class="nav navbar-nav">
			<?php foreach (array('System', 'Interfaces', 'Firewall', 'Services', 'VPN', 'Status', 'Diagnostics', 'Help') as $m): ?>
			<li class="dropdown"><a href="#" class="dropdown-toggle"><?=$m?> <span class="caret"></span></a></li>
			<?php endforeach; ?>
		</ul>
		<ul class="nav navbar-nav navbar-right">
			<li><a href="#" onclick="document.cookie='theme=<?=$theme === 'pfSense.css' ? 'dark' : 'light'?>';location.reload();return false;" title="Toggle theme (dev only)"><i class="fa-solid fa-circle-half-stroke"></i></a></li>
		</ul>
	</div>
</nav>
<div class="container static">
	<header class="header">
		<ol class="breadcrumb"><li><?=$pgtitle[0]?></li><li><a href="#"><?=$pgtitle[1]?></a></li></ol>
		<ul class="context-links"></ul>
	</header>
