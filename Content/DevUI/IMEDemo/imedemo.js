// imedemo.js - IMEDemo page behaviour (2026-11-18)
// Two jobs, per VaCuus-Multiline-Fix.html + the DOM-subset pitfall rules:
//   1. MULTILINE: the #multi-scroll outer box owns the scrolling. The textarea
//      grows with content (over-estimated), its internal scroll is force-killed
//      INLINE (the constructor writes inline overflow-y:auto, which a stylesheet
//      rule cannot beat), its internal offset is re-zeroed on every change, and
//      the outer scrolls to the newest content (immediately + on the next poll
//      tick, which covers the fix note's "scroll twice, layout lands between").
//   2. LIVE MONITOR: poll both fields' values every 150ms; native IME commits
//      dispatch no DOM textinput, so polling is the only reliable signal.
//      innerRML everywhere; innerHTML/textContent are silent no-ops here.

var single = document.getElementById('single');
var multi = document.getElementById('multi');
var outer = document.getElementById('multi-scroll');
var committedEl = document.getElementById('committed');
var multiEl = document.getElementById('multivalue');
var charEl = document.getElementById('charcount');
var eventEl = document.getElementById('eventcount');
var events = 0;

var LINE_HEIGHT = 24;   // matches .text-area line-height
var MIN_HEIGHT = 72;    // #multi-scroll content area: 96 - 2*12 padding
var CHARS_PER_LINE = 22; // deliberately UNDER-estimated line capacity => height over-estimate

// ── 1. Kill the textarea's internal scroll, inline, right now ────────────────
// Belt and braces: try every spelling the style proxy might expose.
(function killInternalScroll() {
	if (!multi) { return; }
	var st = multi.style;
	if (!st) { return; }
	if (typeof st.setProperty === 'function') { st.setProperty('overflow-y', 'hidden'); }
	st.overflowY = 'hidden';
	st['overflow-y'] = 'hidden';
})();

// ── 2. GROW: resize the textarea to cover ALL content, over-estimating ───────
function growTextarea() {
	if (!multi || !outer) { return; }
	var v = multi.value || '';
	var explicitLines = 0;
	for (var i = 0; i < v.length; ++i) { if (v.charAt(i) === '\n') { explicitLines++; } }
	var wrappedLines = Math.ceil(v.length / CHARS_PER_LINE); // whole-value over-estimate
	var lines = Math.max(1, Math.max(explicitLines + 1, wrappedLines));
	var h = Math.max(MIN_HEIGHT, lines * LINE_HEIGHT);
	if (multi.style) { multi.style.height = h + 'px'; }
	// Re-zero the internal offset every time: edits may have nudged it.
	multi.scrollTop = 0;
}

// ── 3. Outer scroll follows the newest content, but never fights the user ────
function outerAtBottom() {
	if (!outer) { return true; }
	return outer.scrollTop >= outer.scrollHeight - outer.clientHeight - 2;
}
function scrollOuterToBottom() {
	if (!outer) { return; }
	outer.scrollTop = outer.scrollHeight; // clamped by the engine
}

// ── 4. Focus styling lives on the outer box (the textarea lost its border) ──
if (multi && outer) {
	multi.addEventListener('focus', function () {
		outer.classList.add('focused'); // classList exists; else the style is cosmetic only
		if (outer.setAttribute) { outer.setAttribute('class', 'focused-extra'); }
		growTextarea();
		scrollOuterToBottom();
	});
	multi.addEventListener('blur', function () {
		outer.classList.remove('focused');
	});
}

// ── 5. Live monitor + grow + follow, one poll loop ───────────────────────────
function esc(s) {
	return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;')
		.replace(/>/g, '&gt;').replace(/\n/g, ' ⏎ ');
}

function bump() {
	events++;
	if (eventEl) { eventEl.innerRML = String(events); }
}

if (single) { single.addEventListener('textinput', bump); }
if (multi) { multi.addEventListener('textinput', bump); }

var lastSingle = null;
var lastMulti = null;

function poll() {
	var a = single ? (single.value || '') : '';
	var b = multi ? (multi.value || '') : '';

	if (a !== lastSingle) {
		lastSingle = a;
		if (committedEl) { committedEl.innerRML = a.length ? esc(a) : '—'; }
	}
	if (b !== lastMulti) {
		lastMulti = b;
		if (multiEl) { multiEl.innerRML = b.length ? esc(b) : '—'; }
		// The only reliable change signal for height growth (IME commits send no
		// DOM event) -- exactly why the fix note demands value polling.
		var stick = outerAtBottom();
		growTextarea();
		if (stick) { scrollOuterToBottom(); }
	}
	if (charEl) { charEl.innerRML = String(a.length + b.length); }
}

setInterval(poll, 150);
growTextarea();
poll();
