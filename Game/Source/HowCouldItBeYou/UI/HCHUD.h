#pragma once

#include "CoreMinimal.h"
#include "Core/HCTypes.h"
#include "GameFramework/HUD.h"
#include "HCHUD.generated.h"

class AHCCharacter;
class AHCGhostPawn;
class AHCGameState;
class AHCPlayerState;
class AHCTaskStation;
class UHCTaskOverlayWidget;
class UHCLobbyWidget;
class UHCPauseWidget;
class UHCResultWidget;
class UUserWidget;

/**
 * Switches screens by phase (docs/02 §16.1) and draws the prototype overlays with the canvas.
 * No names over heads, no list of the living, no kill feed (docs/01 §10).
 */
UCLASS()
class HOWCOULDITBEYOU_API AHCHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	void TogglePauseMenu();
	void ToggleLobbyMenu();

	/** Opens the overlay of a Sequence/Minigame step (called by the player controller). */
	void OpenTaskOverlay(AHCTaskStation* Station);
	bool IsTaskOverlayOpen() const { return TaskOverlay != nullptr; }

	/** Large text in the middle of the screen for a few seconds, e.g. "Du bist tot." */
	void ShowCenterMessage(const FText& Message, float Seconds);
	/** The victim of a kill loses its menus and task windows at once (docs/02 §7.2). */
	void CloseMenusForJumpscare();
	/** While a jumpscare runs nothing is drawn over the picture. Ends by itself after a few seconds at the latest. */
	void SetJumpscareActive(bool bActive);

protected:
	virtual void BeginPlay() override;

	void UpdateScreens();
	void ShowWidget(UUserWidget* Widget, bool bShow);
	void ApplyInputMode(UUserWidget* FocusWidget);

	// Canvas overlays
	void DrawCrosshair();
	void DrawPrompt(AHCCharacter* Character);
	void DrawBattery(AHCCharacter* Character);
	void DrawStamina(AHCCharacter* Character);
	void DrawBuildInfo();
	void DrawFullscreen(const FLinearColor& Color);
	void DrawIntro(const AHCGameState* GS);
	void DrawRoleReveal(const AHCGameState* GS);
	void DrawLoading(const AHCGameState* GS);
	void DrawAnnouncements(const AHCGameState* GS);
	void DrawLobbyHint(const AHCGameState* GS);
	void DrawTaskList(const AHCPlayerState* PS);
	void DrawTaskBar(const AHCGameState* GS);
	void DrawKillIndicator(AHCCharacter* Character);
	void DrawGhostHud(AHCGhostPawn* Ghost);
	void DrawCenterMessage();
	void DrawDebugOverlay(const AHCGameState* GS);
	void DrawCenteredText(const FString& Text, float Y, const FLinearColor& Color, float Scale = 1.f, class UFont* Font = nullptr);

	UPROPERTY(Transient) TObjectPtr<UHCLobbyWidget> LobbyWidget;
	UPROPERTY(Transient) TObjectPtr<UHCResultWidget> ResultWidget;
	UPROPERTY(Transient) TObjectPtr<UHCPauseWidget> PauseWidget;
	UPROPERTY(Transient) TObjectPtr<UHCTaskOverlayWidget> TaskOverlay;

	bool bTaskListCollapsed = false;
	FText CenterMessage;
	double CenterMessageUntil = 0.0;
	double JumpscareUntil = 0.0;

	bool bLobbyMenuOpen = true;
	bool bPauseOpen = false;
	bool bUIModeActive = false;
	TWeakObjectPtr<UUserWidget> FocusedWidget;
	EHCMatchPhase LastPhase = EHCMatchPhase::Lobby;
};
