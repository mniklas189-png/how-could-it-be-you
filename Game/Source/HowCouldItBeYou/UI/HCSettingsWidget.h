#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/HCKeyBindings.h"
#include "Voice/HCAudioDevices.h"
#include "HCSettingsWidget.generated.h"

class APlayerController;
class APlayerState;
class FHCKeyListener;
class UHorizontalBox;
class UVerticalBox;
class UAudioComponent;
class UHCSettingsWidget;
class UButton;
class USizeBox;
class UBorder;
class UTextBlock;
class UWidget;

/** A button of the settings menu: UButton::OnClicked has no parameters, so each button gets one of these. */
UCLASS()
class HOWCOULDITBEYOU_API UHCSettingsClickProxy : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UHCSettingsWidget> Owner;

	/** A row, -1 - page for the buttons that switch the page, KeyRowBase + action for a key button, ResetKeysRow. */
	int32 Row = 0;
	int32 Delta = 0;

	UFUNCTION()
	void HandleClick();
};

/**
 * Personal settings on five pages: general (language, mouse, field of view, shock effects), picture (quality level,
 * resolution of the 3D picture, window, frame rate, FPS counter, brightness, then the single parts of the quality;
 * see Core/HCGraphics.h), sound (total
 * volume, one volume per kind of sound with a short sample,
 * volume in the background), controls (a key for every action: click, then press the new key) and voice chat
 * (microphone with a level display, headset/speakers, talking, the volume of all voices and, in a lobby or a round,
 * of each other player). Opened from the main menu and from the pause menu,
 * lies on top of them. Every change takes effect at once and is saved.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	enum ERow : int32
	{
		// general
		Language, Mouse, InvertY, Fov, Shock,
		// picture: what matters most first (one quality level for everything, the resolution of the 3D picture in
		// percent of the window), at the end the single parts of the quality for those who want to set them apart
		Quality, Resolution, Display, FrameLimit, VSync, FpsCounter, Gamma,
		AntiAliasing, Shadows, Textures, Effects, PostProcess, Reflections,
		// sound: the total, then one volume per kind of sound (HCSoundVolumes::EGroup), then the background
		Volume, EffectsVolume, ScaresVolume, AmbienceVolume, SignalsVolume, BackgroundVolume,
		// voice chat
		MicDevice, MicLevel, OutputDevice, VoiceOn, VoiceMode, TalkKey, MicGain, MicThreshold, VoiceVolume,
		RowCount
	};
	enum EPage : int32 { General, Picture, Sound, Controls, Voice, PageCount };
	/** Click proxy rows beyond the normal rows: one per action on the controls page, and "default keys". */
	static constexpr int32 KeyRowBase = 1000;
	static constexpr int32 ResetKeysRow = 999;
	/** Click proxy rows of the other players' volumes (voice page): PlayerRowBase + index in ShownPlayers. */
	static constexpr int32 PlayerRowBase = 2000;

	/** Opens the settings on top of whatever this player sees. */
	static UHCSettingsWidget* Open(APlayerController* PC);

	/** One step down (-1) or up (+1) in a row; a row below 0 switches the page (see the click proxy). */
	void Adjust(int32 Row, int32 Delta);
	void SelectPage(int32 Page);

	/** Waits for the next key or mouse button for this action (what clicking its key button does). */
	void StartListening(EHCBinding Binding);
	/** The key the player pressed while waiting (Esc cancels); true = taken in. */
	bool FinishListening(const FKey& Key);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void HandleClose();

	void Refresh();
	/** The resolution row shows the real size of the 3D picture; it follows the window while that is dragged. */
	void RefreshResolution();
	static EPage PageOf(int32 Row);
	bool HasVoiceChat() const;
	/** The choices for the two device rows (the Windows default first). */
	void LoadDevices();
	void StopListening();
	/** After a sound row changed: a short sample at the new volume (the previous sample stops). */
	void PlaySample(int32 Row);
	UHCSettingsClickProxy* MakeProxy(int32 Row, int32 Delta);
	UButton* MakeKeyButton(int32 ProxyRow, UTextBlock*& OutLabel);
	/** Adds "< value >" for this row to the end of a line and returns the value's text (small: device names, two lines). */
	UTextBlock* AddStepper(UHorizontalBox* Line, int32 Row, bool bSmallValue);

	/** The other players in this lobby or round, by name (one each, sorted). Empty in the main menu. */
	TArray<APlayerState*> FindOtherPlayers() const;
	/** Builds the lines of the other players' volumes again when the players changed (voice page). */
	void UpdatePlayerLines();
	void RefreshPlayerValues();

	/** Voice page, below the own rows: one volume per other player. */
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> PlayerBox;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> PlayerNames;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> PlayerValues;

	/** Names of the players the lines are for, and their player states (to light up whoever is talking). */
	TArray<FString> ShownPlayers;
	TArray<TWeakObjectPtr<APlayerState>> ShownPlayerStates;
	double NextPlayerCheck = 0.0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Values;

	/** One line per row (ERow), then one per action on the controls page, then the "default keys" line. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWidget>> Lines;

	/** Key name on the button of every action (controls page). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> KeyLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> PageButtons;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> PageHint;

	/** Microphone level: the bar's filled part. */
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> MeterFill;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> MeterFillColor;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UHCSettingsClickProxy>> Proxies;

	int32 CurrentPage = 0;
	/** Action waiting for its new key, or Count. */
	EHCBinding Listening = EHCBinding::Count;
	TSharedPtr<FHCKeyListener> KeyListener;

	TArray<HCAudioDevices::FDevice> Microphones;
	TArray<HCAudioDevices::FDevice> Outputs;
	bool bOutputsKnown = false;
	FHCMicMeter MicMeter;
	float ShownLevel = 0.f;
	/** What the resolution row shows right now (set again only when it changes). */
	FString ShownResolution;
	TWeakObjectPtr<UAudioComponent> Sample;
};
