#include "UI/HCMeetingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerController.h"
#include "Core/HCPlayerState.h"
#include "Framework/Application/SlateApplication.h"
#include "Meeting/HCChat.h"
#include "Meeting/HCVoteTally.h"
#include "UI/HCHUD.h"
#include "UI/HCUIStyle.h"

void UHCVoteClickProxy::HandleClicked()
{
	if (UHCMeetingWidget* Widget = Owner.Get())
	{
		Widget->SubmitVote(TargetPlayerId);
	}
}

void UHCMeetingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	// The room stays visible behind the screen
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.01f, 0.012f, 0.02f, 0.8f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	UTextBlock* Title = HCUI::Text(Tree, NSLOCTEXT("HC", "MeetingTitle", "MEETING"), 38, HCUI::TextColor, true);
	Title->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, Title, 4.f, HAlign_Fill);

	CallerText = HCUI::Text(Tree, FText::GetEmpty(), 16, HCUI::MutedColor);
	CallerText->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, CallerText, 14.f, HAlign_Fill);

	StageText = HCUI::Text(Tree, FText::GetEmpty(), 22, HCUI::AccentGreen, true);
	StageText->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, StageText, 14.f, HAlign_Fill);

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(22.f, 18.f));
	PlayerList = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(PlayerList);
	HCUI::AddRow(Box, Panel, 14.f);

	SkipButton = HCUI::Button(Tree, NSLOCTEXT("HC", "VoteSkip", "ÜBERSPRINGEN"));
	SkipButton->OnClicked.AddDynamic(this, &UHCMeetingWidget::HandleSkip);
	HCUI::AddRow(Box, SkipButton, 12.f, HAlign_Center);

	InfoText = HCUI::Text(Tree, FText::GetEmpty(), 18, HCUI::TextColor, true);
	InfoText->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, InfoText, 8.f, HAlign_Fill);

	TaskText = HCUI::Text(Tree, FText::GetEmpty(), 13, HCUI::MutedColor);
	TaskText->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, TaskText, 0.f, HAlign_Fill);

	// The meeting on the left, the chat on the right
	UHorizontalBox* Columns = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USizeBox* MeetingColumn = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	MeetingColumn->SetWidthOverride(680.f);
	MeetingColumn->AddChild(Box);
	HCUI::AddColumn(Columns, MeetingColumn, 28.f);
	HCUI::AddColumn(Columns, BuildChat(Tree), 0.f);
	HCUI::Place(Root, Columns, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f));
}

UWidget* UHCMeetingWidget::BuildChat(UWidgetTree* Tree)
{
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Column, HCUI::Text(Tree, NSLOCTEXT("HC", "ChatTitle", "CHAT"), 16, HCUI::TextColor, true), 10.f);

	ChatScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	ChatScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
	UVerticalBoxSlot* ScrollSlot = Column->AddChildToVerticalBox(ChatScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ScrollSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));

	ChatBox = HCUI::TextBox(Tree, NSLOCTEXT("HC", "ChatBoxHint", "Nachricht schreiben ..."));
	// Enter sends and the box stays ready for the next line; Escape keeps the half-written text
	ChatBox->SetClearKeyboardFocusOnCommit(false);
	ChatBox->SetRevertTextOnEscape(false);
	ChatBox->OnTextCommitted.AddDynamic(this, &UHCMeetingWidget::HandleChatCommitted);
	ChatBox->OnTextChanged.AddDynamic(this, &UHCMeetingWidget::HandleChatChanged);
	HCUI::AddRow(Column, ChatBox, 6.f);

	ChatHint = HCUI::Text(Tree, FText::GetEmpty(), 12, HCUI::MutedColor);
	HCUI::AddRow(Column, ChatHint, 0.f);

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(16.f, 14.f));
	Panel->SetContent(Column);
	USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetWidthOverride(420.f);
	Size->SetHeightOverride(560.f);
	Size->AddChild(Panel);
	return Size;
}

bool UHCMeetingWidget::IsTypingChat() const
{
	return ChatBox && (ChatBox->HasKeyboardFocus() || ChatBox->HasFocusedDescendants());
}

void UHCMeetingWidget::KeepKeyboardFocus()
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	// Only when nothing (or the game view) has the keys; a context menu of the text box keeps its focus
	const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if (!Focused.IsValid() || Focused->GetType() == TEXT("SViewport"))
	{
		SetFocus();
	}
}

void UHCMeetingWidget::HandleChatChanged(const FText& Text)
{
	const FString& Current = Text.ToString();
	if (Current.Len() > HCChat::MaxLength)
	{
		ChatBox->SetText(FText::FromString(Current.Left(HCChat::MaxLength)));
	}
}

void UHCMeetingWidget::HandleChatCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod != ETextCommit::OnEnter)
	{
		return; // focus moved away: the text stays for later
	}
	if (HCChat::Sanitize(Text.ToString()).IsEmpty())
	{
		// Enter on an empty box leaves the chat, so Escape and F1 reach the screen again
		ChatBox->SetText(FText::GetEmpty());
		SetFocus();
		return;
	}
	if (TrySendChat(Text.ToString()))
	{
		ChatBox->SetText(FText::GetEmpty());
	}
}

bool UHCMeetingWidget::TrySendChat(const FString& Raw)
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	const FString Text = HCChat::Sanitize(Raw);
	const double Now = FPlatformTime::Seconds();
	// Too fast: the text stays in the box, the next Enter sends it
	if (!PC || !GS || Text.IsEmpty() || !HCChat::IsOpen(GS->GetMatchPhase(), GS->GetMeetingStage()) || Now - LastChatSent < HCChat::MinInterval)
	{
		return false;
	}
	LastChatSent = Now;
	PC->ServerSendChat(Text);
	return true;
}

bool UHCMeetingWidget::SendChatText(const FString& Text)
{
	if (!ChatBox || HCChat::Sanitize(Text).IsEmpty())
	{
		return false;
	}
	ChatBox->SetText(FText::FromString(Text));
	HandleChatCommitted(ChatBox->GetText(), ETextCommit::OnEnter);
	return ChatBox->GetText().IsEmpty();
}

void UHCMeetingWidget::AddChatLine(const FHCChatLine& Line, const AHCPlayerState* Own)
{
	UWidgetTree* Tree = WidgetTree;
	UWidget* Shown = nullptr;
	float Top = 0.f;
	if (Line.IsMeetingStart())
	{
		UTextBlock* Divider = HCUI::Text(Tree, FText::Format(NSLOCTEXT("HC", "ChatMeetingStart", "— MEETING {0} —"), static_cast<int32>(Line.MeetingNumber)), 12, HCUI::MutedColor, true);
		Divider->SetJustification(ETextJustify::Center);
		Shown = Divider;
		Top = ChatScroll->GetChildrenCount() > 0 ? 10.f : 0.f;
	}
	else
	{
		const bool bOwn = Own && Own->GetPlayerId() == Line.SenderId;
		const FLinearColor Color = Line.bFromGhost ? HCUI::MutedColor : HCUI::TextColor;
		const FLinearColor NameColor = Line.bFromGhost ? HCUI::MutedColor : bOwn ? FLinearColor(0.4f, 0.85f, 0.55f, 1.f) : HCUI::TextColor;
		FText Name = FText::FromString(Line.SenderName);
		if (Line.bFromGhost)
		{
			Name = FText::Format(NSLOCTEXT("HC", "ChatGhostName", "{0} (Geist)"), Name);
		}
		UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		const FLinearColor Swatch = HCIdentity::Color(Line.SenderIdentity) * (Line.bFromGhost ? 0.35f : 1.f);
		auto Add = [Row](UWidget* Child, float Right, float ChildTop, bool bFill)
		{
			UHorizontalBoxSlot* RowSlot = Row->AddChildToHorizontalBox(Child);
			RowSlot->SetPadding(FMargin(0.f, ChildTop, Right, 0.f));
			RowSlot->SetVerticalAlignment(VAlign_Top);
			if (bFill)
			{
				RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			}
		};
		Add(HCUI::Swatch(Tree, FLinearColor(Swatch.R, Swatch.G, Swatch.B, 1.f), 10.f), 8.f, 5.f, false);
		UTextBlock* NameText = HCUI::Text(Tree, FText::Format(NSLOCTEXT("HC", "ChatName", "{0}:"), Name), 14, NameColor, true);
		NameText->SetAutoWrapText(false);
		Add(NameText, 6.f, 0.f, false);
		// The text of a player is shown as it is: a plain text block, no markup
		Add(HCUI::Text(Tree, FText::FromString(Line.Text), 14, Color), 0.f, 0.f, true);
		Shown = Row;
	}
	if (UScrollBoxSlot* LineSlot = Cast<UScrollBoxSlot>(ChatScroll->AddChild(Shown)))
	{
		LineSlot->SetPadding(FMargin(0.f, Top, 4.f, 6.f));
	}
}

void UHCMeetingWidget::UpdateChat(const AHCPlayerState* Own)
{
	const AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	if (!PC || !ChatScroll)
	{
		return;
	}
	if (PC->GetChatEpoch() != ShownChatEpoch)
	{
		ShownChatEpoch = PC->GetChatEpoch();
		ShownChatSerial = 0;
		ChatScroll->ClearChildren();
	}
	// Follow new lines only if the reader is at the end (or wrote the line), not while scrolling back
	const bool bAtEnd = ChatScroll->GetScrollOffset() >= ChatScroll->GetScrollOffsetOfEnd() - 24.f;
	bool bAdded = false;
	bool bOwnLine = false;
	for (const FHCChatLine& Line : PC->GetChatLines())
	{
		if (Line.LocalSerial <= ShownChatSerial)
		{
			continue;
		}
		ShownChatSerial = Line.LocalSerial;
		AddChatLine(Line, Own);
		bAdded = true;
		bOwnLine |= Own && Own->GetPlayerId() == Line.SenderId;
	}
	while (ChatScroll->GetChildrenCount() > HCChat::MaxLines)
	{
		ChatScroll->RemoveChildAt(0);
	}
	if (bAdded && (bAtEnd || bOwnLine))
	{
		ChatScroll->ScrollToEnd();
	}

	const bool bGhost = Own && !Own->IsAlive();
	ChatHint->SetText(bGhost ? NSLOCTEXT("HC", "ChatHintGhost", "Du bist ein Geist: Nur andere Geister lesen deine Nachrichten.")
		: IsTypingChat() ? NSLOCTEXT("HC", "ChatHintTyping", "Enter: senden · Esc: zurück")
		: NSLOCTEXT("HC", "ChatHintIdle", "Enter: schreiben"));
}

bool UHCMeetingWidget::CanVoteNow(const AHCGameState* GS) const
{
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* Own = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	return GS && Own && GS->GetMeetingStage() == EHCMeetingStage::Voting && Own->IsAlive() && !Own->HasVoted();
}

bool UHCMeetingWidget::SubmitVote(int32 TargetPlayerId)
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	if (!PC || !CanVoteNow(GS))
	{
		return false;
	}
	PC->ServerCastVote(TargetPlayerId);
	return true;
}

void UHCMeetingWidget::HandleSkip()
{
	SubmitVote(FHCVoteTally::SkipId);
}

FReply UHCMeetingWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bTyping = IsTypingChat();
	if (Key == EKeys::Escape && bTyping)
	{
		// Leave the chat box first; the next Escape opens the menu
		SetFocus();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter && !bTyping && ChatBox)
	{
		ChatBox->SetKeyboardFocus();
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::F1)
	{
		// The pause menu stays reachable during a meeting
		const APlayerController* PC = GetOwningPlayer();
		if (AHCHUD* HUD = PC ? PC->GetHUD<AHCHUD>() : nullptr)
		{
			HUD->TogglePauseMenu();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

uint32 UHCMeetingWidget::ComputeSignature(const AHCGameState* GS) const
{
	uint32 Hash = HashCombine(GetTypeHash(static_cast<uint8>(GS->GetMeetingStage())), GetTypeHash(CanVoteNow(GS) ? 1 : 0));
	Hash = HashCombine(Hash, GetTypeHash(GS->GetVoteResults().Num()));
	Hash = HashCombine(Hash, GetTypeHash(GS->GetEjectedPlayerId()));
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS))
		{
			Hash = HashCombine(Hash, GetTypeHash(HCPS->GetPlayerId() * 4 + (HCPS->IsAlive() ? 1 : 0) + (HCPS->HasVoted() ? 2 : 0)));
			Hash = HashCombine(Hash, GetTypeHash(HCPS->GetPlayerName()));
		}
	}
	return Hash == 0 ? 1 : Hash;
}

void UHCMeetingWidget::Rebuild(const AHCGameState* GS)
{
	PlayerList->ClearChildren();
	Proxies.Reset();

	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* Own = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	const EHCMeetingStage Stage = GS->GetMeetingStage();
	const bool bCanVote = CanVoteNow(GS);
	const bool bResults = Stage == EHCMeetingStage::Results;

	TArray<const AHCPlayerState*> Players;
	TMap<int32, int32> IdentityById;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS))
		{
			Players.Add(HCPS);
			IdentityById.Add(HCPS->GetPlayerId(), HCPS->GetIdentity());
		}
	}
	Players.Sort([](const AHCPlayerState& A, const AHCPlayerState& B) { return A.GetIdentity() < B.GetIdentity(); });

	// Small swatches of the voters behind a row; empty with anonymous votes
	auto AddVotes = [&](UHorizontalBox* Row, int32 TargetId)
	{
		for (const FHCVoteResultEntry& Entry : GS->GetVoteResults())
		{
			if (Entry.TargetPlayerId != TargetId)
			{
				continue;
			}
			for (const int32 VoterId : Entry.VoterIds)
			{
				if (const int32* VoterIdentity = IdentityById.Find(VoterId))
				{
					HCUI::AddColumn(Row, HCUI::Swatch(WidgetTree, HCIdentity::Color(*VoterIdentity), 12.f), 4.f);
				}
			}
			HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::Format(NSLOCTEXT("HC", "VoteCount", "{0} {0}|plural(one=Stimme,other=Stimmen)"), Entry.Count),
				15, Entry.Count > 0 ? HCUI::TextColor : HCUI::MutedColor, Entry.Count > 0), 0.f);
		}
	};

	for (const AHCPlayerState* PS : Players)
	{
		const bool bAlive = PS->IsAlive();
		const bool bEjected = bResults && GS->GetEjectedPlayerId() == PS->GetPlayerId();
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Row, HCUI::Swatch(WidgetTree, bAlive ? HCIdentity::Color(PS->GetIdentity()) : HCIdentity::Color(PS->GetIdentity()) * 0.3f, 20.f), 12.f);
		const FLinearColor NameColor = bEjected ? HCUI::AccentRed : bAlive ? HCUI::TextColor : HCUI::MutedColor;
		FText Name = FText::FromString(PS->GetPlayerName());
		if (PS == Own)
		{
			Name = FText::Format(NSLOCTEXT("HC", "MeetingYou", "{0} (du)"), Name);
		}
		HCUI::AddColumn(Row, HCUI::Text(WidgetTree, Name, 18, NameColor, true), 12.f, true);
		if (PS->GetPlayerId() == GS->GetMeetingCallerId())
		{
			HCUI::AddColumn(Row, HCUI::Text(WidgetTree, NSLOCTEXT("HC", "MeetingCalled", "hat einberufen"), 12, HCUI::MutedColor), 14.f);
		}
		if (!bAlive)
		{
			HCUI::AddColumn(Row, HCUI::Text(WidgetTree, NSLOCTEXT("HC", "Dead", "tot"), 14, HCUI::MutedColor), 0.f);
		}
		else if (bResults)
		{
			AddVotes(Row, PS->GetPlayerId());
		}
		else if (Stage == EHCMeetingStage::Voting)
		{
			HCUI::AddColumn(Row, HCUI::Text(WidgetTree, PS->HasVoted() ? NSLOCTEXT("HC", "HasVoted", "hat gewählt") : NSLOCTEXT("HC", "Thinking", "überlegt ..."),
				14, PS->HasVoted() ? HCUI::AccentGreen : HCUI::MutedColor), bCanVote ? 14.f : 0.f);
			if (bCanVote && PS != Own)
			{
				UButton* VoteButton = HCUI::Button(WidgetTree, NSLOCTEXT("HC", "VoteFor", "WÄHLEN"), HCUI::AccentRed, nullptr, 13);
				UHCVoteClickProxy* Proxy = NewObject<UHCVoteClickProxy>(this);
				Proxy->TargetPlayerId = PS->GetPlayerId();
				Proxy->Owner = this;
				Proxies.Add(Proxy);
				VoteButton->OnClicked.AddDynamic(Proxy, &UHCVoteClickProxy::HandleClicked);
				HCUI::AddColumn(Row, VoteButton, 0.f);
			}
		}
		HCUI::AddRow(PlayerList, Row, 9.f);
	}
	if (bResults)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Row, HCUI::Text(WidgetTree, NSLOCTEXT("HC", "VoteSkipped", "Übersprungen / nicht gewählt"), 15, HCUI::MutedColor), 12.f, true);
		AddVotes(Row, FHCVoteTally::SkipId);
		HCUI::AddRow(PlayerList, Row, 0.f);
	}
}

void UHCMeetingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	const uint32 Signature = ComputeSignature(GS);
	if (Signature != ShownSignature)
	{
		ShownSignature = Signature;
		Rebuild(GS);
	}

	const AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	const AHCPlayerState* Own = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	const EHCMeetingStage Stage = GS->GetMeetingStage();
	const int32 Seconds = FMath::CeilToInt(GS->GetPhaseRemainingSeconds());
	UpdateChat(Own);
	KeepKeyboardFocus();

	const FText Caller = FText::FromString(GS->GetMeetingCallerName());
	CallerText->SetText(GS->GetMeetingVictimName().IsEmpty()
		? FText::Format(NSLOCTEXT("HC", "MeetingByButton", "{0} hat den Notfall-Knopf gedrückt."), Caller)
		: FText::Format(NSLOCTEXT("HC", "MeetingByReport", "{0} hat die Überreste von {1} gefunden."), Caller, FText::FromString(GS->GetMeetingVictimName())));

	FText StageLabel;
	FText Info;
	FLinearColor InfoColor = HCUI::MutedColor;
	switch (Stage)
	{
	case EHCMeetingStage::Gathering:
		StageLabel = NSLOCTEXT("HC", "StageGathering", "ALLE VERSAMMELN SICH ...");
		break;
	case EHCMeetingStage::Discussion:
		StageLabel = FText::Format(NSLOCTEXT("HC", "StageDiscussion", "DISKUSSION · {0} s"), Seconds);
		Info = NSLOCTEXT("HC", "InfoDiscussion", "Redet miteinander oder schreibt im Chat. Danach wird abgestimmt.");
		break;
	case EHCMeetingStage::Voting:
		StageLabel = FText::Format(NSLOCTEXT("HC", "StageVoting", "ABSTIMMUNG · {0} s"), Seconds);
		if (!Own || !Own->IsAlive())
		{
			Info = NSLOCTEXT("HC", "VoteGhost", "Geister stimmen nicht ab.");
		}
		else if (Own->HasVoted())
		{
			Info = NSLOCTEXT("HC", "InfoVoted", "Stimme abgegeben. Warte auf die anderen ...");
		}
		else
		{
			Info = NSLOCTEXT("HC", "InfoVote", "Wähle eine Person oder überspringe. Die Stimme ist endgültig.");
			InfoColor = HCUI::TextColor;
		}
		break;
	case EHCMeetingStage::Results:
		StageLabel = NSLOCTEXT("HC", "StageResults", "ERGEBNIS");
		Info = GS->GetMeetingText();
		InfoColor = GS->GetEjectedPlayerId() != INDEX_NONE ? HCUI::AccentRed : HCUI::TextColor;
		break;
	default:
		break;
	}
	// A message from the server ("Bitte neu wählen") has priority for its few seconds
	if (PC && !PC->GetClientMessage().IsEmpty() && Stage == EHCMeetingStage::Voting)
	{
		Info = PC->GetClientMessage();
		InfoColor = HCUI::AccentRed;
	}
	StageText->SetText(StageLabel);
	InfoText->SetText(Info);
	InfoText->SetColorAndOpacity(FSlateColor(InfoColor));
	SkipButton->SetVisibility(CanVoteNow(GS) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	TaskText->SetText(GS->IsTaskBarVisible() && GS->GetTotalTasks() > 0
		? FText::Format(NSLOCTEXT("HC", "MeetingTasks", "Aufgaben der Crew: {0} von {1}"), GS->GetCompletedTasks(), GS->GetTotalTasks())
		: FText::GetEmpty());
}
