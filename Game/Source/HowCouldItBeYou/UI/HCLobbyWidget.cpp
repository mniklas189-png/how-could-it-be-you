#include "UI/HCLobbyWidget.h"

#include "Core/HCKeyBindings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameMode.h"
#include "Core/HCGameSettings.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Online/HCJoinCode.h"
#include "HAL/PlatformApplicationMisc.h"
#include "OnlineSubsystemUtils.h"
#include "Online/HCSessionSubsystem.h"
#include "UI/HCHUD.h"
#include "UI/HCUIStyle.h"
#include "Voice/HCVoiceRules.h"
#include "World/HCMapCatalog.h"

void UHCLobbyWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.f, 0.f, 0.f, 0.f));

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(28.f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(Box);

	HCUI::AddRow(Box, HCUI::Text(Tree, NSLOCTEXT("HC", "LobbyTitle", "LOBBY"), 28, HCUI::TextColor, true), 12.f);
	// The code and the address are what you send to your friends: a click copies them
	UTextBlock* CodeLabel = nullptr;
	UButton* CodeButton = HCUI::Button(Tree, FText::GetEmpty(), FLinearColor(0.11f, 0.10f, 0.05f, 1.f), &CodeLabel, 20);
	CodeButton->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleCopyCode);
	CodeText = CodeLabel;
	CodeText->SetColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.92f, 0.62f)));
	HCUI::AddRow(Box, CodeButton, 6.f);
	UTextBlock* AddressLabel = nullptr;
	UButton* AddressButton = HCUI::Button(Tree, FText::GetEmpty(), HCUI::ButtonColor, &AddressLabel, 13);
	AddressButton->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleCopyAddress);
	AddressText = AddressLabel;
	// A longer text (the English one, or a long IP line) wraps inside the button instead of running over its edge
	AddressText->SetAutoWrapText(true);
	HCUI::AddRow(Box, AddressButton, 6.f);
	ModeText = HCUI::Text(Tree, FText::GetEmpty(), 12, HCUI::MutedColor);
	HCUI::AddRow(Box, ModeText, 16.f);

	// Map of the next round (the host chooses; index = position in HCMaps::All())
	MapTitle = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::MutedColor);
	HCUI::AddRow(Box, MapTitle, 6.f);
	UHorizontalBox* MapRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	const int32 MapCount = FMath::Min(HCMaps::All().Num(), 3);
	for (int32 Index = 0; Index < MapCount; ++Index)
	{
		UButton* MapButton = HCUI::Button(Tree, HCMaps::Get(Index).Name.ToUpper(), HCUI::ButtonColor, nullptr, 12);
		HCUI::AddColumn(MapRow, MapButton, Index + 1 < MapCount ? 8.f : 0.f, true);
		MapButtons.Add(MapButton);
	}
	if (MapButtons.IsValidIndex(0)) { MapButtons[0]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleMap0); }
	if (MapButtons.IsValidIndex(1)) { MapButtons[1]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleMap1); }
	if (MapButtons.IsValidIndex(2)) { MapButtons[2]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleMap2); }
	HCUI::AddRow(Box, MapRow, 6.f);
	MapInfo = HCUI::Text(Tree, FText::GetEmpty(), 12, HCUI::MutedColor);
	MapInfo->SetAutoWrapText(true);
	HCUI::AddRow(Box, MapInfo, 16.f);

	// Voice chat: when the players hear each other (the host chooses, the game state carries it to everybody)
	VoiceTitle = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::MutedColor);
	HCUI::AddRow(Box, VoiceTitle, 6.f);
	UHorizontalBox* RuleRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	VoiceRow = RuleRow;
	// In the order of EHCVoiceChatRule (Refresh marks the chosen one by its index)
	for (const EHCVoiceChatRule Rule : { EHCVoiceChatRule::MeetingsOnly, EHCVoiceChatRule::Proximity, EHCVoiceChatRule::Always })
	{
		UButton* RuleButton = HCUI::Button(Tree, HCVoiceRules::Label(Rule), HCUI::ButtonColor, nullptr, 12);
		HCUI::AddColumn(RuleRow, RuleButton, Rule == EHCVoiceChatRule::Always ? 0.f : 8.f, true);
		VoiceButtons.Add(RuleButton);
	}
	VoiceButtons[0]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleVoiceMeetings);
	VoiceButtons[1]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleVoiceNear);
	VoiceButtons[2]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleVoiceAlways);
	HCUI::AddRow(Box, RuleRow, 6.f);
	VoiceInfo = HCUI::Text(Tree, FText::GetEmpty(), 12, HCUI::MutedColor);
	HCUI::AddRow(Box, VoiceInfo, 16.f);

	// Tasks of the Wandler: a fake list to pretend with, or none at all (index 0 = fake list, 1 = none)
	MonsterTasksTitle = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::MutedColor);
	HCUI::AddRow(Box, MonsterTasksTitle, 6.f);
	UHorizontalBox* MonsterTasksRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UButton* TasksOnButton = HCUI::Button(Tree, NSLOCTEXT("HC", "MonsterTasksOn", "FALSCHE AUFGABEN"), HCUI::ButtonColor, nullptr, 12);
	TasksOnButton->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleMonsterTasksOn);
	HCUI::AddColumn(MonsterTasksRow, TasksOnButton, 8.f, true);
	UButton* TasksOffButton = HCUI::Button(Tree, NSLOCTEXT("HC", "MonsterTasksOff", "KEINE AUFGABEN"), HCUI::ButtonColor, nullptr, 12);
	TasksOffButton->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleMonsterTasksOff);
	HCUI::AddColumn(MonsterTasksRow, TasksOffButton, 0.f, true);
	MonsterTasksButtons.Add(TasksOnButton);
	MonsterTasksButtons.Add(TasksOffButton);
	HCUI::AddRow(Box, MonsterTasksRow, 6.f);
	MonsterTasksInfo = HCUI::Text(Tree, FText::GetEmpty(), 12, HCUI::MutedColor);
	HCUI::AddRow(Box, MonsterTasksInfo, 16.f);

	// Votes in the meeting: open or anonymous (index 0 = open, 1 = anonymous)
	VotesTitle = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::MutedColor);
	HCUI::AddRow(Box, VotesTitle, 6.f);
	UHorizontalBox* VotesRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UButton* VotesOpenButton = HCUI::Button(Tree, NSLOCTEXT("HC", "VotesOpen", "OFFEN"), HCUI::ButtonColor, nullptr, 12);
	VotesOpenButton->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleVotesOpen);
	HCUI::AddColumn(VotesRow, VotesOpenButton, 8.f, true);
	UButton* VotesAnonymousButton = HCUI::Button(Tree, NSLOCTEXT("HC", "VotesAnonymous", "ANONYM"), HCUI::ButtonColor, nullptr, 12);
	VotesAnonymousButton->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleVotesAnonymous);
	HCUI::AddColumn(VotesRow, VotesAnonymousButton, 0.f, true);
	VotesButtons.Add(VotesOpenButton);
	VotesButtons.Add(VotesAnonymousButton);
	HCUI::AddRow(Box, VotesRow, 6.f);
	VotesInfo = HCUI::Text(Tree, FText::GetEmpty(), 12, HCUI::MutedColor);
	HCUI::AddRow(Box, VotesInfo, 16.f);

	CountText = HCUI::Text(Tree, FText::GetEmpty(), 14, HCUI::MutedColor);
	HCUI::AddRow(Box, CountText, 6.f);
	PlayerList = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Box, PlayerList, 18.f);

	UTextBlock* StartLabel = nullptr;
	StartButton = HCUI::Button(Tree, NSLOCTEXT("HC", "StartRound", "RUNDE STARTEN"), HCUI::AccentRed, &StartLabel);
	StartButton->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleStart);
	HCUI::AddRow(Box, StartButton, 6.f);
	HintText = HCUI::Text(Tree, FText::GetEmpty(), 13, HCUI::MutedColor);
	HCUI::AddRow(Box, HintText, 14.f);

	UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UButton* Walk = HCUI::Button(Tree, FText::Format(NSLOCTEXT("HC", "WalkFmt", "HERUMLAUFEN ({0})"), HCKeys::ShortName(EHCBinding::TaskList)), HCUI::ButtonColor, nullptr, 13);
	Walk->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleWalk);
	HCUI::AddColumn(Row, Walk, 10.f, true);
	UButton* Leave = HCUI::Button(Tree, NSLOCTEXT("HC", "Leave", "VERLASSEN"), HCUI::ButtonColor, nullptr, 13);
	Leave->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleLeave);
	HCUI::AddColumn(Row, Leave, 0.f, true);
	HCUI::AddRow(Box, Row, 10.f);

	MessageText = HCUI::Text(Tree, FText::GetEmpty(), 13, FLinearColor(1.f, 0.4f, 0.3f));
	HCUI::AddRow(Box, MessageText, 0.f);

	HCUI::Place(Root, Panel, FVector2D(0.04f, 0.5f), FVector2D(0.f, 0.5f), 420.f);
	Refresh();
}

void UHCLobbyWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshTimer -= InDeltaTime;
	if (RefreshTimer <= 0.f)
	{
		RefreshTimer = 0.25f;
		Refresh();
	}
}

FReply UHCLobbyWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (HCKeys::Is(EHCBinding::TaskList, InKeyEvent.GetKey()))
	{
		HandleWalk();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F1)
	{
		if (AHCHUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AHCHUD>() : nullptr)
		{
			HUD->TogglePauseMenu();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UHCLobbyWidget::Refresh()
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	if (!GS)
	{
		return;
	}

	const FString Code = GS->GetJoinCode();
	const double NowSeconds = FPlatformTime::Seconds();
	CodeText->SetText(NowSeconds < CodeCopiedUntil ? NSLOCTEXT("HC", "CodeCopied", "Code kopiert!")
		: Code.IsEmpty() ? NSLOCTEXT("HC", "NoCode", "Kein Join-Code (Testmodus)")
		: FText::Format(NSLOCTEXT("HC", "CodeFmt", "Code: {0}"), FText::FromString(FHCJoinCode::Format(Code))));
	AddressText->SetText(NowSeconds < AddressCopiedUntil ? NSLOCTEXT("HC", "AddrCopied", "IP-Adresse kopiert!")
		: GS->GetHostAddress().IsEmpty() && !Code.IsEmpty() ? NSLOCTEXT("HC", "AddrOnline", "Online: Beitritt von überall mit dem Code")
		: FText::Format(NSLOCTEXT("HC", "AddrFmt", "IP (gleiches Netzwerk): {0}"), FText::FromString(GS->GetHostAddress())));
	ModeText->SetText(FText::Format(NSLOCTEXT("HC", "ModeFmt", "Klick auf Code oder IP kopiert   /   Modus: {0}"), FText::FromString(GS->GetModeName())));

	const bool bHost = PC && PC->IsHost();
	const int32 StationMap = GS->GetStationMap();
	MapTitle->SetText(bHost ? NSLOCTEXT("HC", "LobbyMapTitle", "Karte") : NSLOCTEXT("HC", "LobbyMapTitleGuest", "Karte (wählt der Host)"));
	for (int32 Index = 0; Index < MapButtons.Num(); ++Index)
	{
		MapButtons[Index]->SetBackgroundColor(Index == StationMap ? HCUI::AccentRed : HCUI::ButtonColor);
		MapButtons[Index]->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	}
	MapInfo->SetText(HCMaps::Get(StationMap).Description);

	const EHCVoiceChatRule VoiceRule = GS->GetVoiceChatRule();
	VoiceTitle->SetText(bHost ? NSLOCTEXT("HC", "VoiceRuleTitle", "Sprachchat") : NSLOCTEXT("HC", "VoiceRuleTitleGuest", "Sprachchat (wählt der Host)"));
	for (int32 Index = 0; Index < VoiceButtons.Num(); ++Index)
	{
		VoiceButtons[Index]->SetBackgroundColor(Index == static_cast<int32>(VoiceRule) ? HCUI::AccentRed : HCUI::ButtonColor);
		// The others see the choice but cannot click (no hover either)
		VoiceButtons[Index]->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	}
	VoiceInfo->SetText(HCVoiceRules::Description(VoiceRule));
	// Voice chat switched off in DefaultEngine.ini ([OnlineSubsystem] bHasVoiceEnabled): no rule to choose
	const ESlateVisibility VoiceVisibility = Online::GetVoiceInterface(GetWorld()).IsValid() ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	VoiceTitle->SetVisibility(VoiceVisibility);
	VoiceRow->SetVisibility(VoiceVisibility);
	VoiceInfo->SetVisibility(VoiceVisibility);

	const bool bMonsterTasks = GS->DoesMonsterHaveTasks();
	MonsterTasksTitle->SetText(bHost ? NSLOCTEXT("HC", "MonsterTasksTitle", "Aufgaben des Wandlers") : NSLOCTEXT("HC", "MonsterTasksTitleGuest", "Aufgaben des Wandlers (wählt der Host)"));
	for (int32 Index = 0; Index < MonsterTasksButtons.Num(); ++Index)
	{
		MonsterTasksButtons[Index]->SetBackgroundColor((Index == 0) == bMonsterTasks ? HCUI::AccentRed : HCUI::ButtonColor);
		MonsterTasksButtons[Index]->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	}
	MonsterTasksInfo->SetText(bMonsterTasks ? NSLOCTEXT("HC", "MonsterTasksOnInfo", "Der Wandler hat eine Aufgabenliste zum Vortäuschen. Sie zählt nie für die Crew.")
		: NSLOCTEXT("HC", "MonsterTasksOffInfo", "Der Wandler hat keine Aufgaben: keine Liste, keine Aufgaben-Menüs."));

	const bool bAnonymousVotes = GS->AreVotesAnonymous();
	VotesTitle->SetText(bHost ? NSLOCTEXT("HC", "VotesTitle", "Abstimmung im Meeting") : NSLOCTEXT("HC", "VotesTitleGuest", "Abstimmung im Meeting (wählt der Host)"));
	for (int32 Index = 0; Index < VotesButtons.Num(); ++Index)
	{
		VotesButtons[Index]->SetBackgroundColor((Index == 1) == bAnonymousVotes ? HCUI::AccentRed : HCUI::ButtonColor);
		VotesButtons[Index]->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::HitTestInvisible);
	}
	VotesInfo->SetText(bAnonymousVotes ? NSLOCTEXT("HC", "VotesAnonymousInfo", "Im Ergebnis steht nur, wie viele Stimmen jeder bekam, nicht von wem.")
		: NSLOCTEXT("HC", "VotesOpenInfo", "Im Ergebnis sieht jeder, wer wen gewählt hat."));

	const UHCGameSettings* Settings = UHCGameSettings::Get();
	const int32 Count = GS->PlayerArray.Num();
	CountText->SetText(FText::Format(NSLOCTEXT("HC", "CountFmt", "Spieler {0}/{1}"), Count, Settings->MaxPlayers));

	// Rebuild the list only when it changed
	FString Key;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS);
		Key += FString::Printf(TEXT("%s|%d;"), *PS->GetPlayerName(), HCPS ? HCPS->GetIdentity() : -1);
	}
	if (Key != LastPlayerKey)
	{
		LastPlayerKey = Key;
		PlayerList->ClearChildren();
		for (const APlayerState* PS : GS->PlayerArray)
		{
			const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS);
			const int32 Identity = HCPS ? FMath::Max(0, HCPS->GetIdentity()) : 0;
			UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			HCUI::AddColumn(Row, HCUI::Swatch(WidgetTree, HCIdentity::Color(Identity), 16.f), 10.f);
			HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::FromString(PS->GetPlayerName()), 16, HCUI::TextColor, true), 10.f);
			HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::Format(NSLOCTEXT("HC", "IdentFmt", "{0} · {1}"),
				HCIdentity::ColorName(Identity), HCIdentity::SymbolName(Identity)), 13, HCUI::MutedColor), 0.f);
			HCUI::AddRow(PlayerList, Row, 6.f);
		}
	}

	StartButton->SetVisibility(bHost ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	StartButton->SetIsEnabled(bHost && Count >= Settings->MinPlayersToStart);
	if (!bHost)
	{
		HintText->SetText(NSLOCTEXT("HC", "WaitHost", "Warte, bis der Host die Runde startet."));
	}
	else if (Count < Settings->MinPlayersToStart)
	{
		HintText->SetText(FText::Format(NSLOCTEXT("HC", "NeedMore", "Mindestens {0} Spieler nötig. Gib deinen Freunden den Code."), Settings->MinPlayersToStart));
	}
	else if (Count < Settings->MinPlayers)
	{
		HintText->SetText(FText::Format(NSLOCTEXT("HC", "TestRound", "Empfohlen sind {0}–{1} Spieler. Mit weniger ist es eine Testrunde."), Settings->MinPlayers, Settings->MaxPlayers));
	}
	else
	{
		HintText->SetText(NSLOCTEXT("HC", "Ready", "Bereit. Ein Wandler wird zufällig ausgewählt."));
	}
	MessageText->SetText(PC ? PC->GetClientMessage() : FText::GetEmpty());
}

void UHCLobbyWidget::HandleStart()
{
	if (AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer()))
	{
		PC->ServerRequestStartMatch();
	}
}

void UHCLobbyWidget::HandleVoiceMeetings()
{
	SetVoiceRule(EHCVoiceChatRule::MeetingsOnly);
}

void UHCLobbyWidget::HandleVoiceNear()
{
	SetVoiceRule(EHCVoiceChatRule::Proximity);
}

void UHCLobbyWidget::HandleVoiceAlways()
{
	SetVoiceRule(EHCVoiceChatRule::Always);
}

void UHCLobbyWidget::SetVoiceRule(EHCVoiceChatRule Rule)
{
	// Only the host has the game mode (it is the server); the buttons of the others cannot be clicked anyway
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetVoiceChatRule(Rule);
		Refresh();
	}
}

void UHCLobbyWidget::HandleMonsterTasksOn()
{
	SetMonsterHasTasks(true);
}

void UHCLobbyWidget::HandleMonsterTasksOff()
{
	SetMonsterHasTasks(false);
}

void UHCLobbyWidget::SetMonsterHasTasks(bool bHasTasks)
{
	// Only the host has the game mode, as with the voice rule
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetMonsterHasTasks(bHasTasks);
		Refresh();
	}
}

void UHCLobbyWidget::HandleVotesOpen()
{
	SetAnonymousVotes(false);
}

void UHCLobbyWidget::HandleVotesAnonymous()
{
	SetAnonymousVotes(true);
}

void UHCLobbyWidget::SetAnonymousVotes(bool bAnonymous)
{
	// Only the host has the game mode, as with the voice rule
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetAnonymousVotes(bAnonymous);
		Refresh();
	}
}

void UHCLobbyWidget::HandleMap0()
{
	SetStationMap(0);
}

void UHCLobbyWidget::HandleMap1()
{
	SetStationMap(1);
}

void UHCLobbyWidget::HandleMap2()
{
	SetStationMap(2);
}

void UHCLobbyWidget::SetStationMap(int32 MapIndex)
{
	// Only the host has the game mode, as with the voice rule
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetStationMap(MapIndex);
		Refresh();
	}
}

void UHCLobbyWidget::HandleWalk()
{
	if (AHCHUD* HUD = GetOwningPlayer() ? GetOwningPlayer()->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->ToggleLobbyMenu();
	}
}

void UHCLobbyWidget::HandleLeave()
{
	if (UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
	{
		Sessions->LeaveToMainMenu();
	}
}

void UHCLobbyWidget::HandleCopyCode()
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (GS && !GS->GetJoinCode().IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*FHCJoinCode::Format(GS->GetJoinCode()));
		CodeCopiedUntil = FPlatformTime::Seconds() + 1.6;
	}
}

void UHCLobbyWidget::HandleCopyAddress()
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (GS && !GS->GetHostAddress().IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*GS->GetHostAddress());
		AddressCopiedUntil = FPlatformTime::Seconds() + 1.6;
	}
}
