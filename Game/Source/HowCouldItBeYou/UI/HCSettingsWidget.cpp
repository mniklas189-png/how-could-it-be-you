#include "UI/HCSettingsWidget.h"

#include "Blueprint/WidgetTree.h"
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
#include "Core/HCLog.h"
#include "Engine/Engine.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/VoiceInterface.h"
#include "Kismet/KismetSystemLibrary.h"
#include "OnlineSubsystemUtils.h"
#include "UI/HCUIStyle.h"

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
	return Row <= Shock ? General : Row <= Gamma ? Picture : Voice;
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
		NSLOCTEXT("HC", "PageControls", "STEUERUNG"), NSLOCTEXT("HC", "PageVoice", "SPRACH-CHAT") };
	UHorizontalBox* Pages = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	for (int32 Page = 0; Page < PageCount; ++Page)
	{
		UButton* Tab = HCUI::Button(Tree, PageNames[Page], HCUI::ButtonColor, nullptr, 14);
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
		NSLOCTEXT("HC", "SetVolume", "Lautstärke"),
		NSLOCTEXT("HC", "SetMouse", "Mausempfindlichkeit"),
		NSLOCTEXT("HC", "SetInvertY", "Maus hoch/runter umkehren"),
		NSLOCTEXT("HC", "SetFov", "Sichtfeld"),
		NSLOCTEXT("HC", "SetShock", "Schock-Effekte"),
		NSLOCTEXT("HC", "SetQuality", "Grafikqualität"),
		NSLOCTEXT("HC", "SetDisplay", "Anzeige"),
		NSLOCTEXT("HC", "SetResolution", "Auflösung"),
		NSLOCTEXT("HC", "SetFrameLimit", "Bilder pro Sekunde (Grenze)"),
		NSLOCTEXT("HC", "SetVSync", "VSync"),
		NSLOCTEXT("HC", "SetRenderScale", "Render-Auflösung"),
		NSLOCTEXT("HC", "SetGamma", "Helligkeit"),
		NSLOCTEXT("HC", "SetMicDevice", "Mikrofon"),
		NSLOCTEXT("HC", "SetMicLevel", "Mikrofon-Pegel (sprich zum Testen)"),
		NSLOCTEXT("HC", "SetOutputDevice", "Ausgabe (Headset / Lautsprecher)"),
		NSLOCTEXT("HC", "SetVoiceOn", "Sprach-Chat"),
		NSLOCTEXT("HC", "SetVoiceMode", "Senden"),
		NSLOCTEXT("HC", "SetVoiceKey", "Sprech-Taste"),
		NSLOCTEXT("HC", "SetMicGain", "Mikrofon-Verstärkung"),
		NSLOCTEXT("HC", "SetMicThreshold", "Mikrofon-Schwelle (offen)"),
		NSLOCTEXT("HC", "SetVoiceVolume", "Lautstärke der anderen"),
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
			const bool bDevice = Row == MicDevice || Row == OutputDevice;
			for (const int32 Delta : { -1, 1 })
			{
				UButton* Step = HCUI::Button(Tree, Delta < 0 ? FText::FromString(TEXT("<")) : FText::FromString(TEXT(">")), HCUI::ButtonColor, nullptr, 16);
				Step->OnClicked.AddDynamic(MakeProxy(Row, Delta), &UHCSettingsClickProxy::HandleClick);
				USizeBox* StepBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				StepBox->SetWidthOverride(StepWidth);
				StepBox->SetContent(Step);
				if (Delta > 0)
				{
					// The value sits between the two buttons; device names are long and get a smaller font and two lines
					UTextBlock* Value = HCUI::Text(Tree, FText::GetEmpty(), bDevice ? 13 : 16, ValueColor, true);
					Value->SetJustification(ETextJustify::Center);
					Value->SetAutoWrapText(bDevice);
					USizeBox* ValueBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
					ValueBox->SetWidthOverride(ValueWidth);
					if (USizeBoxSlot* ValueSlot = Cast<USizeBoxSlot>(ValueBox->SetContent(Value)))
					{
						ValueSlot->SetVerticalAlignment(VAlign_Center);
					}
					HCUI::AddColumn(Line, ValueBox, 8.f);
					Values.Add(Value);
				}
				HCUI::AddColumn(Line, StepBox, Delta < 0 ? 8.f : 0.f);
			}
		}
		HCUI::AddRow(Rows, Line, 10.f);
		Lines.Add(Line);
	}

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
	Microphones.Add({ FString(), NSLOCTEXT("HC", "DeviceDefault", "Windows-Standard").ToString() });
	Microphones.Append(HCAudioDevices::GetMicrophones());
	if (!bOutputsKnown)
	{
		HCAudioDevices::RequestOutputs([WeakThis = TWeakObjectPtr<UHCSettingsWidget>(this)](const TArray<HCAudioDevices::FDevice>& Devices)
		{
			if (UHCSettingsWidget* Self = WeakThis.Get())
			{
				Self->Outputs.Reset();
				Self->Outputs.Add({ FString(), NSLOCTEXT("HC", "DeviceDefault", "Windows-Standard").ToString() });
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
	for (int32 Index = 0; Index < PageButtons.Num(); ++Index)
	{
		PageButtons[Index]->SetBackgroundColor(Index == CurrentPage ? HCUI::AccentRed : HCUI::ButtonColor);
	}
	if (CurrentPage == Voice)
	{
		LoadDevices();
	}
	else
	{
		MicMeter.Stop();
	}
	PageHint->SetText(CurrentPage == Voice
		? (bVoice ? NSLOCTEXT("HC", "VoiceHint", "Stimmen kommen von den Figuren und werden mit der Entfernung leiser. Im Meeting hören sich alle. Tote hören alle, werden aber nur von Toten gehört. \"Taste halten\": nur während du die Sprech-Taste hältst. \"Immer offen\": sobald das Mikrofon etwas über der Schwelle hört. Die Ausgabe gilt für den ganzen Ton des Spiels.")
			: NSLOCTEXT("HC", "VoiceOffHint", "Der Sprach-Chat selbst ist in dieser Version noch abgeschaltet. Mikrofon und Ausgabe kannst du schon wählen; die Ausgabe gilt für den ganzen Ton des Spiels."))
		: CurrentPage == Picture ? NSLOCTEXT("HC", "PictureHint", "Die Render-Auflösung unter 100 % macht das Spiel schneller und etwas unschärfer.")
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
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	if (CurrentPage != Voice || !GI || !MeterFill)
	{
		return;
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
	case Volume:
		GI->SetMasterVolume(GI->GetMasterVolume() + 0.1f * Delta);
		break;
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
			// -1 means "mixed" (never chosen here): treat it as the best level
			const int32 Level = Video->GetOverallScalabilityLevel() < 0 ? 3 : Video->GetOverallScalabilityLevel();
			Video->SetOverallScalabilityLevel(FMath::Clamp(Level + Delta, 0, 3));
			Video->ApplyNonResolutionSettings();
			Video->SaveSettings();
		}
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
				if (Mode == EWindowMode::WindowedFullscreen)
				{
					Video->SetScreenResolution(Video->GetDesktopResolution());
				}
				Video->ApplyResolutionSettings(false);
				Video->SaveSettings();
			}
		}
		break;
	case Resolution:
		if (Video)
		{
			// The sizes the screen offers, smallest first; borderless always uses the desktop size
			TArray<FIntPoint> Sizes;
			UKismetSystemLibrary::GetSupportedFullscreenResolutions(Sizes);
			if (Sizes.Num() > 0 && Video->GetFullscreenMode() != EWindowMode::WindowedFullscreen)
			{
				int32 Index = Sizes.Num() - 1;
				for (int32 i = 0; i < Sizes.Num(); ++i)
				{
					if (Sizes[i].X <= Video->GetScreenResolution().X && Sizes[i].Y <= Video->GetScreenResolution().Y)
					{
						Index = i;
					}
				}
				Video->SetScreenResolution(Sizes[FMath::Clamp(Index + Delta, 0, Sizes.Num() - 1)]);
				Video->ApplyResolutionSettings(false);
				Video->SaveSettings();
			}
		}
		break;
	case FrameLimit:
		if (Video)
		{
			const float Limits[] = { 30.f, 60.f, 90.f, 120.f, 144.f, 165.f, 240.f, 0.f }; // 0 = no limit
			const int32 LimitCount = UE_ARRAY_COUNT(Limits);
			int32 Index = LimitCount - 1;
			for (int32 i = 0; i < LimitCount; ++i)
			{
				if (FMath::IsNearlyEqual(Limits[i], Video->GetFrameRateLimit(), 0.5f))
				{
					Index = i;
				}
			}
			Video->SetFrameRateLimit(Limits[FMath::Clamp(Index + Delta, 0, LimitCount - 1)]);
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
	case RenderScale:
		if (Video)
		{
			float Normalized = 0.f, Current = 100.f, Min = 0.f, Max = 100.f;
			Video->GetResolutionScaleInformationEx(Normalized, Current, Min, Max);
			Current = Current <= 0.f ? 100.f : Current; // 0 = never set: the engine renders at full size
			Video->SetResolutionScaleValueEx(FMath::Clamp(FMath::RoundToFloat((Current + 10.f * Delta) / 10.f) * 10.f, FMath::Max(50.f, Min), Max));
			Video->ApplyNonResolutionSettings();
			Video->SaveSettings();
		}
		break;
	case Gamma:
		Step(TEXT("Gamma"), 2.2f, 0.1f, 1.6f, 3.f);
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
		Step(TEXT("VoiceVolume"), 1.f, 0.1f, 0.f, 2.f);
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
	Values[Volume]->SetText(Percent(GI->GetMasterVolume()));
	Values[Mouse]->SetText(FText::FromString(FString::Printf(TEXT("%.1f x"), GI->GetMouseSensitivity())));
	Values[InvertY]->SetText(GI->GetOption(TEXT("InvertMouseY"), 0.f) > 0.5f ? On : Off);
	Values[Fov]->SetText(FText::Format(NSLOCTEXT("HC", "DegreesFmt", "{0}°"), FMath::RoundToInt(GI->GetFieldOfView())));
	const EHCJumpscareIntensity Shocks = GI->GetJumpscareIntensity();
	Values[Shock]->SetText(Shocks == EHCJumpscareIntensity::Full ? NSLOCTEXT("HC", "ShockFull", "Voll")
		: Shocks == EHCJumpscareIntensity::Reduced ? NSLOCTEXT("HC", "ShockReduced", "Reduziert") : NSLOCTEXT("HC", "ShockMinimal", "Minimal"));

	const FText Levels[] = { NSLOCTEXT("HC", "QLow", "Niedrig"), NSLOCTEXT("HC", "QMedium", "Mittel"), NSLOCTEXT("HC", "QHigh", "Hoch"), NSLOCTEXT("HC", "QEpic", "Sehr hoch") };
	const int32 Level = Video ? Video->GetOverallScalabilityLevel() : 3;
	Values[Quality]->SetText(Level >= 0 && Level <= 3 ? Levels[Level] : NSLOCTEXT("HC", "QMixed", "Gemischt"));
	const EWindowMode::Type Mode = Video ? Video->GetFullscreenMode() : EWindowMode::Windowed;
	Values[Display]->SetText(Mode == EWindowMode::Fullscreen ? NSLOCTEXT("HC", "DFull", "Vollbild")
		: Mode == EWindowMode::WindowedFullscreen ? NSLOCTEXT("HC", "DBorderless", "Randlos") : NSLOCTEXT("HC", "DWindow", "Fenster"));
	const FIntPoint Size = Video ? Video->GetScreenResolution() : FIntPoint(0, 0);
	Values[Resolution]->SetText(Mode == EWindowMode::WindowedFullscreen ? NSLOCTEXT("HC", "ResDesktop", "wie Bildschirm") : FText::FromString(FString::Printf(TEXT("%d x %d"), Size.X, Size.Y)));
	const float Limit = Video ? Video->GetFrameRateLimit() : 0.f;
	Values[FrameLimit]->SetText(Limit > 0.5f ? FText::AsNumber(FMath::RoundToInt(Limit)) : NSLOCTEXT("HC", "NoLimit", "Keine"));
	Values[VSync]->SetText(Video && Video->IsVSyncEnabled() ? On : Off);
	float Normalized = 0.f, Scale = 100.f, Min = 0.f, Max = 100.f;
	if (Video)
	{
		Video->GetResolutionScaleInformationEx(Normalized, Scale, Min, Max);
	}
	Values[RenderScale]->SetText(Percent(Scale <= 0.f ? 1.f : Scale / 100.f)); // 0 = never set: full size
	// 2.2 is the normal value; shown as a step so that nobody has to know what gamma is
	Values[Gamma]->SetText(FText::AsNumber(FMath::RoundToInt((GI->GetOption(TEXT("Gamma"), 2.2f) - 2.2f) * 10.f)));

	// Devices: the saved one by name; one that is not plugged in any more still shows its name
	const FString Mic = GI->GetTextOption(TEXT("MicDevice"));
	Values[MicDevice]->SetText(Mic.IsEmpty() ? NSLOCTEXT("HC", "DeviceDefault", "Windows-Standard")
		: IndexOf(Microphones, Mic) != INDEX_NONE || Microphones.Num() == 0 ? DeviceText(Mic)
		: FText::Format(NSLOCTEXT("HC", "DeviceMissingFmt", "{0} (nicht da)"), DeviceText(Mic)));
	const FString Output = GI->GetTextOption(TEXT("OutputDevice"));
	const int32 OutputIndex = IndexOf(Outputs, Output);
	Values[OutputDevice]->SetText(Output.IsEmpty() ? NSLOCTEXT("HC", "DeviceDefault", "Windows-Standard")
		: OutputIndex != INDEX_NONE ? DeviceText(Outputs[OutputIndex].Name)
		: !bOutputsKnown ? DeviceText(GI->GetTextOption(TEXT("OutputDeviceName")))
		: FText::Format(NSLOCTEXT("HC", "DeviceMissingFmt", "{0} (nicht da)"), DeviceText(GI->GetTextOption(TEXT("OutputDeviceName")))));

	Values[VoiceOn]->SetText(GI->GetOption(TEXT("VoiceEnabled"), 1.f) > 0.5f ? On : Off);
	Values[VoiceMode]->SetText(GI->GetOption(TEXT("VoiceMode"), 0.f) > 0.5f ? NSLOCTEXT("HC", "MicOpen", "Immer offen") : NSLOCTEXT("HC", "MicPush", "Taste halten"));
	Values[TalkKey]->SetText(Listening == EHCBinding::Talk ? Waiting : HCKeys::LongName(HCKeys::Get(EHCBinding::Talk)));
	Values[MicGain]->SetText(Percent(GI->GetOption(TEXT("MicGain"), 1.f)));
	Values[MicThreshold]->SetText(FText::AsNumber(FMath::RoundToInt(GI->GetOption(TEXT("MicThreshold"), 4.f))));
	Values[VoiceVolume]->SetText(Percent(GI->GetOption(TEXT("VoiceVolume"), 1.f)));

	for (int32 Index = 0; Index < KeyLabels.Num(); ++Index)
	{
		const EHCBinding Binding = static_cast<EHCBinding>(Index);
		KeyLabels[Index]->SetText(Listening == Binding ? Waiting : HCKeys::LongName(HCKeys::Get(Binding)));
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
