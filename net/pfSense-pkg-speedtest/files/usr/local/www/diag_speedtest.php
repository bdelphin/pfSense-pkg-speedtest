<?php
/*
 * diag_speedtest.php
 *
 * part of pfSense-pkg-speedtest
 * Licensed under the Apache License, Version 2.0
 */

##|+PRIV
##|*IDENT=page-diagnostics-speedtest
##|*NAME=Diagnostics: Speedtest
##|*DESCR=Allow access to the 'Diagnostics: Speedtest' page.
##|*MATCH=diag_speedtest.php*
##|-PRIV

require_once("guiconfig.inc");
require_once("/usr/local/pkg/speedtest.inc");

$store = new SpeedtestStore();

if (isset($_REQUEST['ajax'])) {
	header('Content-Type: application/json');
	$post = ($_SERVER['REQUEST_METHOD'] === 'POST');
	$reply = array();
	try {
		switch ($_REQUEST['ajax']) {
		case 'status':
			$reply = $store->read_status();
			break;
		case 'servers':
			$reply = array('servers' => speedtest_servers($_GET['interface'] ?? ''));
			break;
		case 'history':
			$reply = array('history' => $store->history());
			break;
		case 'start':
			if ($post) {
				speedtest_start($_POST['interface'] ?? '', $_POST['server'] ?? 'auto');
				$reply = $store->read_status();
			}
			break;
		case 'cancel':
			if ($post) {
				$store->request_cancel();
			}
			break;
		case 'delete':
			if ($post) {
				$store->delete_history((string)($_POST['id'] ?? ''));
				$reply = array('history' => $store->history());
			}
			break;
		case 'clear':
			if ($post) {
				$store->clear_history();
				$reply = array('history' => array());
			}
			break;
		}
	} catch (Exception $e) {
		$reply = array('error' => $e->getMessage());
	}
	echo json_encode($reply);
	exit;
}

$pgtitle = array(gettext("Diagnostics"), gettext("Speedtest"));
include("head.inc");

$interfaces = array('' => gettext('Default (routing table)'));
foreach (speedtest_interfaces() as $if => $info) {
	$interfaces[$if] = "{$info['descr']} ({$info['ip']})";
}

$form = new Form(false);
$section = new Form_Section('Speedtest');
$section->addInput(new Form_Select(
	'interface',
	'Interface',
	'',
	$interfaces
))->setHelp('WAN used for the test. The default follows the routing table of the firewall.');
$section->addInput(new Form_Select(
	'server',
	'Server',
	'auto',
	array('auto' => gettext('Automatic (lowest latency)'))
))->setHelp('Nearest %1$sspeedtest.net%2$s servers. Automatic picks the one with the lowest latency.',
	'<a href="https://www.speedtest.net/" target="_blank" rel="noopener noreferrer">', '</a>');
$form->add($section);

$form->addGlobal(new Form_Button(
	'start',
	'Start',
	null,
	'fa-solid fa-play'
))->addClass('btn-primary');

$form->addGlobal(new Form_Button(
	'cancel',
	'Cancel',
	null,
	'fa-solid fa-stop'
))->addClass('btn-danger hidden');

print($form);
?>

<style>
.st-body { padding: 10px 15px 12px; }
.st-metrics { display: flex; flex-wrap: wrap; margin: 0 -6px; }
.st-metric { flex: 1 1 25%; min-width: 150px; padding: 6px 6px 0; }
.st-metric-inner { position: relative; padding: 10px 14px 14px; border-radius: 4px; background: rgba(128, 128, 128, 0.07); }
.st-label { font-size: 12px; text-transform: uppercase; letter-spacing: 0.06em; opacity: 0.7; }
.st-label .fa { margin-right: 4px; }
.st-value { font-size: 34px; font-weight: 300; line-height: 1.25; white-space: nowrap; font-variant-numeric: tabular-nums; }
.st-unit { font-size: 14px; opacity: 0.6; margin-left: 2px; }
.st-bar { position: absolute; left: 0; bottom: 0; height: 3px; width: 0; border-radius: 0 0 0 4px; background: currentColor; transition: width 0.25s linear; }
.st-metric.st-active .st-value { opacity: 0.85; }
.st-lat { color: #e0a030; }
.st-dl { color: #2b8fd8; }
.st-ul { color: #9b6bd3; }
.st-chart { width: 100%; height: 70px; margin-top: 14px; display: block; }
.st-chart.st-empty { display: none; }
.st-footer { display: flex; flex-wrap: wrap; justify-content: space-between; gap: 4px 16px; margin-top: 10px; font-size: 13px; }
.st-footer .text-muted strong { font-weight: 600; }
#st-history td.st-num, #st-history th.st-num { text-align: right; font-variant-numeric: tabular-nums; }
</style>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=gettext('Results')?></h2></div>
	<div class="panel-body st-body">
		<div id="st-error" class="alert alert-danger hidden" role="alert"></div>
		<div class="st-metrics">
			<div class="st-metric" id="st-latency">
				<div class="st-metric-inner">
					<div class="st-label"><?=gettext('Latency')?></div>
					<div class="st-value"><span class="st-num">&ndash;</span><span class="st-unit">ms</span></div>
					<div class="st-bar st-lat"></div>
				</div>
			</div>
			<div class="st-metric" id="st-jitter">
				<div class="st-metric-inner">
					<div class="st-label"><?=gettext('Jitter')?></div>
					<div class="st-value"><span class="st-num">&ndash;</span><span class="st-unit">ms</span></div>
				</div>
			</div>
			<div class="st-metric" id="st-download">
				<div class="st-metric-inner">
					<div class="st-label"><i class="fa fa-download st-dl"></i><?=gettext('Download')?></div>
					<div class="st-value"><span class="st-num">&ndash;</span><span class="st-unit">Mbps</span></div>
					<div class="st-bar st-dl"></div>
				</div>
			</div>
			<div class="st-metric" id="st-upload">
				<div class="st-metric-inner">
					<div class="st-label"><i class="fa fa-upload st-ul"></i><?=gettext('Upload')?></div>
					<div class="st-value"><span class="st-num">&ndash;</span><span class="st-unit">Mbps</span></div>
					<div class="st-bar st-ul"></div>
				</div>
			</div>
		</div>
		<canvas class="st-chart st-empty" id="st-chart"></canvas>
		<div class="st-footer">
			<span id="st-state" class="text-muted"><?=gettext('No test has been run yet.')?></span>
			<span id="st-meta" class="text-muted"></span>
		</div>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?=gettext('History')?></h2></div>
	<div class="panel-body">
		<div class="table-responsive">
			<table id="st-history" class="table table-striped table-hover table-condensed">
				<thead>
					<tr>
						<th><?=gettext('Date')?></th>
						<th><?=gettext('Interface')?></th>
						<th><?=gettext('Server')?></th>
						<th class="st-num"><?=gettext('Latency')?></th>
						<th class="st-num"><?=gettext('Jitter')?></th>
						<th class="st-num"><?=gettext('Download')?></th>
						<th class="st-num"><?=gettext('Upload')?></th>
						<th><?=gettext('Actions')?></th>
					</tr>
				</thead>
				<tbody></tbody>
			</table>
		</div>
	</div>
</div>

<nav class="action-buttons">
	<button id="st-clear" type="button" class="btn btn-danger btn-sm">
		<i class="fa fa-trash icon-embed-btn"></i><?=gettext('Clear history')?>
	</button>
</nav>

<script type="text/javascript">
//<![CDATA[
events.push(function() {
	var url = 'diag_speedtest.php';
	var results = <?=json_encode($store->history())?>;
	var status = <?=json_encode($store->read_status())?>;
	var text = <?=json_encode(array(
		'init' => gettext('Starting…'),
		'select' => gettext('Selecting the best server…'),
		'latency' => gettext('Measuring latency…'),
		'download' => gettext('Testing download speed…'),
		'upload' => gettext('Testing upload speed…'),
		'cancelled' => gettext('Test cancelled.'),
		'done' => gettext('Completed on %s.'),
		'none' => gettext('No test has been run yet.'),
		'empty' => gettext('No results yet.'),
		'loading' => gettext('Loading servers…'),
		'auto' => gettext('Automatic (lowest latency)'),
		'default' => gettext('Default'),
		'confirm' => gettext('Delete all the results?'),
		'delete' => gettext('Delete this result'),
		'server' => gettext('Server'),
		'isp' => gettext('ISP'),
	))?>;
	var pollTimer = null;
	var wasRunning = false;

	function post(action, data) {
		data = $.extend({ajax: action}, data || {});
		if (typeof csrfMagicToken !== 'undefined') {
			data[csrfMagicName] = csrfMagicToken;
		}
		return $.post(url, data, null, 'json');
	}

	function esc(s) {
		return $('<div/>').text(s == null ? '' : String(s)).html();
	}

	function fmtSpeed(v) {
		if (v == null) return '–';
		return v >= 100 ? v.toFixed(1) : v.toFixed(2);
	}

	function fmtMs(v) {
		if (v == null) return '–';
		return v >= 100 ? v.toFixed(0) : (v >= 10 ? v.toFixed(1) : v.toFixed(2));
	}

	function fmtDate(ts) {
		return new Date(ts * 1000).toLocaleString();
	}

	function serverLabel(s) {
		return s.sponsor + ' – ' + s.name + (s.cc ? ' (' + s.cc + ')' : '');
	}

	/* Servers */

	function loadServers() {
		var iface = $('#interface').val();
		var saved = localStorage.getItem('speedtest.server') || 'auto';
		var $srv = $('#server').prop('disabled', true);
		$srv.empty().append($('<option/>').val('auto').text(text.loading));
		$.getJSON(url, {ajax: 'servers', interface: iface}).done(function(r) {
			$srv.empty().append($('<option/>').val('auto').text(text.auto));
			(r.servers || []).forEach(function(s) {
				$srv.append($('<option/>').val(s.id).text(serverLabel(s) + ' · ' + s.distance + ' km'));
			});
			if (r.error) {
				showError(r.error);
			}
			$srv.val($srv.find('option[value="' + saved + '"]').length ? saved : 'auto');
		}).always(function() {
			$srv.prop('disabled', isRunning());
			if ($srv.children().length === 0) {
				$srv.append($('<option/>').val('auto').text(text.auto));
			}
		});
	}

	/* Live results */

	function isRunning() {
		return status && status.state === 'running';
	}

	function showError(msg) {
		$('#st-error').text(msg || '').toggleClass('hidden', !msg);
	}

	function setMetric(id, value, fmt, active, progress) {
		var $m = $('#st-' + id);
		$m.find('.st-num').text(fmt(value));
		$m.toggleClass('st-active', !!active);
		$m.find('.st-bar').css('width', active ? Math.round(progress * 100) + '%' : 0);
	}

	function render() {
		var st = status || {state: 'idle'};
		var running = st.state === 'running';
		var phase = running ? st.phase : null;
		var p = st.progress || 0;

		setMetric('latency', st.latency, fmtMs, phase === 'latency' || phase === 'select' || phase === 'init',
			phase === 'latency' ? p : 0);
		setMetric('jitter', st.jitter, fmtMs, false, 0);
		setMetric('download', phase === 'download' ? st.live : st.download, fmtSpeed, phase === 'download', p);
		setMetric('upload', phase === 'upload' ? st.live : st.upload, fmtSpeed, phase === 'upload', p);

		var state;
		if (running) {
			state = text[st.phase] || text.init;
		} else if (st.state === 'done') {
			state = text.done.replace('%s', fmtDate(st.finished || st.started));
		} else if (st.state === 'cancelled') {
			state = text.cancelled;
		} else {
			state = st.state === 'idle' ? text.none : '';
		}
		$('#st-state').text(state);
		showError(st.state === 'error' ? st.error : null);

		var meta = [];
		if (st.server) {
			meta.push('<strong>' + text.server + ':</strong> ' + esc(serverLabel(st.server)));
		}
		if (st.client && st.client.isp) {
			meta.push('<strong>' + text.isp + ':</strong> ' + esc(st.client.isp) + ' (' + esc(st.client.ip) + ')');
		}
		$('#st-meta').html(meta.join(' &nbsp;·&nbsp; '));

		$('#start').toggleClass('hidden', running);
		$('#cancel').toggleClass('hidden', !running).prop('disabled', false);
		$('#interface').prop('disabled', running);
		$('#server').prop('disabled', running);

		drawChart(st.samples || {});
	}

	function drawChart(samples) {
		var canvas = document.getElementById('st-chart');
		var empty = !(samples.download || []).length && !(samples.upload || []).length;
		$(canvas).toggleClass('st-empty', empty);
		if (empty) return;
		var ratio = window.devicePixelRatio || 1;
		var w = canvas.clientWidth, h = canvas.clientHeight;
		canvas.width = w * ratio;
		canvas.height = h * ratio;
		var ctx = canvas.getContext('2d');
		ctx.scale(ratio, ratio);
		ctx.clearRect(0, 0, w, h);

		var series = [
			{data: samples.download || [], color: $('#st-download .st-bar').css('color')},
			{data: samples.upload || [], color: $('#st-upload .st-bar').css('color')}
		];
		var max = 0, len = 0;
		series.forEach(function(s) {
			s.data.forEach(function(v) { max = Math.max(max, v); });
			len = Math.max(len, s.data.length);
		});
		if (!max) return;
		/* A phase lasts 10s with a sample every 250ms */
		len = Math.max(len, 41);
		max *= 1.1;

		series.forEach(function(s) {
			if (s.data.length < 2) return;
			var x = function(i) { return i / (len - 1) * w; };
			var y = function(v) { return h - 2 - v / max * (h - 4); };
			ctx.beginPath();
			ctx.moveTo(0, h);
			s.data.forEach(function(v, i) { ctx.lineTo(x(i), y(v)); });
			ctx.lineTo(x(s.data.length - 1), h);
			ctx.closePath();
			ctx.globalAlpha = 0.12;
			ctx.fillStyle = s.color;
			ctx.fill();
			ctx.globalAlpha = 1;
			ctx.beginPath();
			s.data.forEach(function(v, i) { i ? ctx.lineTo(x(i), y(v)) : ctx.moveTo(x(i), y(v)); });
			ctx.strokeStyle = s.color;
			ctx.lineWidth = 1.5;
			ctx.lineJoin = 'round';
			ctx.stroke();
		});
	}

	function poll() {
		clearTimeout(pollTimer);
		$.getJSON(url, {ajax: 'status'}).done(function(st) {
			status = st;
			render();
		}).always(function() {
			if (isRunning()) {
				wasRunning = true;
				pollTimer = setTimeout(poll, 400);
			} else if (wasRunning) {
				wasRunning = false;
				loadHistory();
			}
		});
	}

	/* History */

	function renderHistory() {
		var $tb = $('#st-history tbody').empty();
		if (!results.length) {
			$tb.append('<tr><td colspan="8" class="text-muted">' + esc(text.empty) + '</td></tr>');
		}
		results.forEach(function(e) {
			var $tr = $('<tr/>');
			$tr.append($('<td/>').text(fmtDate(e.time)));
			$tr.append($('<td/>').text(e.interface || text.default));
			$tr.append($('<td/>').text(e.server ? serverLabel(e.server) : '').attr('title', e.server ? e.server.host : ''));
			$tr.append($('<td class="st-num"/>').text(fmtMs(e.latency) + ' ms'));
			$tr.append($('<td class="st-num"/>').text(fmtMs(e.jitter) + ' ms'));
			$tr.append($('<td class="st-num"/>').text(fmtSpeed(e.download) + ' Mbps'));
			$tr.append($('<td class="st-num"/>').text(fmtSpeed(e.upload) + ' Mbps'));
			$tr.append($('<td/>').append(
				$('<a href="#" class="fa fa-trash st-delete"/>').attr('title', text.delete).data('id', e.id)));
			$tb.append($tr);
		});
		$('#st-clear').prop('disabled', !results.length);
	}

	function loadHistory() {
		$.getJSON(url, {ajax: 'history'}).done(function(r) {
			results = r.history || [];
			renderHistory();
		});
	}

	/* Events */

	$('form').on('submit', function(e) { e.preventDefault(); });

	$('#start').on('click', function(e) {
		e.preventDefault();
		var server = $('#server').val() || 'auto';
		localStorage.setItem('speedtest.server', server);
		localStorage.setItem('speedtest.interface', $('#interface').val());
		$(this).prop('disabled', true);
		post('start', {interface: $('#interface').val(), server: server}).done(function(r) {
			if (r.error) {
				showError(r.error);
			} else {
				status = r;
				render();
				poll();
			}
		}).always(function() {
			$('#start').prop('disabled', false);
		});
	});

	$('#cancel').on('click', function(e) {
		e.preventDefault();
		$(this).prop('disabled', true);
		post('cancel');
	});

	$('#interface').on('change', loadServers);

	$('#st-history').on('click', '.st-delete', function(e) {
		e.preventDefault();
		post('delete', {id: $(this).data('id')}).done(function(r) {
			results = r.history || [];
			renderHistory();
		});
	});

	$('#st-clear').on('click', function() {
		if (confirm(text.confirm)) {
			post('clear').done(function() {
				results = [];
				renderHistory();
			});
		}
	});

	$(window).on('resize', function() { drawChart((status && status.samples) || {}); });

	var savedIf = localStorage.getItem('speedtest.interface');
	if (savedIf !== null && $('#interface option[value="' + savedIf + '"]').length) {
		$('#interface').val(savedIf);
	}
	render();
	renderHistory();
	loadServers();
	if (isRunning()) {
		poll();
	}
});
//]]>
</script>

<?php
include("foot.inc");
