// imedemo.js - live result polling (external file: the inline <script> never executed
// in VaCuus's QuickJS DOM subset; every working example in the tree is an external file).
// Native IME writes do not dispatch DOM textinput, so the display polls field values.
// innerRML because textContent/innerHTML are no-ops here.

var single = document.getElementById('single');
var multi = document.getElementById('multi');
var committedEl = document.getElementById('committed');
var multiEl = document.getElementById('multivalue');
var charEl = document.getElementById('charcount');
var eventEl = document.getElementById('eventcount');
var events = 0;

function esc(s) {
	return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
		.replace(/>/g, '&gt;').replace(/\n/g, ' ⏎ ');
}

function refresh() {
	var a = single.value || '';
	var b = multi.value || '';
	committedEl.innerRML = a.length ? esc(a) : '—';
	multiEl.innerRML = b.length ? esc(b) : '—';
	charEl.innerRML = String(a.length + b.length);
}

function bump() {
	events++;
	eventEl.innerRML = String(events);
	refresh();
}
if (single) { single.addEventListener('textinput', bump); }
if (multi) { multi.addEventListener('textinput', bump); }

setInterval(refresh, 150);
refresh();
