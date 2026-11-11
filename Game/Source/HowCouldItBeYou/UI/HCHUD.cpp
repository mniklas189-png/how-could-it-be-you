#include "UI/HCHUD.h"

#include "Core/HCKeyBindings.h"
#include "Core/HCText.h"
#include "Blueprint/UserWidget.h"
#include "CanvasItem.h"
#include "Abilities/HCKillComponent.h"
#include "Characters/HCCharacter.h"
#include "Characters/HCGhostPawn.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/HCInteractionComponent.h"
#include "Misc/PackageName.h"
#include "UI/HCLobbyWidget.h"
#include "UI/HCMeetingWidget.h"
#include "UI/HCShapeshiftWidget.h"
#include "UI/HCStationMapWidget.h"
#include "World/HCStationBuilder.h"
#include "Sabotage/HCPowerSector.h"
#include "EngineUtils.h"
#include "Abilities/HCShapeshiftComponent.h"
#include "UI/HCPauseWidget.h"
#include "UI/HCResultWidget.h"
#include "UI/HCTaskOverlayWidget.h"
#include "Tasks/HCTaskComponent.h"
#include "Tasks/HCTaskStation.h"

static TAutoConsoleVariable<int32> CVarHCShowDebug(
	TEXT("hc.ShowDebug"), 0,
	TEXT("0 = off, 1 = phase/timer/own role, 2 = also all roles (host only, docs/02 §18.1)"));

namespace
{
	const FLinearColor HudText(0.85f, 0.88f, 0.92f, 0.9f);
	const FLinearColor HudDim(0.85f, 0.88f, 0.92f, 0.25f);
	const FLinearColor HudWarn(1.f, 0.35f, 0.25f, 0.9f);
	const FLinearColor MonsterRed(0.85f, 0.1f, 0.08f, 1.f);
	const FLinearColor CrewBlue(0.45f, 0.8f, 1.f, 1.f);
}

void AHCHUD::BeginPlay()
{
	Super::BeginPlay();
	// Lobby panel opens by itself on the lobby map; on a directly opened match map it starts closed
	const FString LobbyShort = FPackageName::GetShortName(UHCGameSettings::Get()->LobbyMap);
	bLobbyMenuOpen = GetWorld() && GetWorld()->GetMapName().EndsWith(LobbyShort);
}

// ---------------------------------------------------------------------------
// Screens

void AHCHUD::TogglePauseMenu()
{
	bPauseOpen = !bPauseOpen;
	if (bPauseOpen && PauseWidget)
	{
		PauseWidget->ResetConfirm();
	}
	UpdateScreens();
}

void AHCHUD::ToggleLobbyMenu()
{
	// Tab: lobby panel in the lobby, task list during a round
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (GS && GS->GetMatchPhase() != EHCMatchPhase::Lobby)
	{
		bTaskListCollapsed = !bTaskListCollapsed;
		return;
	}
	bLobbyMenuOpen = !bLobbyMenuOpen;
	UpdateScreens();
}

void AHCHUD::OpenTaskOverlay(AHCTaskStation* Station)
{
	if (!Station || !PlayerOwner || bPauseOpen)
	{
		return;
	}
	if (TaskOverlay)
	{
		TaskOverlay->Cancel();
		ShowWidget(TaskOverlay, false);
	}
	TaskOverlay = CreateWidget<UHCTaskOverlayWidget>(PlayerOwner, UHCTaskOverlayWidget::StaticClass());
	TaskOverlay->Setup(Station);
	UpdateScreens();
}

void AHCHUD::ShowWidget(UUserWidget* Widget, bool bShow)
{
	if (!Widget)
	{
		return;
	}
	if (bShow && !Widget->IsInViewport())
	{
		Widget->AddToViewport(10);
	}
	else if (!bShow && Widget->IsInViewport())
	{
		Widget->RemoveFromParent();
	}
}

void AHCHUD::UpdateScreens()
{
	APlayerController* PC = PlayerOwner;
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!PC || !GS || !PC->IsLocalController())
	{
		return;
	}
	const EHCMatchPhase Phase = GS->GetMatchPhase();
	if (Phase != LastPhase)
	{
		// New phase: close menus that belong to the old one
		if (Phase == EHCMatchPhase::RoleReveal || Phase == EHCMatchPhase::Intro || Phase == EHCMatchPhase::GameOver)
		{
			bPauseOpen = false;
		}
		LastPhase = Phase;
	}

	// The task overlay closes when it is done, when the round phase ends and when the menu opens
	if (TaskOverlay && (TaskOverlay->IsFinished() || Phase != EHCMatchPhase::Playing || bPauseOpen))
	{
		TaskOverlay->Cancel();
		ShowWidget(TaskOverlay, false);
		TaskOverlay = nullptr;
	}
	const bool bShowPause = bPauseOpen;
	const bool bShowResult = Phase == EHCMatchPhase::GameOver && !bShowPause;
	const bool bShowLobby = Phase == EHCMatchPhase::Lobby && bLobbyMenuOpen && !bShowPause;
	const bool bShowMeeting = Phase == EHCMatchPhase::Meeting && !bShowPause;
	if (Phase != EHCMatchPhase::Playing || bShowPause || TaskOverlay)
	{
		bShapeMenuOpen = false;
	}
	const bool bShowShape = bShapeMenuOpen;
	if (Phase != EHCMatchPhase::Playing || bShowPause || TaskOverlay || bShowShape)
	{
		bMapOpen = false;
	}
	const bool bShowMap = bMapOpen;

	if (bShowPause && !PauseWidget)
	{
		PauseWidget = CreateWidget<UHCPauseWidget>(PC, UHCPauseWidget::StaticClass());
	}
	if (bShowResult && !ResultWidget)
	{
		ResultWidget = CreateWidget<UHCResultWidget>(PC, UHCResultWidget::StaticClass());
	}
	if (bShowLobby && !LobbyWidget)
	{
		LobbyWidget = CreateWidget<UHCLobbyWidget>(PC, UHCLobbyWidget::StaticClass());
	}
	ShowWidget(PauseWidget, bShowPause);
	ShowWidget(ResultWidget, bShowResult);
	ShowWidget(LobbyWidget, bShowLobby);
	if (bShowMeeting && !MeetingWidget)
	{
		MeetingWidget = CreateWidget<UHCMeetingWidget>(PC, UHCMeetingWidget::StaticClass());
	}
	ShowWidget(MeetingWidget, bShowMeeting);
	if (bShowShape && !ShapeshiftWidget)
	{
		ShapeshiftWidget = CreateWidget<UHCShapeshiftWidget>(PC, UHCShapeshiftWidget::StaticClass());
	}
	ShowWidget(ShapeshiftWidget, bShowShape);
	if (bShowMap && !StationMapWidget)
	{
		StationMapWidget = CreateWidget<UHCStationMapWidget>(PC, UHCStationMapWidget::StaticClass());
	}
	ShowWidget(StationMapWidget, bShowMap);
	ShowWidget(TaskOverlay, TaskOverlay != nullptr);

	UUserWidget* Focus = bShowPause ? static_cast<UUserWidget*>(PauseWidget) : bShowResult ? static_cast<UUserWidget*>(ResultWidget)
		: bShowMeeting ? static_cast<UUserWidget*>(MeetingWidget) : bShowShape ? static_cast<UUserWidget*>(ShapeshiftWidget) : bShowMap ? static_cast<UUserWidget*>(StationMapWidget) : TaskOverlay ? static_cast<UUserWidget*>(TaskOverlay) : bShowLobby ? static_cast<UUserWidget*>(LobbyWidget) : nullptr;
	ApplyInputMode(Focus);
}

void AHCHUD::ApplyInputMode(UUserWidget* FocusWidget)
{
	const bool bWantUI = FocusWidget != nullptr;
	if (bWantUI == bUIModeActive && FocusedWidget.Get() == FocusWidget)
	{
		return;
	}
	bUIModeActive = bWantUI;
	FocusedWidget = FocusWidget;
	if (bWantUI)
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(FocusWidget->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerOwner->SetInputMode(Mode);
		PlayerOwner->SetShowMouseCursor(true);
	}
	else
	{
		PlayerOwner->SetInputMode(FInputModeGameOnly());
		PlayerOwner->SetShowMouseCursor(false);
	}
	// Stop sprinting/walking that was held when the menu opened
	PlayerOwner->FlushPressedKeys();
}

// ---------------------------------------------------------------------------
// Drawing

void AHCHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	UpdateScreens();
	if (FPlatformTime::Seconds() < JumpscareUntil)
	{
		return;
	}

	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	const EHCMatchPhase Phase = GS ? GS->GetMatchPhase() : EHCMatchPhase::Lobby;
	AHCCharacter* Character = Cast<AHCCharacter>(GetOwningPawn());

	switch (Phase)
	{
	case EHCMatchPhase::Loading:
		DrawLoading(GS);
		break;
	case EHCMatchPhase::Intro:
		DrawIntro(GS);
		break;
	case EHCMatchPhase::RoleReveal:
		DrawRoleReveal(GS);
		break;
	case EHCMatchPhase::Ejection:
		DrawEjection(GS);
		break;
	case EHCMatchPhase::GameOver:
		break; // result widget
	default:
		DrawBuildInfo();
		if (Character && !bUIModeActive)
		{
			DrawCrosshair();
			DrawPrompt(Character);
		}
		if (Character)
		{
			DrawBattery(Character);
			DrawStamina(Character);
			if (Phase == EHCMatchPhase::Playing)
			{
				DrawKillIndicator(Character);
				DrawShapeshiftIndicator(Character);
			}
		}
		else if (AHCGhostPawn* Ghost = Cast<AHCGhostPawn>(GetOwningPawn()))
		{
			DrawGhostHud(Ghost);
		}
		if (Phase == EHCMatchPhase::Lobby && !bLobbyMenuOpen && !bPauseOpen)
		{
			DrawLobbyHint(GS);
		}
		if (GS && Phase != EHCMatchPhase::Lobby)
		{
			DrawTaskList(PlayerOwner ? PlayerOwner->GetPlayerState<AHCPlayerState>() : nullptr);
			DrawTaskBar(GS);
			DrawPowerBanner();
			DrawMiniMap();
		}
		break;
	}
	DrawCenterMessage();
	if (GS)
	{
		DrawAnnouncements(GS);
		DrawDebugOverlay(GS);
	}
}

void AHCHUD::DrawCenteredText(const FString& Text, float Y, const FLinearColor& Color, float Scale, UFont* Font)
{
	UFont* UseFont = Font ? Font : GEngine->GetMediumFont();
	float W = 0.f, H = 0.f;
	GetTextSize(Text, W, H, UseFont, Scale);
	DrawText(Text, Color, (Canvas->ClipX - W) * 0.5f, Y, UseFont, Scale);
}

void AHCHUD::DrawFullscreen(const FLinearColor& Color)
{
	DrawRect(Color, 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
}

void AHCHUD::DrawLoading(const AHCGameState* GS)
{
	DrawFullscreen(FLinearColor(0.f, 0.f, 0.f, 1.f));
	DrawCenteredText(NSLOCTEXT("HC", "HudWaitAll", "Warte auf alle Spieler ...").ToString(), Canvas->ClipY * 0.5f - 10.f, HudText, 1.4f);
}

void AHCHUD::DrawIntro(const AHCGameState* GS)
{
	// Prototype intro: text panel (docs/06 §2), the Sequencer cutscene follows in M10
	DrawFullscreen(FLinearColor(0.f, 0.f, 0.f, 1.f));
	const float Y = Canvas->ClipY * 0.42f;
	DrawCenteredText(NSLOCTEXT("HC", "IntroLine1", "Kepler-7, Tag 41.").ToString(), Y - 60.f, HudDim, 1.2f);
	DrawCenteredText(NSLOCTEXT("HC", "IntroLine2", "Die Probe aus dem Eis von Europa ist ausgebrochen. Der Kommandant ist tot.").ToString(), Y - 20.f, HudText, 1.1f);
	DrawCenteredText(NSLOCTEXT("HC", "IntroLine3", "Es ist einer von euch.").ToString(), Y + 40.f, MonsterRed, 2.2f, GEngine->GetLargeFont());
	DrawCenteredText(FString::Printf(TEXT("%d"), FMath::CeilToInt(GS->GetPhaseRemainingSeconds())), Canvas->ClipY - 60.f, HudDim, 1.f);
}

void AHCHUD::DrawRoleReveal(const AHCGameState* GS)
{
	DrawFullscreen(FLinearColor(0.f, 0.f, 0.f, 1.f));
	const AHCPlayerState* PS = PlayerOwner ? PlayerOwner->GetPlayerState<AHCPlayerState>() : nullptr;
	// Show the role only once our own role with the current serial arrived (docs/02 §5.4)
	if (!PS || PS->GetRoleInfo().MatchSerial != GS->GetMatchSerial())
	{
		return;
	}
	const bool bMonster = PS->GetRoleInfo().Role == EHCRole::Monster;
	const float Y = Canvas->ClipY * 0.36f;
	// Same layout and duration for both roles, so nothing can be read from the outside (docs/01 §13)
	DrawCenteredText(bMonster ? NSLOCTEXT("HC", "RevealMonster", "DER WANDLER").ToString() : NSLOCTEXT("HC", "RevealCrew", "CREW").ToString(), Y, bMonster ? MonsterRed : CrewBlue, 3.f, GEngine->GetLargeFont());
	DrawCenteredText(bMonster ? NSLOCTEXT("HC", "RevealMonsterGoal", "Töte. Täusche. Werde einer von ihnen.").ToString() : NSLOCTEXT("HC", "RevealCrewGoal", "Erledige deine Aufgaben. Finde den Wandler.").ToString(), Y + 90.f, HudText, 1.3f);

	const int32 Identity = FMath::Max(0, PS->GetIdentity());
	const FString IdentityLine = FString::Printf(TEXT("%s  ·  %s  ·  %s"), *PS->GetPlayerName(),
		*HCIdentity::ColorName(Identity).ToString(), *HCIdentity::SymbolName(Identity).ToString());
	const float SwatchY = Y + 160.f;
	float W = 0.f, H = 0.f;
	GetTextSize(IdentityLine, W, H, GEngine->GetMediumFont(), 1.2f);
	const float X = (Canvas->ClipX - W) * 0.5f;
	DrawRect(HCIdentity::Color(Identity), X - 34.f, SwatchY + 2.f, 22.f, 22.f);
	DrawText(IdentityLine, HudText, X, SwatchY, GEngine->GetMediumFont(), 1.2f);
	DrawCenteredText(FString::Printf(TEXT("%d"), FMath::CeilToInt(GS->GetPhaseRemainingSeconds())), Canvas->ClipY - 60.f, HudDim, 1.f);
}

void AHCHUD::DrawMiniMap()
{
	// Small plan of the station in the top right corner. It shows the rooms and where you stand yourself -
	// no other players, no tasks, no remains: finding out who is where stays the job of your own eyes.
	const APawn* Pawn = GetOwningPawn();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	if (!Pawn || !GS || GS->GetMatchPhase() != EHCMatchPhase::Playing)
	{
		return;
	}
	if (!MiniMapBuilder.IsValid())
	{
		for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
		{
			MiniMapBuilder = *It;
		}
	}
	const AHCStationBuilder* Builder = MiniMapBuilder.Get();
	if (!Builder || Builder->GetLayout().SizeX <= 0 || Builder->GetLayout().Spaces.Num() < 2)
	{
		return;
	}
	const FHCStationLayout& Layout = Builder->GetLayout();
	// Wide enough for the room names, but never more than a fifth of the screen
	const float Width = FMath::Clamp(Canvas->ClipX * 0.2f, 250.f, 360.f);
	const float Scale = Width / Layout.SizeX;
	const float Height = Layout.SizeY * Scale;
	const float X0 = Canvas->ClipX - Width - 30.f;
	const float Y0 = 66.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X0 - 8.f, Y0 - 8.f, Width + 16.f, Height + 38.f);
	// A lightning bolt: the sign for "no power here"
	const float Blink = 0.6f + 0.4f * FMath::Sin(GetWorld()->GetTimeSeconds() * 7.f);
	auto DrawBolt = [this](float CX, float CY, float Size, const FLinearColor& Color)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.75f * Color.A), CX - Size * 0.8f, CY - Size * 1.2f, Size * 1.6f, Size * 2.4f);
		DrawLine(CX + 0.3f * Size, CY - Size, CX - 0.4f * Size, CY + 0.12f * Size, Color, 2.f);
		DrawLine(CX - 0.4f * Size, CY + 0.12f * Size, CX + 0.4f * Size, CY - 0.12f * Size, Color, 2.f);
		DrawLine(CX + 0.4f * Size, CY - 0.12f * Size, CX - 0.3f * Size, CY + Size, Color, 2.f);
	};
	UFont* NameFont = GEngine->GetTinyFont();
	for (const FHCSpace& Space : Layout.Spaces)
	{
		// A sector without power is dark red, like in the big plan
		const AHCPowerSector* Power = AHCPowerSector::Find(GetWorld(), static_cast<uint8>(Space.Sector));
		const bool bDark = Power && !Power->IsPowered();
		const FLinearColor Color = bDark ? (Space.bCorridor ? FLinearColor(0.30f, 0.06f, 0.06f, 0.9f) : FLinearColor(0.45f, 0.08f, 0.08f, 0.92f))
			: (Space.bCorridor ? FLinearColor(0.30f, 0.33f, 0.38f, 0.85f) : FLinearColor(0.50f, 0.55f, 0.62f, 0.9f));
		const float RX = X0 + Space.Rect.Min.X * Scale, RY = Y0 + Space.Rect.Min.Y * Scale;
		const float RW = Space.Rect.Width() * Scale - 1.f, RH = Space.Rect.Height() * Scale - 1.f;
		DrawRect(Color, RX, RY, RW, RH);
	}
	// Names and signs on top of all rooms, so that no neighbour covers them
	for (const FHCSpace& Space : Layout.Spaces)
	{
		if (Space.bCorridor)
		{
			continue;
		}
		const AHCPowerSector* Power = AHCPowerSector::Find(GetWorld(), static_cast<uint8>(Space.Sector));
		const bool bDark = Power && !Power->IsPowered();
		const float RX = X0 + Space.Rect.Min.X * Scale, RY = Y0 + Space.Rect.Min.Y * Scale;
		const float RW = Space.Rect.Width() * Scale - 1.f, RH = Space.Rect.Height() * Scale - 1.f;
		// Room name (short forms where the room is small on the plan)
		const FString Label = HCText::RoomShort(Space.Name).ToString();
		float TW = 0.f, TH = 0.f;
		GetTextSize(Label, TW, TH, NameFont, 1.f);
		DrawText(Label, bDark ? FLinearColor(1.f, 0.8f, 0.75f, 1.f) : FLinearColor(0.05f, 0.06f, 0.08f, 1.f), RX + (RW - TW) * 0.5f, RY + 2.f, NameFont, 1.f);
		if (bDark)
		{
			DrawBolt(RX + RW * 0.5f, RY + RH * 0.5f + 5.f, 8.f, FLinearColor(1.f, 0.85f, 0.15f, Blink));
		}
	}
	// Where the power comes back: the fuse box of a dark sector, framed
	for (const FHCFuseBoxDef& Box : Layout.FuseBoxes)
	{
		const AHCPowerSector* Power = AHCPowerSector::Find(GetWorld(), static_cast<uint8>(Box.Sector));
		if (Power && !Power->IsPowered())
		{
			const float BX = X0 + static_cast<float>(Box.Position.X) * Scale, BY = Y0 + static_cast<float>(Box.Position.Y) * Scale;
			const FLinearColor Frame(1.f, 0.55f, 0.1f, Blink);
			DrawBolt(BX, BY, 6.f, Frame);
			DrawLine(BX - 7.f, BY - 9.f, BX + 7.f, BY - 9.f, Frame, 1.5f);
			DrawLine(BX - 7.f, BY + 9.f, BX + 7.f, BY + 9.f, Frame, 1.5f);
			DrawLine(BX - 7.f, BY - 9.f, BX - 7.f, BY + 9.f, Frame, 1.5f);
			DrawLine(BX + 7.f, BY - 9.f, BX + 7.f, BY + 9.f, Frame, 1.5f);
		}
	}
	// Where you are right now, in words
	const FName Here = Builder->GetSpaceNameAt(Pawn->GetActorLocation());
	if (Layout.FindSpace(Here))
	{
		DrawText(FText::Format(NSLOCTEXT("HC", "HudWhereFmt", "ORT:  {0}"), HCText::Room(Here).ToUpper()).ToString(), FLinearColor(0.93f, 0.89f, 0.83f, 0.95f), X0, Y0 + Height + 8.f, GEngine->GetSmallFont(), 1.f);
	}
	// Your own open tasks (yellow; dim while a step is not ready yet). Nobody else's.
	const AHCPlayerState* OwnState = PlayerOwner ? PlayerOwner->GetPlayerState<AHCPlayerState>() : nullptr;
	if (const UHCTaskComponent* OwnTasks = OwnState ? OwnState->GetTaskComponent() : nullptr)
	{
		for (TActorIterator<AHCTaskStation> It(GetWorld()); It; ++It)
		{
			float Wait = 0.f;
			if (OwnTasks->FindPendingIndexAt(*It, &Wait) != INDEX_NONE)
			{
				const FVector At = Builder->GetActorTransform().InverseTransformPosition(It->GetActorLocation()) / 100.0;
				DrawRect(Wait > 0.f ? FLinearColor(0.6f, 0.5f, 0.15f, 0.9f) : FLinearColor(1.f, 0.85f, 0.2f, 1.f),
					X0 + static_cast<float>(At.X) * Scale - 3.f, Y0 + static_cast<float>(At.Y) * Scale - 3.f, 6.f, 6.f);
			}
		}
	}
	const FVector Local = Builder->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation()) / 100.0;
	const float PX = X0 + static_cast<float>(Local.X) * Scale;
	const float PY = Y0 + static_cast<float>(Local.Y) * Scale;
	const float Yaw = FMath::DegreesToRadians((PlayerOwner ? PlayerOwner->GetControlRotation().Yaw : 0.f) - Builder->GetActorRotation().Yaw);
	const FLinearColor Me(0.93f, 0.89f, 0.83f, 1.f);
	DrawLine(PX, PY, PX + FMath::Cos(Yaw) * 10.f, PY + FMath::Sin(Yaw) * 10.f, Me, 2.f);
	DrawRect(Me, PX - 3.f, PY - 3.f, 6.f, 6.f);
}

void AHCHUD::ToggleStationMap()
{
	bMapOpen = !bMapOpen && !bPauseOpen && !TaskOverlay && !bShapeMenuOpen;
	UpdateScreens();
}

void AHCHUD::CloseStationMap()
{
	if (bMapOpen)
	{
		bMapOpen = false;
		UpdateScreens();
	}
}

bool AHCHUD::StationMapSabotage(uint8 SabotageType, FName TargetId)
{
	return bMapOpen && StationMapWidget && StationMapWidget->IsInViewport() && StationMapWidget->RequestSabotage(SabotageType, TargetId);
}

void AHCHUD::DrawPowerBanner()
{
	// Stays as long as a sector is dark, so nobody misses the outage (docs/01 §7 rule 6)
	for (TActorIterator<AHCPowerSector> It(GetWorld()); It; ++It)
	{
		if (!It->IsPowered())
		{
			const bool bBright = FMath::Fmod(GetWorld()->GetTimeSeconds(), 1.2f) < 0.8f;
			DrawCenteredText(FText::Format(NSLOCTEXT("HC", "HudPowerOutFmt", "STROMAUSFALL  ·  SEKTOR {0}  ·  Notstrom in {1} s"), AHCPowerSector::GetDisplayName(It->GetSectorIndex()).ToUpper(),
				FMath::CeilToInt(It->GetSecondsUntilAutoRestore())).ToString(), 58.f, bBright ? HudWarn : FLinearColor(1.f, 0.35f, 0.25f, 0.45f), 1.f);
		}
	}
}

void AHCHUD::ToggleShapeshiftMenu()
{
	bShapeMenuOpen = !bShapeMenuOpen && !bPauseOpen && !TaskOverlay;
	UpdateScreens();
}

void AHCHUD::CloseShapeshiftMenu()
{
	if (bShapeMenuOpen)
	{
		bShapeMenuOpen = false;
		UpdateScreens();
	}
}

bool AHCHUD::PickShapeshiftTarget(int32 TargetPlayerId)
{
	return bShapeMenuOpen && ShapeshiftWidget && ShapeshiftWidget->IsInViewport() && ShapeshiftWidget->Pick(TargetPlayerId);
}

void AHCHUD::DrawShapeshiftIndicator(AHCCharacter* Character)
{
	// Only the Wandler ever sees this (docs/01 §13)
	const AHCPlayerState* PS = PlayerOwner ? PlayerOwner->GetPlayerState<AHCPlayerState>() : nullptr;
	if (!PS || PS->GetRoleInfo().Role != EHCRole::Monster)
	{
		return;
	}
	const UHCShapeshiftComponent* Shift = Character->GetShapeshift();
	FString Label;
	FLinearColor Color = HudDim;
	switch (Shift->GetState())
	{
	case EHCShapeshiftState::Transforming:
		Label = NSLOCTEXT("HC", "HudShifting", "VERWANDLUNG ...").ToString();
		Color = MonsterRed;
		break;
	case EHCShapeshiftState::Reverting:
		Label = NSLOCTEXT("HC", "HudReverting", "RÜCKVERWANDLUNG ...").ToString();
		Color = MonsterRed;
		break;
	case EHCShapeshiftState::Disguised:
	{
		const int32 Left = FMath::CeilToInt(Shift->GetStateRemaining());
		if (Shift->IsTrueForm())
		{
			Label = FText::Format(NSLOCTEXT("HC", "HudTrueFormFmt", "WAHRE GESTALT  {0} s  [{1}] beendet  -  alle sehen dich"), Left, HCKeys::ShortName(EHCBinding::TrueForm)).ToString();
			Color = MonsterRed;
			break;
		}
		Label = Left > 0 ? FText::Format(NSLOCTEXT("HC", "HudDisguisedTimeFmt", "ALS {0}  {1} s  [{2}] beendet"), FText::FromString(Shift->GetDisguiseName().ToUpper()), Left, HCKeys::ShortName(EHCBinding::Shapeshift)).ToString()
			: FText::Format(NSLOCTEXT("HC", "HudDisguisedFmt", "ALS {0}  [{1}] beendet"), FText::FromString(Shift->GetDisguiseName().ToUpper()), HCKeys::ShortName(EHCBinding::Shapeshift)).ToString();
		Color = CrewBlue;
		break;
	}
	default:
	{
		const float Cooldown = Shift->GetCooldownRemaining();
		Label = Cooldown > 0.f ? FText::Format(NSLOCTEXT("HC", "HudShiftCooldownFmt", "VERWANDELN  {0} s"), FMath::CeilToInt(Cooldown)).ToString()
			: FText::Format(NSLOCTEXT("HC", "HudShiftReadyFmt", "VERWANDELN [{0}]    WAHRE GESTALT [{1}]"), HCKeys::ShortName(EHCBinding::Shapeshift), HCKeys::ShortName(EHCBinding::TrueForm)).ToString();
		Color = Cooldown > 0.f ? HudDim : FLinearColor(0.85f, 0.88f, 0.92f, 0.5f);
		break;
	}
	}
	float W = 0.f, H = 0.f;
	GetTextSize(Label, W, H, GEngine->GetMediumFont(), 1.f);
	const float X = Canvas->ClipX - 40.f - FMath::Max(156.f, W);
	const float Y = Canvas->ClipY - 140.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), X - 8.f, Y - 6.f, FMath::Max(156.f, W) + 16.f, 30.f);
	DrawText(Label, Color, X, Y, GEngine->GetMediumFont(), 1.f);
}

bool AHCHUD::SubmitMeetingVote(int32 TargetPlayerId)
{
	return MeetingWidget && MeetingWidget->IsInViewport() && MeetingWidget->SubmitVote(TargetPlayerId);
}

void AHCHUD::DrawEjection(const AHCGameState* GS)
{
	// Prototype ejection: text on black (docs/06 §2), the airlock sequence follows in M10
	DrawFullscreen(FLinearColor(0.f, 0.f, 0.f, 1.f));
	const float Y = Canvas->ClipY * 0.44f;
	DrawCenteredText(NSLOCTEXT("HC", "HudAirlock", "Die Luftschleuse öffnet sich.").ToString(), Y - 50.f, HudDim, 1.2f);
	DrawCenteredText(GS->GetMeetingText().ToString(), Y, HudText, 2.f, GEngine->GetLargeFont());
}

void AHCHUD::DrawLobbyHint(const AHCGameState* GS)
{
	DrawCenteredText(FText::Format(NSLOCTEXT("HC", "HudLobbyFmt", "LOBBY  ·  {0}: Lobby-Menü  ·  Esc/F1: Menü"), HCKeys::ShortName(EHCBinding::TaskList)).ToString(), 40.f, HudDim, 1.f);
}

void AHCHUD::DrawTaskList(const AHCPlayerState* PS)
{
	// Top left, same look for crew and monster (the monster sees its fake list), docs/01 §13
	const UHCTaskComponent* Tasks = PS ? PS->GetTaskComponent() : nullptr;
	if (!Tasks || Tasks->GetTasks().IsEmpty())
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const float X = 16.f;
	float Y = 44.f;
	const float LineHeight = 22.f;
	const int32 Lines = bTaskListCollapsed ? 1 : Tasks->GetTasks().Num() + 1;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), X - 8.f, Y - 6.f, 430.f, Lines * LineHeight + 10.f);
	DrawText(FText::Format(NSLOCTEXT("HC", "HudTasksFmt", "AUFGABEN  {0}/{1}   [{2}]"), Tasks->CountCompleted(), Tasks->GetTasks().Num(), HCKeys::ShortName(EHCBinding::TaskList)).ToString(), HudText, X, Y, Font, 1.f);
	if (bTaskListCollapsed)
	{
		return;
	}
	const UHCGameSettings* Settings = UHCGameSettings::Get();
	for (const FHCTaskEntry& Entry : Tasks->GetTasks())
	{
		Y += LineHeight;
		const FHCTaskDefinition* Def = Settings->FindTask(Entry.TaskId);
		const FString Name = Def ? Def->DisplayName.ToString() : Entry.TaskId.ToString();
		if (Entry.bCompleted)
		{
			DrawText(FString::Printf(TEXT("[x] %s"), *Name), FLinearColor(0.35f, 0.8f, 0.45f, 0.75f), X, Y, Font, 1.f);
			continue;
		}
		FString Line = FString::Printf(TEXT("[  ] %s"), *Name);
		if (Entry.NumSteps > 1)
		{
			Line += FString::Printf(TEXT(" (%d/%d)"), Entry.CurrentStep + 1, Entry.NumSteps);
		}
		if (Def && Def->Steps.IsValidIndex(Entry.CurrentStep))
		{
			Line += FString::Printf(TEXT("  -  %s"), *HCText::Room(Def->Steps[Entry.CurrentStep].Room).ToString());
		}
		const float Wait = Tasks->GetWaitSeconds(Entry);
		if (Wait > 0.f)
		{
			Line += TEXT("  ") + FText::Format(NSLOCTEXT("HC", "HudTaskWaitFmt", "(bereit in {0} s)"), FMath::CeilToInt(Wait)).ToString();
		}
		DrawText(Line, Wait > 0.f ? FLinearColor(0.5f, 0.7f, 1.f, 0.9f) : HudText, X, Y, Font, 1.f);
	}
}

void AHCHUD::DrawTaskBar(const AHCGameState* GS)
{
	// Top centre; only what the server replicates for the chosen lobby option (docs/02 §9.3)
	if (!GS->IsTaskBarVisible() || GS->GetTotalTasks() <= 0)
	{
		return;
	}
	const float W = 360.f;
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = 16.f;
	const float Fraction = FMath::Clamp(static_cast<float>(GS->GetCompletedTasks()) / GS->GetTotalTasks(), 0.f, 1.f);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X - 4.f, Y - 4.f, W + 8.f, 20.f);
	DrawRect(HudDim, X, Y, W, 12.f);
	DrawRect(FLinearColor(0.3f, 0.85f, 0.45f, 0.95f), X, Y, W * Fraction, 12.f);
	DrawCenteredText(NSLOCTEXT("HC", "HudCrewTasks", "AUFGABEN DER CREW").ToString(), Y + 18.f, HudDim, 1.f, GEngine->GetSmallFont());
}

void AHCHUD::ShowCenterMessage(const FText& Message, float Seconds)
{
	CenterMessage = Message;
	CenterMessageUntil = FPlatformTime::Seconds() + Seconds;
}

void AHCHUD::SetJumpscareActive(bool bActive)
{
	JumpscareUntil = bActive ? FPlatformTime::Seconds() + 6.0 : 0.0;
}

void AHCHUD::CloseMenusForJumpscare()
{
	bPauseOpen = false;
	if (TaskOverlay)
	{
		TaskOverlay->Cancel();
	}
	UpdateScreens();
}

void AHCHUD::DrawCenterMessage()
{
	if (CenterMessage.IsEmpty() || FPlatformTime::Seconds() >= CenterMessageUntil)
	{
		return;
	}
	DrawCenteredText(CenterMessage.ToString(), Canvas->ClipY * 0.36f, HudText, 1.8f, GEngine->GetLargeFont());
}

void AHCHUD::DrawKillIndicator(AHCCharacter* Character)
{
	// Only the Wandler ever sees this, and it makes no sound (docs/01 §13)
	const AHCPlayerState* PS = PlayerOwner ? PlayerOwner->GetPlayerState<AHCPlayerState>() : nullptr;
	if (!PS || PS->GetRoleInfo().Role != EHCRole::Monster)
	{
		return;
	}
	const UHCKillComponent* Kill = Character->GetKillComponent();
	const float Cooldown = Kill->GetCooldownRemaining();
	const bool bTarget = Kill->GetLocalTarget() != nullptr && Cooldown <= 0.f;
	const float X = Canvas->ClipX - 40.f - 156.f;
	const float Y = Canvas->ClipY - 104.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), X - 8.f, Y - 6.f, 172.f, 30.f);
	if (Cooldown > 0.f)
	{
		DrawText(FText::Format(NSLOCTEXT("HC", "HudKillCooldownFmt", "TÖTEN  {0} s"), FMath::CeilToInt(Cooldown)).ToString(), HudDim, X, Y, GEngine->GetMediumFont(), 1.f);
	}
	else
	{
		DrawText(FText::Format(NSLOCTEXT("HC", "HudKillReadyFmt", "TÖTEN  [{0}]"), HCKeys::ShortName(EHCBinding::Kill)).ToString(), bTarget ? MonsterRed : FLinearColor(0.85f, 0.88f, 0.92f, 0.5f), X, Y, GEngine->GetMediumFont(), 1.f);
	}
	if (bTarget && !bUIModeActive)
	{
		// Thin red ring around the crosshair: a target is in reach
		const float CX = Canvas->ClipX * 0.5f;
		const float CY = Canvas->ClipY * 0.5f;
		DrawRect(MonsterRed, CX - 9.f, CY - 9.f, 18.f, 2.f);
		DrawRect(MonsterRed, CX - 9.f, CY + 7.f, 18.f, 2.f);
		DrawRect(MonsterRed, CX - 9.f, CY - 9.f, 2.f, 18.f);
		DrawRect(MonsterRed, CX + 7.f, CY - 9.f, 2.f, 18.f);
	}
}

void AHCHUD::DrawGhostHud(AHCGhostPawn* Ghost)
{
	DrawCenteredText(NSLOCTEXT("HC", "HudGhost", "DU BIST EIN GEIST").ToString(), 60.f, FLinearColor(0.6f, 0.75f, 1.f, 0.8f), 1.2f);
	DrawCenteredText(FText::Format(NSLOCTEXT("HC", "HudGhostHelpFmt", "Lebende sehen und hören dich nicht  ·  {0} / {1}: hoch / runter  ·  {2}: eigene Aufgaben"), HCKeys::ShortName(EHCBinding::GhostUp), HCKeys::ShortName(EHCBinding::GhostDown), HCKeys::ShortName(EHCBinding::Interact)).ToString(), 86.f, HudDim, 1.f, GEngine->GetSmallFont());
	if (bUIModeActive)
	{
		return;
	}
	DrawCrosshair();
	DrawInteractPrompt(Ghost->GetPromptText().ToString(), Ghost->GetHoldProgress(), true);
}

void AHCHUD::DrawAnnouncements(const AHCGameState* GS)
{
	const FText Announcement = GS->GetAnnouncement();
	if (!Announcement.IsEmpty())
	{
		float TextW = 0.f, TextH = 0.f;
		GetTextSize(Announcement.ToString(), TextW, TextH, GEngine->GetMediumFont(), 1.2f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), (Canvas->ClipX - TextW) * 0.5f - 12.f, 108.f, TextW + 24.f, TextH + 8.f);
		DrawCenteredText(Announcement.ToString(), 112.f, HudText, 1.2f);
	}
	if (const AHCPlayerController* PC = Cast<AHCPlayerController>(PlayerOwner))
	{
		const FText Message = PC->GetClientMessage();
		if (!Message.IsEmpty() && !bUIModeActive)
		{
			DrawCenteredText(Message.ToString(), 142.f, HudWarn, 1.1f);
		}
	}
}

void AHCHUD::DrawDebugOverlay(const AHCGameState* GS)
{
	const int32 Level = CVarHCShowDebug.GetValueOnGameThread();
	if (Level <= 0)
	{
		return;
	}
	UFont* Font = GEngine->GetSmallFont();
	float Y = 40.f;
	auto Line = [&](const FString& Text, const FLinearColor& Color)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), 12.f, Y - 1.f, 420.f, 16.f);
		DrawText(Text, Color, 16.f, Y, Font, 1.f);
		Y += 16.f;
	};
	const ENetMode NetMode = GetWorld()->GetNetMode();
	Line(FString::Printf(TEXT("Phase %s  Rest %.1f s  Serial %d  Spieler %d  %s"), HCIdentity::PhaseName(GS->GetMatchPhase()),
		GS->GetPhaseRemainingSeconds(), GS->GetMatchSerial(), GS->PlayerArray.Num(),
		NetMode == NM_ListenServer ? TEXT("HOST") : NetMode == NM_Client ? TEXT("CLIENT") : TEXT("STANDALONE")), FLinearColor::Yellow);
	if (const AHCPlayerState* Own = PlayerOwner ? PlayerOwner->GetPlayerState<AHCPlayerState>() : nullptr)
	{
		Line(FString::Printf(TEXT("Ich: %s  Rolle %s (Serial %d)  lebt %d"), *Own->GetPlayerName(),
			Own->GetRoleInfo().Role == EHCRole::Monster ? TEXT("Monster") : TEXT("Crew"), Own->GetRoleInfo().MatchSerial, Own->IsAlive() ? 1 : 0), FLinearColor::Yellow);
	}
	// Level 2: all roles, only where they are really known = on the host (docs/02 §18.1)
	if (Level >= 2 && NetMode != NM_Client)
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			if (const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS))
			{
				const bool bMonster = HCPS->GetRoleInfo().Role == EHCRole::Monster;
				Line(FString::Printf(TEXT("  %s: %s%s"), *HCPS->GetPlayerName(), bMonster ? TEXT("MONSTER") : TEXT("Crew"), HCPS->IsAlive() ? TEXT("") : TEXT(" (tot)")),
					bMonster ? FLinearColor(1.f, 0.4f, 0.4f) : FLinearColor::White);
			}
		}
	}
}

void AHCHUD::DrawCrosshair()
{
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	DrawRect(HudDim, CX - 2.f, CY - 2.f, 4.f, 4.f);
}

void AHCHUD::DrawPrompt(AHCCharacter* Character)
{
	const UHCInteractionComponent* Interaction = Character->GetInteraction();
	if (!Interaction)
	{
		return;
	}
	DrawInteractPrompt(Interaction->GetPromptText().ToString(), Interaction->GetHoldProgress(), Interaction->IsFocusAvailable());
}

void AHCHUD::DrawInteractPrompt(const FString& Prompt, float Progress, bool bAvailable)
{
	if (Prompt.IsEmpty() && Progress < 0.f)
	{
		return;
	}
	// Same amber as the task markers on the maps: "here you can do something"
	const FLinearColor Accent(1.f, 0.78f, 0.18f, 1.f);
	const FLinearColor Plate(0.02f, 0.025f, 0.035f, 0.82f);
	UFont* Font = GEngine->GetMediumFont();

	// "[E] Kern kalibrieren" -> key cap "E" + label "Kern kalibrieren"
	FString Key;
	FString Label = Prompt;
	int32 Close = INDEX_NONE;
	if (Prompt.StartsWith(TEXT("[")) && Prompt.FindChar(TEXT(']'), Close) && Close > 1)
	{
		Key = Prompt.Mid(1, Close - 1);
		Label = Prompt.Mid(Close + 1).TrimStartAndEnd();
	}

	const float LabelScale = 1.15f;
	float LW = 0.f, LH = 0.f;
	GetTextSize(Label.IsEmpty() ? FString(TEXT(" ")) : Label, LW, LH, Font, LabelScale);
	float KW = 0.f, KH = 0.f;
	if (!Key.IsEmpty())
	{
		GetTextSize(Key, KW, KH, Font, 1.1f);
	}
	const float Pad = 14.f, Gap = 12.f;
	const float CapH = LH + 8.f;
	const float CapW = Key.IsEmpty() ? 0.f : FMath::Max(CapH, KW + 18.f);
	const float PlateW = FMath::Max(Pad * 2.f + CapW + (Key.IsEmpty() ? 0.f : Gap) + LW, 220.f);
	const float PlateH = CapH + 16.f;
	const float PX = (Canvas->ClipX - PlateW) * 0.5f;
	const float PY = Canvas->ClipY * 0.5f + 34.f;

	auto Frame = [this](float X, float Y, float W, float H, float T, const FLinearColor& C)
	{
		DrawRect(C, X, Y, W, T);
		DrawRect(C, X, Y + H - T, W, T);
		DrawRect(C, X, Y + T, T, H - 2.f * T);
		DrawRect(C, X + W - T, Y + T, T, H - 2.f * T);
	};
	auto ShadowText = [this](const FString& Text, const FLinearColor& C, float X, float Y, UFont* F, float Scale)
	{
		FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), F, C);
		Item.Scale = FVector2D(Scale, Scale);
		Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.9f), FVector2D(1.5f, 1.5f));
		Canvas->DrawItem(Item);
	};

	if (!Prompt.IsEmpty())
	{
		// Dark plate with a slowly breathing amber edge, so it stands out even in front of a lamp
		const float Pulse = bAvailable ? 0.7f + 0.3f * FMath::Sin(GetWorld()->GetRealTimeSeconds() * 4.f) : 0.f;
		DrawRect(Plate, PX, PY, PlateW, PlateH);
		Frame(PX, PY, PlateW, PlateH, 2.f, bAvailable ? FLinearColor(Accent.R, Accent.G, Accent.B, Pulse) : FLinearColor(0.6f, 0.62f, 0.66f, 0.5f));

		float X = PX + (PlateW - (CapW + (Key.IsEmpty() ? 0.f : Gap) + LW)) * 0.5f;
		const float CapY = PY + (PlateH - CapH) * 0.5f;
		if (!Key.IsEmpty())
		{
			// Key cap: solid amber with a dark letter, a darker lower lip like a real key
			const FLinearColor CapColor = bAvailable ? Accent : FLinearColor(0.55f, 0.57f, 0.6f, 1.f);
			DrawRect(CapColor * FLinearColor(0.55f, 0.55f, 0.55f, 1.f), X, CapY + 3.f, CapW, CapH);
			DrawRect(CapColor, X, CapY, CapW, CapH - 1.f);
			DrawText(Key, FLinearColor(0.04f, 0.04f, 0.05f, 1.f), X + (CapW - KW) * 0.5f, CapY + (CapH - 1.f - KH) * 0.5f, Font, 1.1f);
			X += CapW + Gap;
		}
		ShadowText(Label, bAvailable ? FLinearColor::White : FLinearColor(0.75f, 0.77f, 0.8f, 1.f), X, PY + (PlateH - LH) * 0.5f, Font, LabelScale);
	}

	if (Progress >= 0.f)
	{
		// Hold bar right under the plate
		const float W = PlateW;
		const float X = PX;
		const float Y = PY + PlateH + 6.f;
		DrawRect(Plate, X, Y, W, 10.f);
		DrawRect(Accent, X + 2.f, Y + 2.f, (W - 4.f) * Progress, 6.f);
	}
}

void AHCHUD::DrawBattery(AHCCharacter* Character)
{
	// Segments instead of numbers (docs/01 §13), bottom right, always visible but subtle
	constexpr int32 Segments = 10;
	const float SegW = 12.f, SegH = 18.f, Gap = 4.f;
	const float TotalW = Segments * SegW + (Segments - 1) * Gap;
	const float X0 = Canvas->ClipX - TotalW - 40.f;
	const float Y0 = Canvas->ClipY - 50.f;
	const float Battery = Character->GetBatteryFraction();
	const int32 Lit = FMath::CeilToInt(Battery * Segments - KINDA_SMALL_NUMBER);
	const FLinearColor On = Battery < 0.2f ? HudWarn : HudText;
	for (int32 i = 0; i < Segments; ++i)
	{
		DrawRect(i < Lit ? On : HudDim, X0 + i * (SegW + Gap), Y0, SegW, SegH);
	}
	DrawText(Character->IsFlashlightOn() ? NSLOCTEXT("HC", "HudLampOn", "LAMPE AN").ToString() : FText::Format(NSLOCTEXT("HC", "HudLampOffFmt", "LAMPE [{0}]"), HCKeys::ShortName(EHCBinding::Flashlight)).ToString(), HudDim, X0, Y0 - 22.f, GEngine->GetSmallFont(), 1.f);
}

void AHCHUD::DrawStamina(AHCCharacter* Character)
{
	const float Stamina = Character->GetStaminaFraction();
	if (Stamina >= 0.999f && !Character->IsSprinting())
	{
		return; // only while sprinting / recovering
	}
	const float W = 200.f;
	const float X = (Canvas->ClipX - W) * 0.5f;
	const float Y = Canvas->ClipY - 40.f;
	DrawRect(HudDim, X, Y, W, 4.f);
	DrawRect(Stamina < 0.25f ? HudWarn : HudText, X, Y, W * Stamina, 4.f);
}

void AHCHUD::DrawBuildInfo()
{
	DrawText(NSLOCTEXT("HC", "HudBuildInfo", "HOW COULD IT BE YOU  ·  Prototyp M7").ToString(), HudDim, 16.f, 12.f, GEngine->GetSmallFont(), 1.f);
}
