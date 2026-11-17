// Copyright 2026 Vladimir Alyamkin. All Rights Reserved.

// VaCuusImeMultilineTest.cpp — regression for the owner-reported multiline failure
// (2026-11-18): line-1 Enter inserts a newline, but the SECOND Enter "clears" the
// field and further IME commits do nothing. The live-window trace showed the mirror
// stuck at '你好\n' while the IME's write positions marched past the end (b=4,8,12),
// i.e. the two document views diverged. This test replays that exact sequence
// headless: commit, Enter, commit on line 2, Enter again, commit on line 3.

#include "Misc/AutomationTest.h"

#include "SVaCuusWidget.h"
#include "VaCuus.h"
#include "VaCuusDocumentHost.h"
#include "VaCuusEngine.h"
#include "VaCuusInputEvent.h"
#include "VaCuusInteractiveSnapshot.h"
#include "VaCuusRmlCasts.h"
#include "VaCuusSlateElement.h"
#include "VaCuusSubsystem.h"
#include "VaCuusTestDocumentHost.h"
#include "VaCuusUIThread.h"
#include "VaCuusView.h"
#include "VaCuusViewStatus.h"

#include "Engine/GameInstance.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/ITextInputMethodSystem.h"
#include "HAL/PlatformProcess.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Geometry.h"
#include "UObject/StrongObjectPtr.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

#if WITH_DEV_AUTOMATION_TESTS

namespace VaCuusImeMultilineTest
{
class FProbeHost final : public FVaCuusTestDocumentHost
{
public:
	FProbeHost()
		: FVaCuusTestDocumentHost(TEXT("vacuus_ime_multiline_view"), "vacuus://ime_multiline.rml", Rml::FocusFlag::Document)
	{
	}

	virtual void SetVisible(bool bVisible) override
	{
		check(FVaCuusUIThread::IsInUIThread());
		if (RmlDocument)
		{
			bVisible ? RmlDocument->Show() : RmlDocument->Hide();
		}
	}

	virtual void RecordAndPublishFrame() override
	{
		check(FVaCuusUIThread::IsInUIThread());

		Context->Update();

		FVaCuusInteractiveSnapshot& Snapshot = Status->GetSnapshotWriteBuffer();
		BuildVaCuusInteractiveSnapshot(*Context, ViewSize, ++SnapshotGeneration, Snapshot);
		Status->PublishSnapshot();

		FieldValue.Reset();
		MultiValue.Reset();
		MultiRect = FIntRect(0, 0, 0, 0);
		if (RmlDocument)
		{
			if (Rml::Element* Field = RmlDocument->GetElementById("field"))
			{
				if (Rml::ElementFormControl* Control = VaCuusCastFormControl(*Field))
				{
					FieldValue = UTF8_TO_TCHAR(Control->GetValue().c_str());
				}
			}
			if (Rml::Element* Multi = RmlDocument->GetElementById("multi"))
			{
				if (Rml::ElementFormControl* Control = VaCuusCastFormControl(*Multi))
				{
					MultiValue = UTF8_TO_TCHAR(Control->GetValue().c_str());
				}
				const Rml::Vector2f P = Multi->GetAbsoluteOffset(Rml::BoxArea::Border);
				const Rml::Vector2f Size = Multi->GetBox().GetSize(Rml::BoxArea::Border);
				MultiRect = FIntRect(FMath::RoundToInt(P.x), FMath::RoundToInt(P.y),
					FMath::RoundToInt(P.x + Size.x), FMath::RoundToInt(P.y + Size.y));
			}
		}

		Status->FramesRecorded.fetch_add(1, std::memory_order_release);
	}

	FString FieldValue;
	FString MultiValue;
	FIntRect MultiRect;

private:
	uint64 SnapshotGeneration = 0;
};

static bool RunFrames(FVaCuusUIThread& UIThread, int32 NumFrames)
{
	for (int32 Index = 0; Index < NumFrames; ++Index)
	{
		const uint64 Before = UIThread.GetFrameCount();
		UIThread.Trigger();
		if (!UIThread.WaitForFrameCount(Before + 1, 5.0))
		{
			return false;
		}
	}
	return true;
}

static const TCHAR* GDocument = TEXT(R"(<rml>
<head>
<style>
body { display: block; width: 100%; height: 100%; font-family: LatoLatin; font-size: 14px; }
/* 40px = one line: every newline after the first overflows the box. The owner
   repro is "content clears once it passes the visible area, then no more input"
   (2026-11-18) -- with this height the whole sequence below IS the overflow repro. */
#field { position: absolute; left: 50px; top: 50px; width: 300px; height: 40px; }
</style>
</head>
<body>
	<textarea id="field"/>
</body>
</rml>)");

static const FIntPoint GFieldPoint(100, 80);

static FPointerEvent MakePointerEvent(FIntPoint Position, const TSet<FKey>& PressedButtons, const FKey& EffectingButton)
{
	const FVector2D Screen(Position.X, Position.Y);
	return FPointerEvent(FSlateApplicationBase::CursorPointerIndex, Screen, Screen, PressedButtons, EffectingButton,
		0.0f, FModifierKeysState());
}

static FKeyEvent MakeKeyEvent(const FKey& Key)
{
	return FKeyEvent(Key, FModifierKeysState(), /*bIsRepeat=*/false, /*UserIndex=*/0, /*CharacterCode=*/0, /*KeyCode=*/0);
}

/** A character event as Slate delivers one per UTF-16 unit. */
static FCharacterEvent MakeCharacterEvent(TCHAR Character)
{
	return FCharacterEvent(Character, FModifierKeysState(), /*UserIndex=*/0, /*bIsRepeat=*/false);
}
}	 // namespace VaCuusImeMultilineTest

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVaCuusImeMultilineTest, "VaCuus.Input.MultilineEnter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVaCuusImeMultilineTest::RunTest(const FString& Parameters)
{
	using namespace VaCuusImeMultilineTest;

	if (!FPlatformProcess::SupportsMultithreading())
	{
		AddInfo(TEXT("Skipped: no multithreading support"));
		return true;
	}

	FVaCuusModule& Module = FVaCuusModule::Get();
	FVaCuusUIThread* UIThread = Module.GetOrStartUIThread();
	if (!TestNotNull(TEXT("UI thread"), UIThread))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		Module.StopUIThread();
	};

	const FIntPoint ViewSize(400, 300);
	const TSharedRef<FVaCuusViewStatus> Status = MakeShared<FVaCuusViewStatus>();

	TUniquePtr<FProbeHost> OwnedHost = MakeUnique<FProbeHost>();
	FProbeHost* Host = OwnedHost.Get();

	const uint32 ViewId = UIThread->AllocateViewId();
	UIThread->EnqueueAddView(ViewId, MoveTemp(OwnedHost), ViewSize, Status);
	UIThread->EnqueueLoadDocumentFromMemory(ViewId, GDocument, 1);

	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UVaCuusSubsystem> Subsystem(NewObject<UVaCuusSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UVaCuusView> View(NewObject<UVaCuusView>(Subsystem.Get()));
	View->InitializeView(Subsystem.Get(), ViewId, Status, ViewSize);

	const TSharedRef<FVaCuusSlateElement> Element = MakeShared<FVaCuusSlateElement>();
	TSharedRef<SVaCuusWidget> Widget = SNew(SVaCuusWidget, View.Get(), Element);
	ON_SCOPE_EXIT
	{
		Widget->DetachView();
	};

	const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(ViewSize.X, ViewSize.Y), FSlateLayoutTransform());

	if (!TestTrue(TEXT("UI frames ran"), RunFrames(*UIThread, 2)))
	{
		return false;
	}
	View->PollStatus();

	// Focus the textarea.
	{
		const TSet<FKey> LeftOnly = {EKeys::LeftMouseButton};
		Widget->OnMouseButtonDown(Geometry, MakePointerEvent(GFieldPoint, LeftOnly, EKeys::LeftMouseButton));
		Widget->OnMouseButtonUp(Geometry, MakePointerEvent(GFieldPoint, TSet<FKey>(), EKeys::LeftMouseButton));
		if (!TestTrue(TEXT("Frame after click"), RunFrames(*UIThread, 1)))
		{
			return false;
		}
		View->PollStatus();
	}

	ITextInputMethodContext* ImeContext = View->GetImeContextForTesting();
	if (!TestNotNull(TEXT("IME context"), ImeContext))
	{
		return false;
	}

	// ── 1. IME commit on line 1: 你好 ────────────────────────────────
	ImeContext->BeginComposition();
	ImeContext->SetTextInRange(0, 0, TEXT("你好"));
	ImeContext->EndComposition();
	if (!TestTrue(TEXT("Frame after commit 1"), RunFrames(*UIThread, 2)))
	{
		return false;
	}
	View->PollStatus();
	TestEqual(TEXT("Commit 1 lands"), Host->FieldValue, FString(TEXT("你好")));

	// ── 2. Enter #1: newline ─────────────────────────────────────────
	Widget->OnKeyDown(Geometry, MakeKeyEvent(EKeys::Enter));
	if (!TestTrue(TEXT("Frame after Enter 1"), RunFrames(*UIThread, 2)))
	{
		return false;
	}
	View->PollStatus();
	TestEqual(TEXT("Enter 1 inserts a newline"), Host->FieldValue, FString(TEXT("你好\n")));

	// ── 3. IME commit on line 2: 世界 ────────────────────────────────
	// Exactly the shape from the owner trace: composition writes arrive, the mirror
	// must grow with them, then the commit lands 你好\n世界.
	ImeContext->BeginComposition();
	ImeContext->SetTextInRange(3, 0, TEXT("s"));
	TestEqual(TEXT("Mirror grows mid-composition (write 1)"), int64(ImeContext->GetTextLength()), int64(4));
	ImeContext->SetTextInRange(3, 1, TEXT("shijie"));
	TestEqual(TEXT("Mirror grows mid-composition (write 2)"), int64(ImeContext->GetTextLength()), int64(9));
	ImeContext->SetTextInRange(3, 6, TEXT("世界"));
	ImeContext->EndComposition();
	if (!TestTrue(TEXT("Frame after commit 2"), RunFrames(*UIThread, 2)))
	{
		return false;
	}
	View->PollStatus();
	TestEqual(TEXT("Commit 2 lands on line 2"), Host->FieldValue, FString(TEXT("你好\n世界")));

	// ── 4. Enter #2 — the reported failure point ────────────────────
	Widget->OnKeyDown(Geometry, MakeKeyEvent(EKeys::Enter));
	if (!TestTrue(TEXT("Frame after Enter 2"), RunFrames(*UIThread, 2)))
	{
		return false;
	}
	View->PollStatus();
	TestEqual(TEXT("Enter 2 keeps the text and adds a newline"), Host->FieldValue, FString(TEXT("你好\n世界\n")));

	// ── 5. IME commit on line 3 — "candidates show but do nothing" ──
	ImeContext->BeginComposition();
	ImeContext->SetTextInRange(6, 0, TEXT("测试"));
	ImeContext->EndComposition();
	if (!TestTrue(TEXT("Frame after commit 3"), RunFrames(*UIThread, 2)))
	{
		return false;
	}
	View->PollStatus();
	TestEqual(TEXT("Commit 3 lands on line 3"), Host->FieldValue, FString(TEXT("你好\n世界\n测试")));

	// ── 6. PLAIN ENGLISH TYPING on the same textarea (2026-11-18 owner report:
	//        "英文字符也没办法正常" -- the non-IME keyboard path was never covered here).
	// ─────────────────────────────────────────────────────────────────────
	View->PollStatus();
	Widget->OnKeyChar(Geometry, MakeCharacterEvent(TEXT('a')));
	Widget->OnKeyChar(Geometry, MakeCharacterEvent(TEXT('b')));
	Widget->OnKeyChar(Geometry, MakeCharacterEvent(TEXT('c')));
	if (!TestTrue(TEXT("Frame after English typing"), RunFrames(*UIThread, 2)))
	{
		return false;
	}
	View->PollStatus();
	TestEqual(TEXT("English chars append after the last commit"),
		Host->FieldValue, FString(TEXT("你好\n世界\n测试abc")));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
static const TCHAR* GRealDocument = TEXT(R"RML(<!-- imedemo.rml - IME 输入验证页（示例） -->
<rml>
<head>
<style>
/*
 * vacuus-base.rcss — the CLI's base stylesheet (@vacuus/cli, spec §3.1).
 *
 * WHY IT EXISTS: RmlUi ships NO user-agent stylesheet (the arch spec's §13 risk
 * row). In a browser `div` is display:block because the UA sheet says so; in
 * RmlUi every element defaults to inline, margins are 0, and a web developer's
 * first document collapses into one line. This sheet is the missing UA layer:
 * link it FIRST, put your own sheet after it.
 *
 * Other §13 gotchas this file cannot fix but you must know:
 *  - box-shadow is NOT animatable (RmlUi refuses the key at animation start,
 *    ElementAnimation.cpp:640-648) — `vacuus lint` catches it at authoring time.
 *  - THERE IS NO `ease` FAMILY. `transition: opacity 0.3s ease-in-out;` does not
 *    "work loosely" — it drops the WHOLE declaration, with one generic
 *    "Syntax error parsing property declaration". `ease-in-out` misses the
 *    keyword map (PropertyParserAnimation.cpp:240), the `%fs` sscanf for a
 *    duration fails (:267), so it falls to the property-name branch where
 *    GetShorthand and GetProperty both return null and ParseTransition returns
 *    false (:301-315). The complete tween table is :27-77 — eleven families,
 *      back bounce circular cubic elastic exponential linear quadratic
 *      quartic quintic sine
 *    each with `-in`, `-out`, `-in-out`. `cubic-in-out` is the closest thing to
 *    CSS's `ease-in-out`; write that. (Proven side by side in one sheet: the
 *    `ease-in-out` line logged the syntax error, the `cubic-in-out` line was
 *    silent and applied.)
 *  - CASE: `animation` lowercases each token before the lookup (:133), and
 *    `transition` does NOT (:240). So `Cubic-Out` animates and the same
 *    spelling silently kills a transition. Write tween keywords lowercase.
 *  - position: absolute resolves against the nearest ancestor with
 *    position: relative|absolute (RmlUi has no browser-style default-positioned
 *    root chain past the document).
 *  - There is no default font: name a loaded family below, or text logs
 *    "No font face defined" per layout pass. The plugin loads LatoLatin; a
 *    project shipping its OWN faces has exactly one route, `@font-face` in
 *    RCSS, and its `src` is ROOT-relative, not document-relative like `<link>`
 *    and `<script src>` — see gotchas.md #5.
 */

/* The block layer of the missing UA sheet. */
body,
div,
p,
h1,
h2,
h3,
h4,
section,
header,
footer,
nav,
ul,
ol,
li,
form {
	display: block;
}

/* The inline layer. */
span,
a,
b,
i,
em,
strong,
label,
code {
	display: inline;
}

body {
	/* The document fills its context; without this, absolute children of body
	   resolve percentages against a zero-sized box. */
	width: 100%;
	height: 100%;

	/* LatoLatin is the face the VaCuus engine registers for its own demos
	   (FVaCuusEngine::Initialize). Replace with your registered font. */
	font-family: LatoLatin;
	font-size: 16dp;
	color: #ffffff;
}

h1 {
	font-size: 28dp;
}

h2 {
	font-size: 22dp;
}

/* imedemo.rcss -- IME verification page.
 *
 * Two hard rules, both from the VaCuus IME postmortem:
 *   1. CJK needs a registered fallback face. LatoLatin has no CJK glyphs and
 *      a missing glyph renders as a tofu box, which is a FONT problem, not an
 *      IME problem. Do not debug the IME for it.
 *   2. The input's height / padding / line-height must be defined together and
 *      with box-sizing:border-box. RmlUi lays text out from the top of the
 *      content box, so a field taller than its line-height pushes the text (and
 *      the caret) up off the baseline.
 */

/* CJK face. src is ROOT-relative to Content/DevUI and takes no url(). */
@font-face {
	src: fonts/SimHei.ttf;
	font-family: "CJK";
	-rmlui-fallback-face: true;
}

/* No user-agent sheet: blockify the common elements first. */
body, div, p, h1, label, ul, li, form { display: block; }
span, a, b, i, em, strong, code { display: inline; }

body {
	width: 100%;
	height: 100%;
	font-family: LatoLatin;
	font-size: 15px;
	color: #e6edf7;
	background: #080b14;
}

#backdrop-glow {
	position: absolute;
	left: 18%;
	top: 6%;
	width: 420px;
	height: 420px;
	border-radius: 210px;
	background: #1b3a6b;
	opacity: 0.35;
}

#panel {
	position: relative;
	width: 560px;
	margin: 40px auto;
	padding: 28px 30px 30px 30px;
	background: #0e1522;
	border-radius: 14px;
	border: 1px solid #1e2b42;
}

#header { margin-bottom: 22px; }
#title-row { margin-bottom: 6px; }

#badge {
	display: inline-block;
	padding: 3px 9px;
	margin-right: 10px;
	background: #1f6feb;
	border-radius: 6px;
	color: #ffffff;
	font-size: 12px;
	vertical-align: middle;
}

h1 {
	display: inline;
	font-size: 24px;
	color: #f2f7ff;
	vertical-align: middle;
}

.subtitle {
	font-size: 13px;
	color: #6e82a3;
	margin: 0 0 0 2px;
}

.card {
	padding: 16px 18px;
	margin-bottom: 14px;
	background: #121b2c;
	border-radius: 10px;
	border: 1px solid #1c2942;
}

.field-label {
	font-size: 12px;
	color: #7f93b5;
	margin-bottom: 9px;
	letter-spacing: 0.4px;
}

/* height 46 = 24 line + 11 top + 11 bottom. All three fixed together. */
.text-input {
	box-sizing: border-box;
	width: 100%;
	height: 46px;
	padding: 11px 14px;
	background: #070c16;
	border: 1px solid #253550;
	border-radius: 8px;
	color: #e6edf7;
	font-family: LatoLatin;
	font-size: 16px;
	line-height: 24px;
}

.text-input:focus {
	border-color: #1f6feb;
	background: #08101d;
}

/* height 96 = 24 * 3 lines + 12 + 12 */
.text-area {
	box-sizing: border-box;
	width: 100%;
	height: 96px;
	padding: 12px 14px;
	background: #070c16;
	border: 1px solid #253550;
	border-radius: 8px;
	color: #e6edf7;
	font-size: 15px;
	line-height: 24px;
}

.text-area:focus { border-color: #1f6feb; }

.hint {
	margin-top: 8px;
	font-size: 12px;
	color: #5d7091;
}

#result-row {
	display: block;
	padding: 7px 0;
	border-bottom: 1px solid #182338;
}

.kv {
	display: inline-block;
	width: 90px;
	font-size: 13px;
	color: #7f93b5;
}

.value {
	display: inline-block;
	font-size: 15px;
	color: #39d353;
}

.checklist ul {
	list-style-type: none;
	padding: 0;
	margin: 0;
}

.checklist li {
	padding: 5px 0 5px 20px;
	font-size: 13px;
	color: #93a4c1;
	border-bottom: 1px solid #131d2e;
}
</style>
	<title>IME 输入验证</title>
	
	
</head>
<body>
	<div id="backdrop-glow"/>

	<div id="panel">
		<div id="header">
			<div id="title-row">
				<span id="badge">IME</span>
				<h1>中文输入验证</h1>
			</div>
			<p class="subtitle">VaCuus · RmlUi · UE Win64</p>
		</div>

		<div class="card">
			<label class="field-label">单行输入</label>
			<input type="text" id="single" class="text-input" placeholder="在这里输入：nihao"/>
			<div class="hint">选择候选词后，框内应只剩「你好」，不是 nihao</div>
		</div>

		<div class="card">
			<label class="field-label">多行输入</label>
			<textarea id="multi" class="text-area" placeholder="多行中文输入测试…"></textarea>
		</div>

		<div class="card" id="result-card">
			<label class="field-label">实时结果</label>
			<div id="result-row">
				<span class="kv">single 值</span>
				<span id="committed" class="value">—</span>
			</div>
			<div id="result-row">
				<span class="kv">multi 值</span>
				<span id="multivalue" class="value">—</span>
			</div>
			<div id="result-row">
				<span class="kv">字符数</span>
				<span id="charcount" class="value">0</span>
			</div>
			<div id="result-row">
				<span class="kv">事件数</span>
				<span id="eventcount" class="value">0</span>
			</div>
			<div class="hint">轮询显示（150ms）。原生 IME 上屏不走 DOM 事件，事件数只计普通键盘 textinput；innerHTML/textContent 在本 DOM 子集是空操作，故用 innerRML。若某行框内看不见文字而这里仍有值 → 渲染层问题，值未丢。</div>
		</div>

		<div class="card checklist">
			<label class="field-label">验收清单</label>
			<ul>
				<li>候选窗出现在输入框附近</li>
				<li>选词后只留中文，没有拼音残留</li>
				<li>Esc 取消不插入文本</li>
				<li>Backspace 只删拼音</li>
				<li>点击别处仍能穿透到游戏</li>
			</ul>
		</div>
	</div>

	
</body>
</rml>
)RML");

namespace VaCuusImeMultilineTest
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVaCuusImeRealPageTest, "VaCuus.Input.MultilineRealPage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVaCuusImeRealPageTest::RunTest(const FString& Parameters)
{
	using namespace VaCuusImeMultilineTest;

	if (!FPlatformProcess::SupportsMultithreading())
	{
		AddInfo(TEXT("Skipped: no multithreading support"));
		return true;
	}

	FVaCuusModule& Module = FVaCuusModule::Get();
	FVaCuusUIThread* UIThread = Module.GetOrStartUIThread();
	if (!TestNotNull(TEXT("UI thread"), UIThread))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		Module.StopUIThread();
	};

	const FIntPoint ViewSize(800, 900);
	const TSharedRef<FVaCuusViewStatus> Status = MakeShared<FVaCuusViewStatus>();
	TUniquePtr<FProbeHost> OwnedHost = MakeUnique<FProbeHost>();
	FProbeHost* Host = OwnedHost.Get();
	const uint32 ViewId = UIThread->AllocateViewId();
	UIThread->EnqueueAddView(ViewId, MoveTemp(OwnedHost), ViewSize, Status);
	UIThread->EnqueueLoadDocumentFromMemory(ViewId, GRealDocument, 1);

	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	TStrongObjectPtr<UVaCuusSubsystem> Subsystem(NewObject<UVaCuusSubsystem>(GameInstance.Get()));
	TStrongObjectPtr<UVaCuusView> View(NewObject<UVaCuusView>(Subsystem.Get()));
	View->InitializeView(Subsystem.Get(), ViewId, Status, ViewSize);
	const TSharedRef<FVaCuusSlateElement> Element = MakeShared<FVaCuusSlateElement>();
	TSharedRef<SVaCuusWidget> Widget = SNew(SVaCuusWidget, View.Get(), Element);
	ON_SCOPE_EXIT { Widget->DetachView(); };
	const FGeometry Geometry = FGeometry::MakeRoot(FVector2f(ViewSize.X, ViewSize.Y), FSlateLayoutTransform());

	if (!TestTrue(TEXT("Frames ran"), RunFrames(*UIThread, 3)))
	{
		return false;
	}
	View->PollStatus();

	// The real page centers a 560px panel; probe where #multi actually landed.
	AddInfo(FString::Printf(TEXT("#multi rect: %s"), *Host->MultiRect.ToString()));
	const FIntPoint Click(
		(Host->MultiRect.Min.X + Host->MultiRect.Max.X) / 2,
		(Host->MultiRect.Min.Y + Host->MultiRect.Max.Y) / 2);
	if (!TestTrue(TEXT("#multi has a sane rect on the real page"),
			Host->MultiRect.Max.X > Host->MultiRect.Min.X && Host->MultiRect.Max.Y > Host->MultiRect.Min.Y))
	{
		return false;
	}

	{
		const TSet<FKey> LeftOnly = {EKeys::LeftMouseButton};
		Widget->OnMouseButtonDown(Geometry, MakePointerEvent(Click, LeftOnly, EKeys::LeftMouseButton));
		Widget->OnMouseButtonUp(Geometry, MakePointerEvent(Click, TSet<FKey>(), EKeys::LeftMouseButton));
		if (!TestTrue(TEXT("Frame after click"), RunFrames(*UIThread, 2)))
		{
			return false;
		}
		View->PollStatus();
	}

	ITextInputMethodContext* ImeContext = View->GetImeContextForTesting();
	if (!TestNotNull(TEXT("IME context on real page"), ImeContext))
	{
		return false;
	}

	// IME commit 你好 on the real textarea.
	ImeContext->BeginComposition();
	ImeContext->SetTextInRange(0, 0, TEXT("nihao"));
	ImeContext->SetTextInRange(0, 5, TEXT("你好"));
	ImeContext->EndComposition();
	if (!TestTrue(TEXT("Frame after commit"), RunFrames(*UIThread, 2)))
	{
		return false;
	}
	View->PollStatus();
	TestEqual(TEXT("Real page: 你好 committed into #multi"), Host->MultiValue, FString(TEXT("你好")));

	// English typing on the same field.
	Widget->OnKeyChar(Geometry, MakeCharacterEvent(TEXT('x')));
	Widget->OnKeyChar(Geometry, MakeCharacterEvent(TEXT('y')));
	if (!TestTrue(TEXT("Frame after English"), RunFrames(*UIThread, 2)))
	{
		return false;
	}
	View->PollStatus();
	TestEqual(TEXT("Real page: English appended"), Host->MultiValue, FString(TEXT("你好xy")));

	return true;
}
}	 // namespace VaCuusImeMultilineTest

#endif	 // WITH_DEV_AUTOMATION_TESTS
