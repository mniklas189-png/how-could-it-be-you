#include "UI/HCMainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameInstance.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Online/HCSessionSubsystem.h"
#include "UI/HCUIStyle.h"

void UHCMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.f, 0.f, 1.f));

	// --- Epilepsy warning (first start, docs/01 §10) ---
	{
		UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(40.f));
		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Panel->SetContent(Box);
		HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "WarnTitle", "WARNUNG"), 28, HCUI::AccentRed, true), 16.f);
		HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "WarnBody",
			"Dieses Spiel enthält Schreckmomente, plötzliche laute Geräusche, flackerndes Licht und Dunkelheit. "
			"Bei manchen Menschen können flackernde Lichter epileptische Anfälle auslösen. "
			"Wenn du oder jemand in deiner Familie an Epilepsie leidet, sprich vor dem Spielen mit einer Ärztin oder einem Arzt.\n\n"
			"Empfohlen ab 16 Jahren."), 16), 28.f);
		HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "WarnIntensity", "Du kannst die Schreckmomente abschwächen (jederzeit im Menü änderbar):"), 14, HCUI::MutedColor), 8.f);
		UTextBlock* WarningLabel = nullptr;
		UButton* IntensityWarning = HCUI::Button(Tree, FText::GetEmpty(), HCUI::ButtonColor, &WarningLabel, 14);
		IntensityWarning->OnClicked.AddDynamic(this, &UHCMainMenuWidget::HandleIntensity);
		IntensityLabelWarning = WarningLabel;
		HCUI::AddRow(Box, IntensityWarning, 24.f);
		UButton* Accept = HCUI::Button(Tree, NSLOCTEXT("HC", "WarnAccept", "VERSTANDEN"), HCUI::AccentRed);
		Accept->OnClicked.AddDynamic(this, &UHCMainMenuWidget::HandleAcceptWarning);
		HCUI::AddRow(Box, Accept, 0.f, HAlign_Right);
		WarningPanel = Panel;
		HCUI::Place(Root, Panel, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 640.f);
	}

	// --- Main menu ---
	{
		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "Title", "HOW COULD IT BE YOU"), 40, HCUI::TextColor, true), 4.f);
		HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "Subtitle", "Es ist einer von euch."), 16, FLinearColor(0.8f, 0.15f, 0.15f)), 40.f);

		HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "NameLabel", "Dein Name"), 14, HCUI::MutedColor), 4.f);
		NameBox = HCUI::TextBox(Tree, NSLOCTEXT("HC", "NameHint", "z. B. Lena"));
		HCUI::AddRow(Box, NameBox, 24.f);

		HostButton = HCUI::Button(Tree, NSLOCTEXT("HC", "Host", "LOBBY HOSTEN"), HCUI::AccentRed);
		HostButton->OnClicked.AddDynamic(this, &UHCMainMenuWidget::HandleHost);
		HCUI::AddRow(Box, HostButton, 28.f);

		HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "CodeLabel", "Join-Code oder IP-Adresse des Hosts"), 14, HCUI::MutedColor), 4.f);
		CodeBox = HCUI::TextBox(Tree, NSLOCTEXT("HC", "CodeHint", "K7M 4QX  oder  192.168.0.5"));
		HCUI::AddRow(Box, CodeBox, 8.f);
		JoinButton = HCUI::Button(Tree, NSLOCTEXT("HC", "Join", "BEITRETEN"));
		JoinButton->OnClicked.AddDynamic(this, &UHCMainMenuWidget::HandleJoin);
		HCUI::AddRow(Box, JoinButton, 28.f);

		UTextBlock* MenuLabel = nullptr;
		UButton* IntensityMenu = HCUI::Button(Tree, FText::GetEmpty(), HCUI::ButtonColor, &MenuLabel, 13);
		IntensityMenu->OnClicked.AddDynamic(this, &UHCMainMenuWidget::HandleIntensity);
		IntensityLabelMenu = MenuLabel;
		HCUI::AddRow(Box, IntensityMenu, 10.f);

		UButton* Quit = HCUI::Button(Tree, NSLOCTEXT("HC", "Quit", "BEENDEN"));
		Quit->OnClicked.AddDynamic(this, &UHCMainMenuWidget::HandleQuit);
		HCUI::AddRow(Box, Quit, 24.f);

		StatusText = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::MutedColor);
		HCUI::AddRow(Box, StatusText, 0.f);

		MainPanel = Box;
		HCUI::Place(Root, Box, FVector2D(0.1f, 0.5f), FVector2D(0.f, 0.5f), 460.f);
	}

	UTextBlock* Version = HCUI::Text(Tree, FText::FromString(FString::Printf(TEXT("Version %s · Prototyp M6"), *UHCSessionSubsystem::GetBuildVersion())), 12, HCUI::MutedColor);
	HCUI::Place(Root, Version, FVector2D(1.f, 1.f), FVector2D(1.f, 1.f));
}

void UHCMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	const bool bShowWarning = GI && !GI->HasAcceptedEpilepsyWarning();
	WarningPanel->SetVisibility(bShowWarning ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	MainPanel->SetVisibility(bShowWarning ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	RefreshIntensityLabels();
	if (GI)
	{
		NameBox->SetText(FText::FromString(GI->GetPlayerName()));
		const FText Message = GI->ConsumeMenuMessage();
		if (!Message.IsEmpty())
		{
			ShowStatus(Message, true);
		}
	}
	if (UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
	{
		StatusHandle = Sessions->OnStatus.AddUObject(this, &UHCMainMenuWidget::ShowStatus);
	}
}

void UHCMainMenuWidget::NativeDestruct()
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHCSessionSubsystem* Sessions = GameInstance->GetSubsystem<UHCSessionSubsystem>())
		{
			Sessions->OnStatus.Remove(StatusHandle);
		}
	}
	Super::NativeDestruct();
}

void UHCMainMenuWidget::ShowStatus(const FText& Message, bool bIsError)
{
	StatusText->SetText(Message);
	StatusText->SetColorAndOpacity(FSlateColor(bIsError ? FLinearColor(1.f, 0.4f, 0.3f) : HCUI::MutedColor));
}

FString UHCMainMenuWidget::GetEnteredName() const
{
	const FString Name = UHCSessionSubsystem::SanitizePlayerName(NameBox->GetText().ToString());
	if (UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance()))
	{
		GI->SetPlayerName(Name);
	}
	return Name;
}

void UHCMainMenuWidget::HandleAcceptWarning()
{
	if (UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance()))
	{
		GI->AcceptEpilepsyWarning();
	}
	WarningPanel->SetVisibility(ESlateVisibility::Collapsed);
	MainPanel->SetVisibility(ESlateVisibility::Visible);
}

void UHCMainMenuWidget::HandleHost()
{
	if (UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
	{
		Sessions->HostLobby(GetEnteredName());
	}
}

void UHCMainMenuWidget::HandleJoin()
{
	// Search limit: at most one attempt every 3 s (docs/03 §7.3)
	if (FPlatformTime::Seconds() < JoinBlockedUntil)
	{
		return;
	}
	JoinBlockedUntil = FPlatformTime::Seconds() + 3.0;
	if (UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
	{
		Sessions->Join(CodeBox->GetText().ToString(), GetEnteredName());
	}
}

void UHCMainMenuWidget::HandleIntensity()
{
	if (UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance()))
	{
		GI->CycleJumpscareIntensity();
	}
	RefreshIntensityLabels();
}

void UHCMainMenuWidget::RefreshIntensityLabels()
{
	const UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	const FText Label = UHCGameInstance::GetJumpscareIntensityLabel(GI ? GI->GetJumpscareIntensity() : EHCJumpscareIntensity::Full);
	if (IntensityLabelWarning)
	{
		IntensityLabelWarning->SetText(Label);
	}
	if (IntensityLabelMenu)
	{
		IntensityLabelMenu->SetText(Label);
	}
}

void UHCMainMenuWidget::HandleQuit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
