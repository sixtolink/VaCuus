// imedemo.js - company multiline fix (VaCuus-Multiline-Fix.html)
// The textarea only edits; #multi-scroll owns the scrolling. The textarea's
// constructor inline overflow-y:auto is the ONLY thing that can't be beaten
// from CSS -- killing it requires an INLINE hidden from script (fix note §2).

var multi = document.getElementById('multi');
var outer = document.getElementById('multi-scroll');
var single = document.getElementById('single');
var committedEl = document.getElementById('committed');
var multiEl = document.getElementById('multivalue');
var charEl = document.getElementById('charcount');
var eventEl = document.getElementById('eventcount');
var events = 0;

var LINE_HEIGHT = 24;
var MIN_HEIGHT = 72;  // #multi-scroll content area: 96 - 2*12 padding
var CHARS_PER_LINE = 22; // over-estimate: better too tall than too short

// ── Kill internal scroll INLINE (the one thing CSS cannot do) ────────────────
if (multi && multi.style) {
	if (typeof multi.style.setProperty === 'function') {
		multi.style.setProperty('overflow-y', 'hidden');
	}
	multi.style.overflowY = 'hidden';
	multi.style['overflow-y'] = 'hidden';
}

// ── GROW: resize to cover ALL content, over-estimating ──────────────────────
function growTextarea() {
	if (!multi) return;
	var v = multi.value || '';
	var nl = 0;
	for (var i = 0; i < v.length; i++) { if (v.charAt(i) === '\n') nl++; }
	var wrapped = Math.ceil(v.length / CHARS_PER_LINE);
	var lines = Math.max(1, Math.max(nl + 1, wrapped));
	var h = Math.max(MIN_HEIGHT, lines * LINE_HEIGHT);
	if (multi.style) multi.style.height = h + 'px';
	multi.scrollTop = 0; // re-zero internal offset every change
}

// ── Outer follows the newest content (never fights user scrollback) ─────────
function outerAtBottom() {
	if (!outer) return true;
	return outer.scrollTop >= outer.scrollHeight - outer.clientHeight - 2;
}
function scrollOuterDown() {
	if (outer) outer.scrollTop = outer.scrollHeight;
}

// ── Focus styling on the outer (textarea lost its border) ───────────────────
if (multi && outer) {
	multi.addEventListener('focus', function() {
		if (outer.setAttribute) outer.setAttribute('data-focused', '1');
		growTextarea();
		scrollOuterDown();
	});
	multi.addEventListener('blur', function() {
		if (outer.setAttribute) outer.setAttribute('data-focused', '0');
	});
}

// ── Live monitor + grow + follow, one poll loop ─────────────────────────────
function esc(s) {
	return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
		.replace(/>/g, '&gt;').replace(/\n/g, ' ⏎ ');
}
function bump() {
	events++;
	if (eventEl) eventEl.innerRML = String(events);
}
if (single) single.addEventListener('textinput', bump);
if (multi) multi.addEventListener('textinput', bump);

var lastA = null, lastB = null;
function poll() {
	var a = single ? (single.value || '') : '';
	var b = multi ? (multi.value || '') : '';
	if (a !== lastA) {
		lastA = a;
		if (committedEl) committedEl.innerRML = a.length ? esc(a) : '—';
	}
	if (b !== lastB) {
		lastB = b;
		if (multiEl) multiEl.innerRML = b.length ? esc(b) : '—';
		var stick = outerAtBottom();
		growTextarea();
		if (stick) scrollOuterDown();
	}
	if (charEl) charEl.innerRML = String(a.length + b.length);
}

setInterval(poll, 150);
growTextarea();
poll();
