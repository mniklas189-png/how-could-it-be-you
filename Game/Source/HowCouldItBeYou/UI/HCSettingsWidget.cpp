#include "UI/HCSettingsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameInstance.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "UI/HCUIStyle.h"

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
	HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "SettingsHint", "Jede Änderung gilt sofort und wird gespeichert."), 13, HCUI::MutedColor), 22.f);

	const FText Labels[RowCount] = {
		NSLOCTEXT("HC", "SetVolume", "Lautstärke"),
		NSLOCTEXT("HC", "SetMouse", "Mausempfindlichkeit"),
		NSLOCTEXT("HC", "SetFov", "Sichtfeld"),
		NSLOCTEXT("HC", "SetQuality", "Grafikqualität"),
		NSLOCTEXT("HC", "SetDisplay", "Anzeige"),
		NSLOCTEXT("HC", "SetVSync", "VSync"),
		NSLOCTEXT("HC", "SetShock", "Schock-Effekte"),
	};
	for (int32 Row = 0; Row < RowCount; ++Row)
	{
		UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Line, HCUI::Text(Tree, Labels[Row], 16, HCUI::TextColor), 16.f, true);
		for (const int32 Delta : { -1, 1 })
		{
			UHCSettingsClickProxy* Proxy = NewObject<UHCSettingsClickProxy>(this);
			Proxy->Owner = this;
			Proxy->Row = Row;
			Proxy->Delta = Delta;
			Proxies.Add(Proxy);
			UButton* Step = HCUI::Button(Tree, Delta < 0 ? FText::FromString(TEXT("<")) : FText::FromString(TEXT(">")), HCUI::ButtonColor, nullptr, 16);
			Step->OnClicked.AddDynamic(Proxy, &UHCSettingsClickProxy::HandleClick);
			USizeBox* StepBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			StepBox->SetWidthOverride(44.f);
			StepBox->SetContent(Step);
			if (Delta > 0)
			{
				// The value sits between the two buttons
				UTextBlock* Value = HCUI::Text(Tree, FText::GetEmpty(), 16, FLinearColor(0.95f, 0.92f, 0.62f), true);
				Value->SetJustification(ETextJustify::Center);
				USizeBox* ValueBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				ValueBox->SetWidthOverride(190.f);
				ValueBox->SetContent(Value);
				HCUI::AddColumn(Line, ValueBox, 8.f);
				Values.Add(Value);
			}
			HCUI::AddColumn(Line, StepBox, Delta < 0 ? 8.f : 0.f);
		}
		HCUI::AddRow(Box, Line, 10.f);
	}

	UButton* Close = HCUI::Button(Tree, NSLOCTEXT("HC", "SettingsBack", "ZURÜCK"), HCUI::AccentRed);
	Close->OnClicked.AddDynamic(this, &UHCSettingsWidget::HandleClose);
	UVerticalBox* Spacer = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Box, Spacer, 12.f);
	HCUI::AddRow(Box, Close, 0.f);

	HCUI::Place(Root, Panel, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 620.f);
	Refresh();
}

void UHCSettingsWidget::Adjust(int32 Row, int32 Delta)
{
	UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	UGameUserSettings* Video = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!GI)
	{
		return;
	}
	switch (Row)
	{
	case Volume:
		GI->SetMasterVolume(GI->GetMasterVolume() + 0.1f * Delta);
		break;
	case Mouse:
		GI->SetMouseSensitivity(GI->GetMouseSensitivity() + 0.1f * Delta);
		break;
	case Fov:
		GI->SetFieldOfView(GI->GetFieldOfView() + 5.f * Delta);
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
				if (Mode != EWindowMode::Windowed)
				{
					Video->SetScreenResolution(Video->GetDesktopResolution());
				}
				Video->ApplyResolutionSettings(false);
				Video->SaveSettings();
			}
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
	case Shock:
		GI->CycleJumpscareIntensity();
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
	Values[Volume]->SetText(FText::Format(NSLOCTEXT("HC", "PercentFmt", "{0} %"), FMath::RoundToInt(GI->GetMasterVolume() * 100.f)));
	Values[Mouse]->SetText(FText::FromString(FString::Printf(TEXT("%.1f x"), GI->GetMouseSensitivity())));
	Values[Fov]->SetText(FText::Format(NSLOCTEXT("HC", "DegreesFmt", "{0}°"), FMath::RoundToInt(GI->GetFieldOfView())));
	const FText Levels[] = { NSLOCTEXT("HC", "QLow", "Niedrig"), NSLOCTEXT("HC", "QMedium", "Mittel"), NSLOCTEXT("HC", "QHigh", "Hoch"), NSLOCTEXT("HC", "QEpic", "Sehr hoch") };
	const int32 Level = Video ? Video->GetOverallScalabilityLevel() : 3;
	Values[Quality]->SetText(Level >= 0 && Level <= 3 ? Levels[Level] : NSLOCTEXT("HC", "QMixed", "Gemischt"));
	const EWindowMode::Type Mode = Video ? Video->GetFullscreenMode() : EWindowMode::Windowed;
	Values[Display]->SetText(Mode == EWindowMode::Fullscreen ? NSLOCTEXT("HC", "DFull", "Vollbild")
		: Mode == EWindowMode::WindowedFullscreen ? NSLOCTEXT("HC", "DBorderless", "Randlos") : NSLOCTEXT("HC", "DWindow", "Fenster"));
	Values[VSync]->SetText(Video && Video->IsVSyncEnabled() ? NSLOCTEXT("HC", "On", "An") : NSLOCTEXT("HC", "Off", "Aus"));
	const EHCJumpscareIntensity Shocks = GI->GetJumpscareIntensity();
	Values[Shock]->SetText(Shocks == EHCJumpscareIntensity::Full ? NSLOCTEXT("HC", "ShockFull", "Voll")
		: Shocks == EHCJumpscareIntensity::Reduced ? NSLOCTEXT("HC", "ShockReduced", "Reduziert") : NSLOCTEXT("HC", "ShockMinimal", "Minimal"));
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
	RemoveFromParent();
}
