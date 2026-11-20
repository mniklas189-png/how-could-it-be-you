#include "UI/HCLobbyWidget.h"

#include "Core/HCKeyBindings.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Core/HCRoundOptions.h"
#include "Roles/HCJobs.h"
#include "UI/HCLook.h"
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

	// Right: the settings of the round, for the host only (the others see map and voice chat on the left)
	UBorder* Settings = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(24.f));
	SettingsPanel = Settings;
	UVerticalBox* Right = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	HCLook::StyleScrollBox(Scroll);
	Scroll->AddChild(Right);
	USizeBox* Limit = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Limit->SetMaxDesiredHeight(860.f);
	Limit->SetContent(Scroll);
	Settings->SetContent(Limit);
	HCUI::AddRow(Right, HCUI::Text(Tree, NSLOCTEXT("HC", "RoundSettingsTitle", "EINSTELLUNGEN"), 22, HCUI::TextColor, true), 4.f);
	HCUI::AddRow(Right, HCUI::Text(Tree, NSLOCTEXT("HC", "RoundSettingsSub", "Nur du als Host siehst und änderst sie."), 12, HCUI::MutedColor), 16.f);

	// Two buttons side by side; the chosen one is red (Refresh)
	auto AddPair = [Tree, Right](const FText& Title, const FText& First, const FText& Second, TArray<TObjectPtr<UButton>>& OutButtons) -> UTextBlock*
	{
		HCUI::AddRow(Right, HCUI::Text(Tree, Title, 14, HCUI::MutedColor), 6.f);
		UHorizontalBox* PairRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UButton* A = HCUI::Button(Tree, First, HCUI::ButtonColor, nullptr, 12);
		UButton* B = HCUI::Button(Tree, Second, HCUI::ButtonColor, nullptr, 12);
		HCUI::AddColumn(PairRow, A, 8.f, true);
		HCUI::AddColumn(PairRow, B, 0.f, true);
		OutButtons.Add(A);
		OutButtons.Add(B);
		HCUI::AddRow(Right, PairRow, 6.f);
		UTextBlock* Info = HCUI::Text(Tree, FText::GetEmpty(), 12, HCUI::MutedColor);
		Info->SetAutoWrapText(true);
		HCUI::AddRow(Right, Info, 14.f);
		return Info;
	};
	const FText On = NSLOCTEXT("HC", "OptionOn", "AN");
	const FText Off = NSLOCTEXT("HC", "OptionOff", "AUS");

	// Tasks of the Wandler: a fake list to pretend with, or none at all (index 0 = fake list, 1 = none)
	MonsterTasksInfo = AddPair(NSLOCTEXT("HC", "MonsterTasksTitle", "Aufgaben des Wandlers"), NSLOCTEXT("HC", "MonsterTasksOn", "FALSCHE AUFGABEN"),
		NSLOCTEXT("HC", "MonsterTasksOff", "KEINE AUFGABEN"), MonsterTasksButtons);
	MonsterTasksButtons[0]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleMonsterTasksOn);
	MonsterTasksButtons[1]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleMonsterTasksOff);

	// Votes in the meeting: open or anonymous (index 0 = open, 1 = anonymous)
	VotesInfo = AddPair(NSLOCTEXT("HC", "VotesTitle", "Abstimmung im Meeting"), NSLOCTEXT("HC", "VotesOpen", "OFFEN"), NSLOCTEXT("HC", "VotesAnonymous", "ANONYM"), VotesButtons);
	VotesButtons[0]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleVotesOpen);
	VotesButtons[1]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleVotesAnonymous);

	// Ventilation shafts (index 0 = on, 1 = off)
	VentsInfo = AddPair(NSLOCTEXT("HC", "VentsOptionTitle", "Lüftungsschächte"), On, Off, VentsButtons);
	VentsButtons[0]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleVentsOn);
	VentsButtons[1]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleVentsOff);

	// A second Wandler from eight players on
	SecondInfo = AddPair(FText::Format(NSLOCTEXT("HC", "SecondOptionTitleFmt", "Zweiter Wandler ab {0} Spielern"), HCRoundOptions::SecondMonsterPlayers), On, Off, SecondButtons);
	SecondButtons[0]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleSecondOn);
	SecondButtons[1]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleSecondOff);

	// Jobs, and which of them are given out
	JobsInfo = AddPair(NSLOCTEXT("HC", "JobsOptionTitle", "Berufe"), On, Off, JobsButtons);
	JobsButtons[0]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleJobsOn);
	JobsButtons[1]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleJobsOff);
	for (int32 RowIndex = 0; RowIndex < HCJobs::Count / 2; ++RowIndex)
	{
		UHorizontalBox* JobRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		for (int32 Column = 0; Column < 2; ++Column)
		{
			const int32 Index = RowIndex * 2 + Column;
			UButton* JobButton = HCUI::Button(Tree, HCJobs::Name(HCJobs::FromIndex(Index)).ToUpper(), HCUI::ButtonColor, nullptr, 12);
			JobButton->SetToolTipText(HCJobs::Ability(HCJobs::FromIndex(Index)));
			HCUI::AddColumn(JobRow, JobButton, Column == 0 ? 8.f : 0.f, true);
			JobButtons.Add(JobButton);
		}
		HCUI::AddRow(Right, JobRow, 6.f);
	}
	JobButtons[0]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleJob0);
	JobButtons[1]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleJob1);
	JobButtons[2]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleJob2);
	JobButtons[3]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleJob3);
	JobButtons[4]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleJob4);
	JobButtons[5]->OnClicked.AddDynamic(this, &UHCLobbyWidget::HandleJob5);
	HCUI::Place(Root, Settings, FVector2D(0.96f, 0.5f), FVector2D(1.f, 0.5f), 400.f);
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
		: GS->IsOnlineLobby() ? FText::Format(NSLOCTEXT("HC", "AddrOnlineIp", "Online von überall mit dem Code, im gleichen Netzwerk auch per IP: {0}"), FText::FromString(GS->GetHostAddress()))
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

	const UHCGameSettings* Settings = UHCGameSettings::Get();
	const int32 Count = GS->PlayerArray.Num();
	SettingsPanel->SetVisibility(bHost ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (bHost)
	{
		RefreshSettings(GS, Count);
	}
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

void UHCLobbyWidget::RefreshSettings(const AHCGameState* GS, int32 PlayerCount)
{
	auto Mark = [](const TArray<TObjectPtr<UButton>>& Buttons, int32 Chosen)
	{
		for (int32 Index = 0; Index < Buttons.Num(); ++Index)
		{
			Buttons[Index]->SetBackgroundColor(Index == Chosen ? HCUI::AccentRed : HCUI::ButtonColor);
		}
	};
	const bool bMonsterTasks = GS->DoesMonsterHaveTasks();
	Mark(MonsterTasksButtons, bMonsterTasks ? 0 : 1);
	MonsterTasksInfo->SetText(bMonsterTasks ? NSLOCTEXT("HC", "MonsterTasksOnInfo", "Der Wandler hat eine Aufgabenliste zum Vortäuschen. Sie zählt nie für die Crew.")
		: NSLOCTEXT("HC", "MonsterTasksOffInfo", "Der Wandler hat keine Aufgaben: keine Liste, keine Aufgaben-Menüs."));

	const bool bAnonymousVotes = GS->AreVotesAnonymous();
	Mark(VotesButtons, bAnonymousVotes ? 1 : 0);
	VotesInfo->SetText(bAnonymousVotes ? NSLOCTEXT("HC", "VotesAnonymousInfo", "Im Ergebnis steht nur, wie viele Stimmen jeder bekam, nicht von wem.")
		: NSLOCTEXT("HC", "VotesOpenInfo", "Im Ergebnis sieht jeder, wer wen gewählt hat."));

	const bool bVents = GS->AreVentsEnabled();
	Mark(VentsButtons, bVents ? 0 : 1);
	VentsInfo->SetText(bVents ? NSLOCTEXT("HC", "VentsOnInfo", "Der Wandler (und der Techniker) kriecht durch Luken im Boden von Raum zu Raum.")
		: NSLOCTEXT("HC", "VentsOffInfo", "Keine Luken: alle gehen zu Fuß."));

	const bool bSecond = GS->IsSecondMonsterEnabled();
	Mark(SecondButtons, bSecond ? 0 : 1);
	SecondInfo->SetText(!bSecond ? NSLOCTEXT("HC", "SecondOffInfo", "Immer nur ein Wandler.")
		: PlayerCount >= HCRoundOptions::SecondMonsterPlayers ? NSLOCTEXT("HC", "SecondOnNowInfo", "Jetzt sind genug da: zwei Wandler, die sich gegenseitig kennen.")
		: FText::Format(NSLOCTEXT("HC", "SecondOnLaterInfoFmt", "Zwei Wandler, die sich kennen, sobald {0} mitspielen (jetzt {1})."), HCRoundOptions::SecondMonsterPlayers, PlayerCount));

	const bool bJobs = GS->AreJobsEnabled();
	const uint8 Mask = GS->GetJobMask();
	Mark(JobsButtons, bJobs ? 0 : 1);
	int32 Enabled = 0;
	for (int32 Index = 0; Index < JobButtons.Num(); ++Index)
	{
		const bool bOn = (Mask & HCJobs::Bit(HCJobs::FromIndex(Index))) != 0;
		Enabled += bOn ? 1 : 0;
		JobButtons[Index]->SetBackgroundColor(bOn && bJobs ? HCUI::AccentRed : HCUI::ButtonColor);
		JobButtons[Index]->SetIsEnabled(bJobs);
	}
	JobsInfo->SetText(!bJobs ? NSLOCTEXT("HC", "JobsOffInfo", "Niemand hat einen Beruf.")
		: Enabled == 0 ? NSLOCTEXT("HC", "JobsNoneInfo", "Kein Beruf gewählt: dann hat auch niemand einen.")
		: NSLOCTEXT("HC", "JobsOnInfo", "Jeder in der Crew bekommt einen der roten Berufe, jeden nur einmal. Der Wandler bekommt einen als Tarnung. Den Kommandanten kennen alle."));
}

void UHCLobbyWidget::HandleVentsOn()
{
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetVentsEnabled(true);
		Refresh();
	}
}

void UHCLobbyWidget::HandleVentsOff()
{
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetVentsEnabled(false);
		Refresh();
	}
}

void UHCLobbyWidget::HandleSecondOn()
{
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetSecondMonsterEnabled(true);
		Refresh();
	}
}

void UHCLobbyWidget::HandleSecondOff()
{
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetSecondMonsterEnabled(false);
		Refresh();
	}
}

void UHCLobbyWidget::HandleJobsOn()
{
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetJobsEnabled(true);
		Refresh();
	}
}

void UHCLobbyWidget::HandleJobsOff()
{
	if (AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr)
	{
		GM->SetJobsEnabled(false);
		Refresh();
	}
}

void UHCLobbyWidget::HandleJob0() { ToggleJob(0); }
void UHCLobbyWidget::HandleJob1() { ToggleJob(1); }
void UHCLobbyWidget::HandleJob2() { ToggleJob(2); }
void UHCLobbyWidget::HandleJob3() { ToggleJob(3); }
void UHCLobbyWidget::HandleJob4() { ToggleJob(4); }
void UHCLobbyWidget::HandleJob5() { ToggleJob(5); }

void UHCLobbyWidget::ToggleJob(int32 Index)
{
	AHCGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AHCGameMode>() : nullptr;
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (GM && GS)
	{
		const EHCJob Job = HCJobs::FromIndex(Index);
		GM->SetJobEnabled(Job, (GS->GetJobMask() & HCJobs::Bit(Job)) == 0);
		Refresh();
	}
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
