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
		if (RmlDocument)
		{
			if (Rml::Element* Field = RmlDocument->GetElementById("field"))
			{
				if (Rml::ElementFormControl* Control = VaCuusCastFormControl(*Field))
				{
					FieldValue = UTF8_TO_TCHAR(Control->GetValue().c_str());
				}
			}
		}

		Status->FramesRecorded.fetch_add(1, std::memory_order_release);
	}

	FString FieldValue;

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

	return true;
}

#endif	 // WITH_DEV_AUTOMATION_TESTS
