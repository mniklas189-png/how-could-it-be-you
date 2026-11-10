#include "UI/HCPauseWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Characters/HCCharacter.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameInstance.h"
#include "Core/HCPlayerController.h"
#include "Online/HCSessionSubsystem.h"
#include "UI/HCHUD.h"
#include "UI/HCUIStyle.h"

void UHCPauseWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.f, 0.f, 0.6f));

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(32.f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Box);
	HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "PauseTitle", "MENÜ"), 26, HCUI::TextColor, true), 6.f);
	HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "NoPause", "Das Spiel läuft weiter."), 13, HCUI::MutedColor), 22.f);

	UButton* Continue = HCUI::Button(Tree, NSLOCTEXT("HC", "Continue", "WEITER"));
	Continue->OnClicked.AddDynamic(this, &UHCPauseWidget::HandleContinue);
	HCUI::AddRow(Box, Continue, 10.f);
	UButton* Stuck = HCUI::Button(Tree, NSLOCTEXT("HC", "Stuck", "ICH STECKE FEST"));
	Stuck->OnClicked.AddDynamic(this, &UHCPauseWidget::HandleUnstuck);
	HCUI::AddRow(Box, Stuck, 10.f);
	UTextBlock* IntensityText = nullptr;
	UButton* IntensityButton = HCUI::Button(Tree, FText::GetEmpty(), HCUI::ButtonColor, &IntensityText, 13);
	IntensityButton->OnClicked.AddDynamic(this, &UHCPauseWidget::HandleIntensity);
	IntensityLabel = IntensityText;
	HCUI::AddRow(Box, IntensityButton, 10.f);
	UTextBlock* Label = nullptr;
	UButton* Leave = HCUI::Button(Tree, NSLOCTEXT("HC", "LeaveGame", "SPIEL VERLASSEN"), HCUI::AccentRed, &Label);
	Leave->OnClicked.AddDynamic(this, &UHCPauseWidget::HandleLeave);
	LeaveLabel = Label;
	HCUI::AddRow(Box, Leave, 10.f);
	InfoText = HCUI::Text(Tree, FText::GetEmpty(), 13, HCUI::MutedColor);
	HCUI::AddRow(Box, InfoText, 0.f);

	HCUI::Place(Root, Panel, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 380.f);
}

void UHCPauseWidget::HandleIntensity()
{
	if (UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance()))
	{
		GI->CycleJumpscareIntensity();
	}
	ResetConfirm();
}

void UHCPauseWidget::ResetConfirm()
{
	if (IntensityLabel)
	{
		const UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
		IntensityLabel->SetText(UHCGameInstance::GetJumpscareIntensityLabel(GI ? GI->GetJumpscareIntensity() : EHCJumpscareIntensity::Full));
	}
	bConfirmLeave = false;
	if (LeaveLabel)
	{
		LeaveLabel->SetText(NSLOCTEXT("HC", "LeaveGame", "SPIEL VERLASSEN"));
	}
	if (InfoText)
	{
		InfoText->SetText(FText::GetEmpty());
	}
}

FReply UHCPauseWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F1)
	{
		HandleContinue();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UHCPauseWidget::HandleContinue()
{
	if (AHCHUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->TogglePauseMenu();
	}
}

void UHCPauseWidget::HandleUnstuck()
{
	// Player path against getting stuck, with a cooldown (docs/04 §6)
	const double Now = FPlatformTime::Seconds();
	if (Now - LastUnstuckTime < 60.0)
	{
		InfoText->SetText(FText::Format(NSLOCTEXT("HC", "UnstuckWait", "Bitte warte noch {0} s."), FMath::CeilToInt(60.0 - (Now - LastUnstuckTime))));
		return;
	}
	if (AHCCharacter* Character = GetOwningPlayer() ? Cast<AHCCharacter>(GetOwningPlayer()->GetPawn()) : nullptr)
	{
		LastUnstuckTime = Now;
		Character->ServerUnstuck();
		HandleContinue();
	}
}

void UHCPauseWidget::HandleLeave()
{
	const AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	if (PC && PC->IsHost() && !bConfirmLeave && GetWorld()->GetNetMode() == NM_ListenServer)
	{
		// Host: confirmation, the round ends for everyone (docs/03 §7.6)
		bConfirmLeave = true;
		LeaveLabel->SetText(NSLOCTEXT("HC", "LeaveConfirm", "WIRKLICH? RUNDE ENDET FÜR ALLE"));
		return;
	}
	if (UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
	{
		Sessions->LeaveToMainMenu();
	}
}
