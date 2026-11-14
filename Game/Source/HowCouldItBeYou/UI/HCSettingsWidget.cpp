#include "UI/HCSettingsWidget.h"

#include "Audio/HCSoundVolumes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameInstance.h"
#include "Core/HCGraphics.h"
#include "Core/HCLog.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interfaces/VoiceInterface.h"
#include "OnlineSubsystemUtils.h"
#include "Scalability.h"
#include "UI/HCFpsOverlay.h"
#include "UI/HCUIStyle.h"
#include "UnrealClient.h"
#include "Voice/HCVoiceBoost.h"
#include "Voice/HCVoiceSubsystem.h"

/** While the settings wait for a new key: takes the next key or mouse button before anything else sees it. */
class FHCKeyListener : public IInputProcessor
{
public:
	TWeakObjectPtr<UHCSettingsWidget> Owner;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		return Owner.IsValid() && Owner->FinishListening(InKeyEvent.GetKey());
	}
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		return Owner.IsValid() && Owner->FinishListening(MouseEvent.GetEffectingButton());
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("HCKeyListener"); }
};

namespace
{
	/** The entry that follows the system's own device: "Windows-Standard" on Windows, elsewhere (Linux) just "System" (same in German and English). */
	FText DefaultDeviceText()
	{
		const FText WindowsDefault = NSLOCTEXT("HC", "DeviceDefault", "Windows-Standard");
		return PLATFORM_WINDOWS ? WindowsDefault : FText::AsCultureInvariant(TEXT("System"));
	}

	const FLinearColor ValueColor(0.95f, 0.92f, 0.62f);
	const FLinearColor MeterTrack(0.05f, 0.058f, 0.074f, 1.f);
	const float ValueWidth = 250.f;
	const float StepWidth = 44.f;
	/** Width of "< value >": the key buttons and the level bar are as wide. */
	const float ControlWidth = StepWidth + 8.f + ValueWidth + 8.f + StepWidth;

	/** Microphone level 0..1 for the bar: speech lies around 0.02..0.3 */
	float BarFraction(float Level)
	{
		return FMath::Clamp(FMath::Sqrt(FMath::Max(Level, 0.f) / 0.3f), 0.f, 1.f);
	}

	/** Open microphone: how loud a sound must be to count as speech (step 1 = everything, step 10 = only loud voices). */
	float MicThresholdOf(float Step)
	{
		return 0.01f + 0.022f * (Step - 1.f);
	}

	/** Device names can be long; the value box shows two lines at most. */
	FText DeviceText(const FString& Name)
	{
		return FText::FromString(Name.Len() > 44 ? Name.Left(42) + TEXT("...") : Name);
	}

	/** Step through a device list (index 0 = Windows default) from the saved id. */
	int32 IndexOf(const TArray<HCAudioDevices::FDevice>& Devices, const FString& Id)
	{
		for (int32 Index = 0; Index < Devices.Num(); ++Index)
		{
			if (Devices[Index].Id == Id)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/** The kind of sound a row of the sound page sets; Count for the other rows (the total, the background). */
	HCSoundVolumes::EGroup GroupOfRow(int32 Row)
	{
		using HCSoundVolumes::EGroup;
		return Row == UHCSettingsWidget::EffectsVolume ? EGroup::Effects
			: Row == UHCSettingsWidget::ScaresVolume ? EGroup::Scares
			: Row == UHCSettingsWidget::AmbienceVolume ? EGroup::Ambience
			: Row == UHCSettingsWidget::SignalsVolume ? EGroup::Signals : EGroup::Count;
	}

	/** The part of the picture quality a row of the picture page sets; none for the other rows. */
	int32* QualityPartOfRow(Scalability::FQualityLevels& Levels, int32 Row)
	{
		switch (Row)
		{
		case UHCSettingsWidget::AntiAliasing: return &Levels.AntiAliasingQuality;
		case UHCSettingsWidget::Shadows: return &Levels.ShadowQuality;
		case UHCSettingsWidget::Textures: return &Levels.TextureQuality;
		case UHCSettingsWidget::Effects: return &Levels.EffectsQuality;
		case UHCSettingsWidget::PostProcess: return &Levels.PostProcessQuality;
		case UHCSettingsWidget::Reflections: return &Levels.ReflectionQuality;
		default: return nullptr;
		}
	}

	/** Resolution of the 3D picture a player set, in percent of the window (0 in the saved settings = never set = full). */
	float SavedResolutionPercent(const UGameUserSettings* Video)
	{
		const float Percent = Video ? Video->ScalabilityQuality.ResolutionQuality : 100.f;
		return Percent <= 0.f ? 100.f : FMath::Clamp(Percent, HCGraphics::MinResolutionPercent, 100.f);
	}
}

void UHCSettingsClickProxy::HandleClick()
{
	if (Owner)
	{
		Owner->Adjust(Row, Delta);
	}
}

UHCSettingsWidget* UHCSettingsWidget::Open(APlayerController* PC)
{
	UHCSettingsWidget* Widget = PC ? CreateWidget<UHCSettingsWidget>(PC, UHCSettingsWidget::StaticClass()) : nullptr;
	if (Widget)
	{
		Widget->AddToViewport(500);
		Widget->SetKeyboardFocus();
	}
	return Widget;
}

UHCSettingsWidget::EPage UHCSettingsWidget::PageOf(int32 Row)
{
	if (Row >= RowCount)
	{
		return Controls; // the lines of the actions and "default keys"
	}
	return Row <= Shock ? General : Row <= Reflections ? Picture : Row <= BackgroundVolume ? Sound : Voice;
}

bool UHCSettingsWidget::HasVoiceChat() const
{
	// Switched off in DefaultEngine.ini ([OnlineSubsystem] bHasVoiceEnabled): only the device choice is shown
	return Online::GetVoiceInterface(GetWorld()).IsValid();
}

UHCSettingsClickProxy* UHCSettingsWidget::MakeProxy(int32 Row, int32 Delta)
{
	UHCSettingsClickProxy* Proxy = NewObject<UHCSettingsClickProxy>(this);
	Proxy->Owner = this;
	Proxy->Row = Row;
	Proxy->Delta = Delta;
	Proxies.Add(Proxy);
	return Proxy;
}

UTextBlock* UHCSettingsWidget::AddStepper(UHorizontalBox* Line, int32 Row, bool bSmallValue)
{
	UTextBlock* Value = nullptr;
	for (const int32 Delta : { -1, 1 })
	{
		UButton* Step = HCUI::Button(WidgetTree, Delta < 0 ? FText::FromString(TEXT("<")) : FText::FromString(TEXT(">")), HCUI::ButtonColor, nullptr, 16);
		Step->OnClicked.AddDynamic(MakeProxy(Row, Delta), &UHCSettingsClickProxy::HandleClick);
		USizeBox* StepBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		StepBox->SetWidthOverride(StepWidth);
		StepBox->SetContent(Step);
		if (Delta > 0)
		{
			// The value sits between the two buttons; device names are long and get a smaller font and two lines
			Value = HCUI::Text(WidgetTree, FText::GetEmpty(), bSmallValue ? 13 : 16, ValueColor, true);
			Value->SetJustification(ETextJustify::Center);
			Value->SetAutoWrapText(bSmallValue);
			USizeBox* ValueBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			ValueBox->SetWidthOverride(ValueWidth);
			if (USizeBoxSlot* ValueSlot = Cast<USizeBoxSlot>(ValueBox->SetContent(Value)))
			{
				ValueSlot->SetVerticalAlignment(VAlign_Center);
			}
			HCUI::AddColumn(Line, ValueBox, 8.f);
		}
		HCUI::AddColumn(Line, StepBox, Delta < 0 ? 8.f : 0.f);
	}
	return Value;
}

TArray<APlayerState*> UHCSettingsWidget::FindOtherPlayers() const
{
	TArray<APlayerState*> Found;
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World && World->GetNetMode() != NM_Standalone ? World->GetGameState() : nullptr;
	const APlayerController* PC = GetOwningPlayer();
	if (!GameState || !PC)
	{
		return Found;
	}
	// One line per name. While a map change settles a player can be listed twice (the copy from the previous map)
	const FString OwnName = PC->PlayerState ? PC->PlayerState->GetPlayerName() : FString();
	TMap<FString, APlayerState*> ByName;
	for (APlayerState* PS : GameState->PlayerArray)
	{
		if (!IsValid(PS) || PS == PC->PlayerState || PS->GetPlayerName().IsEmpty() || PS->GetPlayerName() == OwnName)
		{
			continue;
		}
		APlayerState*& Chosen = ByName.FindOrAdd(PS->GetPlayerName());
		if (!Chosen || (Chosen->IsFromPreviousLevel() && !PS->IsFromPreviousLevel()))
		{
			Chosen = PS;
		}
	}
	ByName.KeySort(TLess<FString>());
	ByName.GenerateValueArray(Found);
	return Found;
}

void UHCSettingsWidget::UpdatePlayerLines()
{
	if (!PlayerBox)
	{
		return;
	}
	const TArray<APlayerState*> Players = FindOtherPlayers();
	TArray<FString> Names;
	for (const APlayerState* PS : Players)
	{
		Names.Add(PS->GetPlayerName());
	}
	ShownPlayerStates.Reset();
	for (APlayerState* PS : Players)
	{
		ShownPlayerStates.Add(PS);
	}
	if (Names == ShownPlayers && PlayerBox->GetChildrenCount() > 0)
	{
		return;
	}
	ShownPlayers = Names;
	PlayerBox->ClearChildren();
	PlayerNames.Reset();
	PlayerValues.Reset();
	UWidgetTree* Tree = WidgetTree;
	HCUI::AddRow(PlayerBox, HCUI::Text(Tree, NSLOCTEXT("HC", "PlayersVoiceTitle", "STIMMEN DER MITSPIELER"), 15, HCUI::TextColor, true), 4.f);
	UTextBlock* Hint = HCUI::Text(Tree, Names.IsEmpty() ? NSLOCTEXT("HC", "PlayersVoiceNone", "Sobald du in einer Lobby oder Runde bist, stehen hier alle Mitspieler.")
		: NSLOCTEXT("HC", "PlayersVoiceHint", "Jeden einzeln lauter, leiser oder stumm stellen; wird nach Namen gespeichert. Wer gerade spricht, leuchtet grün."), 13, HCUI::MutedColor);
	Hint->SetAutoWrapText(true);
	HCUI::AddRow(PlayerBox, Hint, 10.f);
	for (int32 Index = 0; Index < Names.Num(); ++Index)
	{
		UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UTextBlock* Name = HCUI::Text(Tree, FText::FromString(Names[Index]), 16, HCUI::TextColor);
		HCUI::AddColumn(Line, Name, 16.f, true);
		PlayerNames.Add(Name);
		PlayerValues.Add(AddStepper(Line, PlayerRowBase + Index, false));
		HCUI::AddRow(PlayerBox, Line, 10.f);
	}
	RefreshPlayerValues();
}

void UHCSettingsWidget::RefreshPlayerValues()
{
	for (int32 Index = 0; Index < PlayerValues.Num() && Index < ShownPlayers.Num(); ++Index)
	{
		const float Volume = HCVoiceVolumes::OfPlayer(ShownPlayers[Index]);
		PlayerValues[Index]->SetText(Volume <= 0.001f ? NSLOCTEXT("HC", "VolumeMuted", "Stumm")
			: FText::Format(NSLOCTEXT("HC", "PercentFmt", "{0} %"), FMath::RoundToInt(Volume * 100.f)));
	}
}

UButton* UHCSettingsWidget::MakeKeyButton(int32 ProxyRow, UTextBlock*& OutLabel)
{
	UButton* Button = HCUI::Button(WidgetTree, FText::GetEmpty(), HCUI::ButtonColor, &OutLabel, 15);
	OutLabel->SetColorAndOpacity(FSlateColor(ValueColor));
	Button->OnClicked.AddDynamic(MakeProxy(ProxyRow, 0), &UHCSettingsClickProxy::HandleClick);
	return Button;
}

void UHCSettingsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	// Dims and covers what lies underneath, so nothing there can be clicked by accident
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.f, 0.f, 0.78f));

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(32.f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Box);
	HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "SettingsTitle", "EINSTELLUNGEN"), 26, HCUI::TextColor, true), 6.f);
	HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "SettingsHint", "Jede Änderung gilt sofort und wird gespeichert."), 13, HCUI::MutedColor), 16.f);

	// The pages
	const FText PageNames[PageCount] = { NSLOCTEXT("HC", "PageGeneral", "ALLGEMEIN"), NSLOCTEXT("HC", "PagePicture", "BILD"),
		NSLOCTEXT("HC", "PageSound", "TON"), NSLOCTEXT("HC", "PageControls", "STEUERUNG"), NSLOCTEXT("HC", "PageVoice", "SPRACH-CHAT") };
	UHorizontalBox* Pages = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	for (int32 Page = 0; Page < PageCount; ++Page)
	{
		UButton* Tab = HCUI::Button(Tree, PageNames[Page], HCUI::ButtonColor, nullptr, 13); // five tabs: "SPRACH-CHAT" must fit
		Tab->OnClicked.AddDynamic(MakeProxy(-1 - Page, 0), &UHCSettingsClickProxy::HandleClick);
		PageButtons.Add(Tab);
		HCUI::AddColumn(Pages, Tab, Page + 1 < PageCount ? 8.f : 0.f, true);
	}
	HCUI::AddRow(Box, Pages, 18.f);

	// The rows of all pages; only those of the chosen page are shown. Long pages scroll.
	UVerticalBox* Rows = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	Scroll->AddChild(Rows);
	Scroll->SetScrollbarThickness(FVector2D(6.f, 6.f));
	USizeBox* ScrollLimit = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	ScrollLimit->SetMaxDesiredHeight(560.f);
	ScrollLimit->SetContent(Scroll);
	HCUI::AddRow(Box, ScrollLimit, 4.f);

	const FText Labels[RowCount] = {
		NSLOCTEXT("HC", "SetLanguage", "Sprache / Language"),
		NSLOCTEXT("HC", "SetMouse", "Mausempfindlichkeit"),
		NSLOCTEXT("HC", "SetInvertY", "Maus hoch/runter umkehren"),
		NSLOCTEXT("HC", "SetFov", "Sichtfeld"),
		NSLOCTEXT("HC", "SetShock", "Schock-Effekte"),
		NSLOCTEXT("HC", "SetQuality", "Grafikqualität"),
		NSLOCTEXT("HC", "SetResolution", "Auflösung (3D-Bild)"),
		NSLOCTEXT("HC", "SetDisplay", "Anzeige"),
		NSLOCTEXT("HC", "SetFrameLimit", "Bilder pro Sekunde (Grenze)"),
		NSLOCTEXT("HC", "SetVSync", "VSync"),
		NSLOCTEXT("HC", "SetFpsCounter", "FPS-Anzeige"),
		NSLOCTEXT("HC", "SetGamma", "Helligkeit"),
		NSLOCTEXT("HC", "SetAntiAliasing", "Kantenglättung"),
		NSLOCTEXT("HC", "SetShadows", "Schatten und Lichtkegel"),
		NSLOCTEXT("HC", "SetTextures", "Texturen"),
		NSLOCTEXT("HC", "SetEffects", "Effekte"),
		NSLOCTEXT("HC", "SetPostProcess", "Nachbearbeitung"),
		NSLOCTEXT("HC", "SetReflections", "Spiegelungen"),
		NSLOCTEXT("HC", "SetVolumeTotal", "Gesamtlautstärke"),
		NSLOCTEXT("HC", "SetVolumeEffects", "Geräusche (Türen, Schalter)"),
		NSLOCTEXT("HC", "SetVolumeScares", "Schreck (Jumpscares, Schreie)"),
		NSLOCTEXT("HC", "SetVolumeAmbience", "Umgebung (Lüftung, Lampen)"),
		NSLOCTEXT("HC", "SetVolumeSignals", "Signale (Alarm, Rundenende)"),
		NSLOCTEXT("HC", "SetVolumeBackground", "Ton im Hintergrund"),
		NSLOCTEXT("HC", "SetMicDevice", "Mikrofon"),
		NSLOCTEXT("HC", "SetMicLevel", "Mikrofon-Pegel (sprich zum Testen)"),
		NSLOCTEXT("HC", "SetOutputDevice", "Ausgabe (Headset / Lautsprecher)"),
		NSLOCTEXT("HC", "SetVoiceOn", "Sprach-Chat"),
		NSLOCTEXT("HC", "SetVoiceMode", "Senden"),
		NSLOCTEXT("HC", "SetVoiceKey", "Sprech-Taste"),
		NSLOCTEXT("HC", "SetMicGain", "Mikrofon-Verstärkung"),
		NSLOCTEXT("HC", "SetMicThreshold", "Mikrofon-Schwelle (offen)"),
		NSLOCTEXT("HC", "SetVoiceVolumeAll", "Lautstärke aller Stimmen"),
	};
	for (int32 Row = 0; Row < RowCount; ++Row)
	{
		UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Line, HCUI::Text(Tree, Labels[Row], 16, HCUI::TextColor), 16.f, true);
		if (Row == TalkKey)
		{
			// The talk key is one of the actions of the controls page; it can be set here as well
			UTextBlock* KeyLabel = nullptr;
			USizeBox* KeyBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			KeyBox->SetWidthOverride(ControlWidth);
			KeyBox->SetContent(MakeKeyButton(KeyRowBase + static_cast<int32>(EHCBinding::Talk), KeyLabel));
			HCUI::AddColumn(Line, KeyBox, 0.f);
			Values.Add(KeyLabel);
		}
		else if (Row == MicLevel)
		{
			// A bar that fills with what the microphone hears
			UBorder* Track = HCUI::Panel(Tree, MeterTrack, FMargin(2.f));
			UHorizontalBox* Inside = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			MeterFill = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			MeterFill->SetWidthOverride(0.f);
			MeterFill->SetHeightOverride(14.f);
			MeterFillColor = HCUI::Panel(Tree, HCUI::AccentGreen, FMargin(0.f));
			MeterFill->SetContent(MeterFillColor);
			HCUI::AddColumn(Inside, MeterFill, 0.f);
			Track->SetContent(Inside);
			USizeBox* TrackBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			TrackBox->SetWidthOverride(ControlWidth);
			if (USizeBoxSlot* TrackSlot = Cast<USizeBoxSlot>(TrackBox->SetContent(Track)))
			{
				TrackSlot->SetVerticalAlignment(VAlign_Center);
			}
			HCUI::AddColumn(Line, TrackBox, 0.f);
			Values.Add(nullptr);
		}
		else
		{
			Values.Add(AddStepper(Line, Row, Row == MicDevice || Row == OutputDevice));
		}
		HCUI::AddRow(Rows, Line, 10.f);
		Lines.Add(Line);
	}

	// Voice page, under its own rows: the other players, each with a volume of their own (filled in UpdatePlayerLines)
	PlayerBox = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Rows, PlayerBox, 0.f);

	// Controls: one key per action
	for (int32 Index = 0; Index < HCKeys::Count; ++Index)
	{
		UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Line, HCUI::Text(Tree, HCKeys::Label(static_cast<EHCBinding>(Index)), 16, HCUI::TextColor), 16.f, true);
		UTextBlock* KeyLabel = nullptr;
		USizeBox* KeyBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		KeyBox->SetWidthOverride(ControlWidth);
		KeyBox->SetContent(MakeKeyButton(KeyRowBase + Index, KeyLabel));
		HCUI::AddColumn(Line, KeyBox, 0.f);
		KeyLabels.Add(KeyLabel);
		HCUI::AddRow(Rows, Line, 8.f);
		Lines.Add(Line);
	}
	{
		UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Line, HCUI::Text(Tree, NSLOCTEXT("HC", "KeysFixed", "Esc / F1: Pausemenü (fest)"), 14, HCUI::MutedColor), 16.f, true);
		UButton* Reset = HCUI::Button(Tree, NSLOCTEXT("HC", "KeysReset", "STANDARD-TASTEN"), HCUI::ButtonColor, nullptr, 14);
		Reset->OnClicked.AddDynamic(MakeProxy(ResetKeysRow, 0), &UHCSettingsClickProxy::HandleClick);
		USizeBox* ResetBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ResetBox->SetWidthOverride(ControlWidth);
		ResetBox->SetContent(Reset);
		HCUI::AddColumn(Line, ResetBox, 0.f);
		HCUI::AddRow(Rows, Line, 4.f);
		Lines.Add(Line);
	}

	PageHint = HCUI::Text(Tree, FText::GetEmpty(), 13, HCUI::MutedColor);
	PageHint->SetAutoWrapText(true);
	HCUI::AddRow(Box, PageHint, 14.f);

	UButton* Close = HCUI::Button(Tree, NSLOCTEXT("HC", "SettingsBack", "ZURÜCK"), HCUI::AccentRed);
	Close->OnClicked.AddDynamic(this, &UHCSettingsWidget::HandleClose);
	HCUI::AddRow(Box, Close, 0.f);

	HCUI::Place(Root, Panel, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 760.f);
	SelectPage(General);
}

void UHCSettingsWidget::NativeDestruct()
{
	StopListening();
	MicMeter.Stop();
	Super::NativeDestruct();
}

void UHCSettingsWidget::LoadDevices()
{
	Microphones.Reset();
	Microphones.Add({ FString(), DefaultDeviceText().ToString() });
	Microphones.Append(HCAudioDevices::GetMicrophones());
	if (!bOutputsKnown)
	{
		HCAudioDevices::RequestOutputs([WeakThis = TWeakObjectPtr<UHCSettingsWidget>(this)](const TArray<HCAudioDevices::FDevice>& Devices)
		{
			if (UHCSettingsWidget* Self = WeakThis.Get())
			{
				Self->Outputs.Reset();
				Self->Outputs.Add({ FString(), DefaultDeviceText().ToString() });
				Self->Outputs.Append(Devices);
				Self->bOutputsKnown = true;
				UE_LOG(LogHC, Log, TEXT("Settings: %d microphones, %d outputs (each without the Windows default)"), Self->Microphones.Num() - 1, Devices.Num());
				Self->Refresh();
			}
		});
	}
}

void UHCSettingsWidget::SelectPage(int32 Page)
{
	StopListening();
	CurrentPage = FMath::Clamp(Page, 0, PageCount - 1);
	const bool bVoice = HasVoiceChat();
	for (int32 Row = 0; Row < Lines.Num(); ++Row)
	{
		// Without voice chat the voice page only offers the devices
		const bool bVoiceOnly = Row >= VoiceOn && Row < RowCount;
		Lines[Row]->SetVisibility(PageOf(Row) == CurrentPage && (bVoice || !bVoiceOnly) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	PlayerBox->SetVisibility(CurrentPage == Voice && bVoice ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	for (int32 Index = 0; Index < PageButtons.Num(); ++Index)
	{
		PageButtons[Index]->SetBackgroundColor(Index == CurrentPage ? HCUI::AccentRed : HCUI::ButtonColor);
	}
	if (CurrentPage == Voice)
	{
		LoadDevices();
		ShownPlayers.Reset(); // built again, also after a change of the language
		PlayerBox->ClearChildren();
		UpdatePlayerLines();
	}
	else
	{
		MicMeter.Stop();
	}
	PageHint->SetText(CurrentPage == Voice
		? (bVoice ? NSLOCTEXT("HC", "VoiceHint", "Wann ihr euch hört, wählt der Host in der Lobby: nur in Meetings, Nähe + Meetings oder immer. Bei Nähe werden Stimmen mit der Entfernung leiser. Tote hören alle, werden aber nur von Toten gehört. \"Taste halten\": nur während du die Sprech-Taste hältst. \"Immer offen\": sobald das Mikrofon etwas über der Schwelle hört. Die Ausgabe gilt für den ganzen Ton des Spiels.")
			: NSLOCTEXT("HC", "VoiceOffHint", "Der Sprach-Chat selbst ist in dieser Version noch abgeschaltet. Mikrofon und Ausgabe kannst du schon wählen; die Ausgabe gilt für den ganzen Ton des Spiels."))
		: CurrentPage == Picture ? NSLOCTEXT("HC", "PictureHintPerf", "Zu langsam? Am meisten bringt eine niedrigere Auflösung: Das 3D-Bild wird etwas weicher, Schrift und Menüs bleiben scharf. Bei niedrigerer Grafikqualität bleiben Schatten, Lichtkegel und Effekte, sie werden nur einfacher berechnet; die unteren Zeilen stellen einzelne Teile davon ein. Die FPS-Anzeige zeigt oben rechts, was eine Änderung bringt.")
		: CurrentPage == Sound ? NSLOCTEXT("HC", "SoundHint", "Die Gesamtlautstärke gilt für alles, die Regler darunter zusätzlich für ihre Art von Ton; beim Verstellen hörst du eine kurze Probe. \"Ton im Hintergrund\": wie laut das Spiel ist, während ein anderes Fenster vorne ist. Headset oder Lautsprecher wählst du unter SPRACH-CHAT.")
		: CurrentPage == Controls ? NSLOCTEXT("HC", "ControlsHint", "Klicke auf eine Taste und drücke dann die neue Taste oder Maustaste (Esc bricht ab). Hat eine andere Aktion die Taste schon, tauschen die beiden.")
		: FText::GetEmpty());
	Refresh();
}

void UHCSettingsWidget::StartListening(EHCBinding Binding)
{
	StopListening();
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	Listening = Binding;
	KeyListener = MakeShared<FHCKeyListener>();
	KeyListener->Owner = this;
	FSlateApplication::Get().RegisterInputPreProcessor(KeyListener, 0);
	Refresh();
}

bool UHCSettingsWidget::FinishListening(const FKey& Key)
{
	if (Listening == EHCBinding::Count)
	{
		return false;
	}
	if (Key == EKeys::Escape || Key == EKeys::F1)
	{
		StopListening(); // cancelled
	}
	else if (HCKeys::CanBind(Key))
	{
		HCKeys::Set(Listening, Key);
		StopListening();
	}
	// Anything else (a gamepad, say) is swallowed and the menu keeps waiting
	Refresh();
	return true;
}

void UHCSettingsWidget::PlaySample(int32 Row)
{
	// The total volume is heard on an everyday sound (a door)
	const HCSoundVolumes::EGroup Group = Row == Volume ? HCSoundVolumes::EGroup::Effects : GroupOfRow(Row);
	if (Group == HCSoundVolumes::EGroup::Count)
	{
		return;
	}
	if (UAudioComponent* Old = Sample.Get())
	{
		Old->Stop(); // clicking fast through the steps must not pile the samples up
	}
	Sample = HCSoundVolumes::PlaySample(this, Group);
}

void UHCSettingsWidget::StopListening()
{
	Listening = EHCBinding::Count;
	if (KeyListener.IsValid())
	{
		// Not from inside the listener's own call: Slate is going through its list of listeners right now
		TSharedPtr<FHCKeyListener> Old = MoveTemp(KeyListener);
		Old->Owner.Reset();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick([Old]()
			{
				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().UnregisterInputPreProcessor(Old);
				}
			});
		}
		else if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(Old);
		}
	}
}

void UHCSettingsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (CurrentPage == Picture)
	{
		RefreshResolution(); // the window may be dragged to another size while the page is open
	}
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	if (CurrentPage != Voice || !GI || !MeterFill)
	{
		return;
	}
	// The other players: lines for whoever came or went, and green for whoever talks right now
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextPlayerCheck)
	{
		NextPlayerCheck = Now + 0.5;
		UpdatePlayerLines();
	}
	for (int32 Index = 0; Index < PlayerNames.Num() && Index < ShownPlayerStates.Num(); ++Index)
	{
		const bool bSpeaks = UHCVoiceSubsystem::IsVoiceArriving(ShownPlayerStates[Index].Get());
		PlayerNames[Index]->SetColorAndOpacity(FSlateColor(bSpeaks ? HCUI::AccentGreen : HCUI::TextColor));
	}
	// Listen to the chosen microphone while the voice page is open
	MicMeter.Start(GI->GetTextOption(TEXT("MicDevice")));
	const float Level = MicMeter.Update() * GI->GetOption(TEXT("MicGain"), 1.f);
	// Up at once, down slowly, so that the bar is readable
	ShownLevel = Level > ShownLevel ? Level : FMath::FInterpTo(ShownLevel, Level, InDeltaTime, 6.f);
	MeterFill->SetWidthOverride((ControlWidth - 4.f) * BarFraction(ShownLevel));
	// Green: the others would hear this (open microphone only above the threshold)
	const bool bOpen = GI->GetOption(TEXT("VoiceMode"), 0.f) > 0.5f;
	const bool bHeard = ShownLevel > (bOpen ? MicThresholdOf(GI->GetOption(TEXT("MicThreshold"), 4.f)) : 0.01f);
	MeterFillColor->SetBrushColor(bHeard ? HCUI::AccentGreen : FLinearColor(0.45f, 0.48f, 0.52f));
}

void UHCSettingsWidget::Adjust(int32 Row, int32 Delta)
{
	if (Row < 0)
	{
		SelectPage(-1 - Row);
		return;
	}
	if (Row >= PlayerRowBase)
	{
		if (ShownPlayers.IsValidIndex(Row - PlayerRowBase))
		{
			const FString& Name = ShownPlayers[Row - PlayerRowBase];
			HCVoiceVolumes::SetPlayer(Name, HCVoiceVolumes::Step(HCVoiceVolumes::OfPlayer(Name), Delta));
			RefreshPlayerValues();
		}
		return;
	}
	if (Row >= KeyRowBase)
	{
		StartListening(static_cast<EHCBinding>(FMath::Clamp(Row - KeyRowBase, 0, HCKeys::Count - 1)));
		return;
	}
	if (Row == ResetKeysRow)
	{
		StopListening();
		HCKeys::ResetAll();
		Refresh();
		return;
	}
	UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	UGameUserSettings* Video = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GI)
	{
		return;
	}
	// A named option, moved by one step inside its limits
	auto Step = [GI, Delta](const TCHAR* Key, float Default, float Size, float Min, float Max)
	{
		GI->SetOption(Key, FMath::Clamp(FMath::RoundToFloat((GI->GetOption(Key, Default) + Size * Delta) / Size) * Size, Min, Max));
	};
	// One device further in a list (index 0 = Windows default), wrapping around
	auto StepDevice = [GI, Delta](const TArray<HCAudioDevices::FDevice>& Devices, const TCHAR* Key, const TCHAR* NameKey)
	{
		if (Devices.Num() == 0)
		{
			return;
		}
		const int32 Index = FMath::Max(0, IndexOf(Devices, GI->GetTextOption(Key)));
		const HCAudioDevices::FDevice& Next = Devices[(Index + Delta + Devices.Num()) % Devices.Num()];
		GI->SetTextOption(Key, Next.Id);
		if (NameKey)
		{
			GI->SetTextOption(NameKey, Next.Name);
		}
	};
	switch (Row)
	{
	case Language:
	{
		// Every text switches at once; the page is built again so that nothing keeps the old words
		const FString Next = UHCGameInstance::GetLanguage() == TEXT("de") ? TEXT("en") : TEXT("de");
		UHCGameInstance::SetLanguage(Next);
		SelectPage(CurrentPage);
		return;
	}
	case Mouse:
		GI->SetMouseSensitivity(GI->GetMouseSensitivity() + 0.1f * Delta);
		break;
	case InvertY:
		GI->SetOption(TEXT("InvertMouseY"), Delta > 0 ? 1.f : 0.f);
		break;
	case Fov:
		GI->SetFieldOfView(GI->GetFieldOfView() + 5.f * Delta);
		break;
	case Shock:
		GI->CycleJumpscareIntensity();
		break;
	case Quality:
		if (Video)
		{
			// One level for every part. With parts set apart ("Gemischt") the step starts from the level the rest
			// still has. Every level keeps every effect (Config/DefaultScalability.ini) and leaves the resolution
			// alone; the anti-aliasing method follows (HCGraphics).
			const int32 Overall = HCGraphics::OverallLevel(Video->ScalabilityQuality);
			const int32 Level = Overall < 0 ? HCGraphics::BaseLevel(Video->ScalabilityQuality) : Overall;
			HCGraphics::SetOverallLevel(Video->ScalabilityQuality, FMath::Clamp(Level + Delta, 0, 3));
			Video->ApplyNonResolutionSettings();
			Video->SaveSettings();
			// The player's own choice: the automatic choice on the first start never overrides it
			GI->SetOption(HCGraphics::QualityChosenKey, 1.f);
		}
		break;
	case AntiAliasing:
	case Shadows:
	case Textures:
	case Effects:
	case PostProcess:
	case Reflections:
		if (Video)
		{
			if (int32* Part = QualityPartOfRow(Video->ScalabilityQuality, Row))
			{
				*Part = FMath::Clamp(*Part + Delta, 0, 3);
				Video->ApplyNonResolutionSettings();
				Video->SaveSettings();
				GI->SetOption(HCGraphics::QualityChosenKey, 1.f);
			}
		}
		break;
	case Resolution:
		if (Video)
		{
			// Only the 3D picture gets smaller; the window, texts and menus keep the full size
			Video->SetResolutionScaleValueEx(HCGraphics::StepResolution(SavedResolutionPercent(Video), Delta));
			Video->ApplyNonResolutionSettings();
			Video->SaveSettings();
			// Chosen by the player: kept over starts (a lower value from an old version would go back to 100 %)
			GI->SetOption(HCGraphics::ResolutionChosenKey, 1.f);
		}
		break;
	case FpsCounter:
		Step(HCGraphics::FpsCounterKey, 0.f, 1.f, 0.f, static_cast<float>(HCFps::ModeCount - 1));
		break;
	case Display:
		if (Video)
		{
			const EWindowMode::Type Modes[] = { EWindowMode::Windowed, EWindowMode::WindowedFullscreen, EWindowMode::Fullscreen };
			const int32 ModeCount = UE_ARRAY_COUNT(Modes);
			int32 Index = 0;
			for (int32 i = 0; i < ModeCount; ++i)
			{
				if (Modes[i] == Video->GetFullscreenMode())
				{
					Index = i;
				}
			}
			const EWindowMode::Type Mode = Modes[FMath::Clamp(Index + Delta, 0, ModeCount - 1)];
			if (Mode != Video->GetFullscreenMode())
			{
				Video->SetFullscreenMode(Mode);
				// Fullscreen and borderless always at the size of the screen (never a lower resolution); a window opens
				// a little smaller than the screen and can be dragged to any size or maximised
				const FIntPoint Desktop = Video->GetDesktopResolution();
				if (Desktop.X > 0 && Desktop.Y > 0)
				{
					Video->SetScreenResolution(Mode == EWindowMode::Windowed ? HCGraphics::WindowedSize(Desktop) : Desktop);
				}
				Video->ApplyResolutionSettings(false);
				Video->SaveSettings();
			}
		}
		break;
	case FrameLimit:
		if (Video)
		{
			// 15 .. 240 pictures per second, or no limit (0)
			Video->SetFrameRateLimit(HCGraphics::StepFrameLimit(Video->GetFrameRateLimit(), Delta));
			Video->ApplyNonResolutionSettings();
			Video->SaveSettings();
		}
		break;
	case VSync:
		if (Video)
		{
			Video->SetVSyncEnabled(Delta > 0);
			Video->ApplyNonResolutionSettings();
			Video->SaveSettings();
		}
		break;
	case Gamma:
		Step(TEXT("Gamma"), 2.2f, 0.1f, 1.6f, 3.f);
		break;
	case Volume:
		GI->SetMasterVolume(GI->GetMasterVolume() + 0.1f * Delta);
		PlaySample(Row);
		break;
	case EffectsVolume:
	case ScaresVolume:
	case AmbienceVolume:
	case SignalsVolume:
		Step(HCSoundVolumes::OptionKey(GroupOfRow(Row)), 1.f, 0.1f, 0.f, 1.f);
		PlaySample(Row);
		break;
	case BackgroundVolume:
		Step(HCSoundVolumes::BackgroundKey, 0.f, 0.1f, 0.f, 1.f);
		GI->ApplyMasterVolume();
		break;
	case MicDevice:
		StepDevice(Microphones, TEXT("MicDevice"), nullptr);
		break;
	case OutputDevice:
		StepDevice(Outputs, TEXT("OutputDevice"), TEXT("OutputDeviceName"));
		GI->ApplyOutputDevice();
		break;
	case VoiceOn:
		GI->SetOption(TEXT("VoiceEnabled"), Delta > 0 ? 1.f : 0.f);
		break;
	case VoiceMode:
		GI->SetOption(TEXT("VoiceMode"), Delta > 0 ? 1.f : 0.f);
		break;
	case MicGain:
		Step(TEXT("MicGain"), 1.f, 0.25f, 0.5f, 3.f);
		break;
	case MicThreshold:
		Step(TEXT("MicThreshold"), 4.f, 1.f, 1.f, 10.f);
		break;
	case VoiceVolume:
		// Up to 400 % (the gain stage of the voice chat, Voice/HCVoiceBoost.h); 10 % steps below 100 %, 25 % above
		GI->SetOption(TEXT("VoiceVolume"), HCVoiceVolumes::Step(HCVoiceVolumes::OfAll(this), Delta));
		break;
	default:
		break;
	}
	Refresh();
}

void UHCSettingsWidget::Refresh()
{
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	const UGameUserSettings* Video = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GI || Values.Num() < RowCount)
	{
		return;
	}
	const FText On = NSLOCTEXT("HC", "On", "An"), Off = NSLOCTEXT("HC", "Off", "Aus");
	auto Percent = [](float Factor) { return FText::Format(NSLOCTEXT("HC", "PercentFmt", "{0} %"), FMath::RoundToInt(Factor * 100.f)); };
	const FText Waiting = NSLOCTEXT("HC", "KeyWaiting", "Taste drücken ...");

	// Each language in its own words, so that everybody finds the way back
	Values[Language]->SetText(UHCGameInstance::GetLanguage() == TEXT("de") ? FText::FromString(TEXT("Deutsch")) : FText::FromString(TEXT("English")));
	Values[Mouse]->SetText(FText::FromString(FString::Printf(TEXT("%.1f x"), GI->GetMouseSensitivity())));
	Values[InvertY]->SetText(GI->GetOption(TEXT("InvertMouseY"), 0.f) > 0.5f ? On : Off);
	Values[Fov]->SetText(FText::Format(NSLOCTEXT("HC", "DegreesFmt", "{0}°"), FMath::RoundToInt(GI->GetFieldOfView())));
	const EHCJumpscareIntensity Shocks = GI->GetJumpscareIntensity();
	Values[Shock]->SetText(Shocks == EHCJumpscareIntensity::Full ? NSLOCTEXT("HC", "ShockFull", "Voll")
		: Shocks == EHCJumpscareIntensity::Reduced ? NSLOCTEXT("HC", "ShockReduced", "Reduziert") : NSLOCTEXT("HC", "ShockMinimal", "Minimal"));

	const FText Levels[] = { NSLOCTEXT("HC", "QLow", "Niedrig"), NSLOCTEXT("HC", "QMedium", "Mittel"), NSLOCTEXT("HC", "QHigh", "Hoch"), NSLOCTEXT("HC", "QEpic", "Sehr hoch") };
	Scalability::FQualityLevels Parts = Video ? Video->ScalabilityQuality : Scalability::GetQualityLevels();
	const int32 Level = HCGraphics::OverallLevel(Parts);
	Values[Quality]->SetText(Level >= 0 && Level <= 3 ? Levels[Level] : NSLOCTEXT("HC", "QMixed", "Gemischt"));
	for (const int32 Row : { AntiAliasing, Shadows, Textures, Effects, PostProcess, Reflections })
	{
		Values[Row]->SetText(Levels[FMath::Clamp(*QualityPartOfRow(Parts, Row), 0, 3)]);
	}
	ShownResolution.Reset();
	RefreshResolution();
	const int32 Fps = FMath::RoundToInt32(GI->GetOption(HCGraphics::FpsCounterKey, 0.f));
	Values[FpsCounter]->SetText(Fps >= HCFps::Detailed ? NSLOCTEXT("HC", "FpsDetailed", "Mit Details") : Fps == HCFps::Simple ? On : Off);
	const EWindowMode::Type Mode = Video ? Video->GetFullscreenMode() : EWindowMode::Windowed;
	Values[Display]->SetText(Mode == EWindowMode::Fullscreen ? NSLOCTEXT("HC", "DFull", "Vollbild")
		: Mode == EWindowMode::WindowedFullscreen ? NSLOCTEXT("HC", "DBorderless", "Randlos") : NSLOCTEXT("HC", "DWindow", "Fenster"));
	const float Limit = Video ? Video->GetFrameRateLimit() : 0.f;
	Values[FrameLimit]->SetText(Limit > 0.5f ? FText::AsNumber(FMath::RoundToInt(Limit)) : NSLOCTEXT("HC", "NoLimit", "Keine"));
	Values[VSync]->SetText(Video && Video->IsVSyncEnabled() ? On : Off);
	// 2.2 is the normal value; shown as a step so that nobody has to know what gamma is
	Values[Gamma]->SetText(FText::AsNumber(FMath::RoundToInt((GI->GetOption(TEXT("Gamma"), 2.2f) - 2.2f) * 10.f)));

	Values[Volume]->SetText(Percent(GI->GetMasterVolume()));
	for (const int32 Row : { EffectsVolume, ScaresVolume, AmbienceVolume, SignalsVolume })
	{
		Values[Row]->SetText(Percent(FMath::Clamp(GI->GetOption(HCSoundVolumes::OptionKey(GroupOfRow(Row)), 1.f), 0.f, 1.f)));
	}
	const float Background = GI->GetOption(HCSoundVolumes::BackgroundKey, 0.f);
	Values[BackgroundVolume]->SetText(Background <= 0.001f ? NSLOCTEXT("HC", "VolumeMuted", "Stumm") : Percent(Background));

	// Devices: the saved one by name; one that is not plugged in any more still shows its name
	const FString Mic = GI->GetTextOption(TEXT("MicDevice"));
	Values[MicDevice]->SetText(Mic.IsEmpty() ? DefaultDeviceText()
		: IndexOf(Microphones, Mic) != INDEX_NONE || Microphones.Num() == 0 ? DeviceText(Mic)
		: FText::Format(NSLOCTEXT("HC", "DeviceMissingFmt", "{0} (nicht da)"), DeviceText(Mic)));
	const FString Output = GI->GetTextOption(TEXT("OutputDevice"));
	const int32 OutputIndex = IndexOf(Outputs, Output);
	Values[OutputDevice]->SetText(Output.IsEmpty() ? DefaultDeviceText()
		: OutputIndex != INDEX_NONE ? DeviceText(Outputs[OutputIndex].Name)
		: !bOutputsKnown ? DeviceText(GI->GetTextOption(TEXT("OutputDeviceName")))
		: FText::Format(NSLOCTEXT("HC", "DeviceMissingFmt", "{0} (nicht da)"), DeviceText(GI->GetTextOption(TEXT("OutputDeviceName")))));

	Values[VoiceOn]->SetText(GI->GetOption(TEXT("VoiceEnabled"), 1.f) > 0.5f ? On : Off);
	Values[VoiceMode]->SetText(GI->GetOption(TEXT("VoiceMode"), 0.f) > 0.5f ? NSLOCTEXT("HC", "MicOpen", "Immer offen") : NSLOCTEXT("HC", "MicPush", "Taste halten"));
	Values[TalkKey]->SetText(Listening == EHCBinding::Talk ? Waiting : HCKeys::LongName(HCKeys::Get(EHCBinding::Talk)));
	Values[MicGain]->SetText(Percent(GI->GetOption(TEXT("MicGain"), 1.f)));
	Values[MicThreshold]->SetText(FText::AsNumber(FMath::RoundToInt(GI->GetOption(TEXT("MicThreshold"), 4.f))));
	const float AllVoices = HCVoiceVolumes::OfAll(this);
	Values[VoiceVolume]->SetText(AllVoices <= 0.001f ? NSLOCTEXT("HC", "VolumeMuted", "Stumm") : Percent(AllVoices));

	for (int32 Index = 0; Index < KeyLabels.Num(); ++Index)
	{
		const EHCBinding Binding = static_cast<EHCBinding>(Index);
		KeyLabels[Index]->SetText(Listening == Binding ? Waiting : HCKeys::LongName(HCKeys::Get(Binding)));
	}
}

void UHCSettingsWidget::RefreshResolution()
{
	if (!Values.IsValidIndex(Resolution) || !Values[Resolution])
	{
		return;
	}
	const float Percent = SavedResolutionPercent(GEngine ? GEngine->GetGameUserSettings() : nullptr);
	const FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
	const FIntPoint Size = HCGraphics::RenderSize(Viewport ? Viewport->GetSizeXY() : FIntPoint(0, 0), Percent);
	// "1536 x 864 (80 %)": the size the 3D picture really has, so that the number means something
	const FString Text = Size.X > 0 && Size.Y > 0 ? FString::Printf(TEXT("%d x %d (%d %%)"), Size.X, Size.Y, FMath::RoundToInt32(Percent))
		: FString::Printf(TEXT("%d %%"), FMath::RoundToInt32(Percent));
	if (Text != ShownResolution)
	{
		ShownResolution = Text;
		Values[Resolution]->SetText(FText::AsCultureInvariant(Text));
	}
}

FReply UHCSettingsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		HandleClose();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UHCSettingsWidget::HandleClose()
{
	StopListening();
	MicMeter.Stop();
	RemoveFromParent();
}
