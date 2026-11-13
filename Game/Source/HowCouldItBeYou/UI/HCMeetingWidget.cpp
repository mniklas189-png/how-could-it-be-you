#include "UI/HCMeetingWidget.h"

#include "Audio/HCProceduralAudio.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
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
#include "Misc/CommandLine.h"
#include "UI/HCHUD.h"
#include "UI/HCLook.h"
#include "UI/HCUIStyle.h"
#include "Core/HCText.h"
#include "EngineUtils.h"
#include "World/HCMapCatalog.h"
#include "World/HCStationBuilder.h"

namespace HCMeetingLayout
{
	constexpr float ColumnWidth = 780.f;
	constexpr float CardGap = 12.f;
	constexpr float CardWidth = (ColumnWidth - CardGap) * 0.5f;
	constexpr float CardHeight = 78.f;
	constexpr float HeaderHeight = 150.f;
	constexpr float ChatWidth = 440.f;
	/** The chat has a fixed height: its scroll box would otherwise ask for the height of all its lines. */
	constexpr float ChatHeight = 600.f;
	/** After a vote was sent, the ballot stays closed this long while the server's answer is on its way. */
	constexpr double VoteEcho = 1.5;
}

void UHCMeetingWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	OpenedAt = FPlatformTime::Seconds();
	StageSince = OpenedAt;

	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor::Transparent);
	const TWeakObjectPtr<UHCMeetingWidget> Weak(this);
	auto FullScreen = [Root](UWidget* Child)
	{
		UCanvasPanelSlot* ChildSlot = Root->AddChildToCanvas(Child);
		ChildSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		ChildSlot->SetOffsets(FMargin(0.f));
	};

	// The room stays visible behind the screen, as a dark shape
	FullScreen(HCLook::Paint(Tree, FVector2D::ZeroVector, [Weak](const FHCDraw& D, const FHCPaintState&)
	{
		if (const UHCMeetingWidget* Widget = Weak.Get())
		{
			Widget->PaintBackdrop(D);
		}
	}));

	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Box, HCLook::Paint(Tree, FVector2D(HCMeetingLayout::ColumnWidth, HCMeetingLayout::HeaderHeight), [Weak](const FHCDraw& D, const FHCPaintState&)
	{
		if (const UHCMeetingWidget* Widget = Weak.Get())
		{
			Widget->PaintHeader(D);
		}
	}), 18.f);

	PlayerGrid = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Box, PlayerGrid, 4.f);

	// What to do now, or the verdict
	HCUI::AddRow(Box, HCLook::Paint(Tree, FVector2D(HCMeetingLayout::ColumnWidth, 38.f), [Weak](const FHCDraw& D, const FHCPaintState&)
	{
		if (const UHCMeetingWidget* Widget = Weak.Get())
		{
			Widget->PaintInfo(D);
		}
	}), 12.f);

	// The ballot: "nobody" as its own line, then the button that makes the choice final
	SkipLine = HCLook::Control(Tree, FVector2D(HCMeetingLayout::ColumnWidth, 46.f),
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCMeetingWidget* Widget = Weak.Get())
			{
				Widget->PaintSkipLine(D, S);
			}
		},
		[Weak](const FVector2D&, const FVector2D&)
		{
			if (UHCMeetingWidget* Widget = Weak.Get())
			{
				Widget->PickTarget(FHCVoteTally::SkipId);
			}
		});
	SkipLine->WantsHand = [Weak]()
	{
		const UHCMeetingWidget* Widget = Weak.Get();
		return Widget && Widget->CanPickNow();
	};
	HCUI::AddRow(Box, SkipLine, 14.f);

	ConfirmButton = HCLook::Control(Tree, FVector2D(HCMeetingLayout::ColumnWidth, 50.f),
		[Weak](const FHCDraw& D, const FHCPaintState& S)
		{
			if (const UHCMeetingWidget* Widget = Weak.Get())
			{
				Widget->PaintConfirm(D, S);
			}
		},
		[](const FVector2D&, const FVector2D&) {});
	ConfirmButton->OnReleased = [Weak](bool bInside)
	{
		UHCMeetingWidget* Widget = Weak.Get();
		if (Widget && bInside)
		{
			Widget->ConfirmVote();
		}
	};
	ConfirmButton->WantsHand = [Weak]()
	{
		const UHCMeetingWidget* Widget = Weak.Get();
		return Widget && Widget->bHasPick;
	};
	HCUI::AddRow(Box, ConfirmButton, 0.f);

	// The meeting on the left, the chat on the right, as tall as the meeting
	UHorizontalBox* Columns = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USizeBox* MeetingColumn = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	MeetingColumn->SetWidthOverride(HCMeetingLayout::ColumnWidth);
	MeetingColumn->AddChild(Box);
	HCUI::AddColumn(Columns, MeetingColumn, 34.f);
	UHorizontalBoxSlot* ChatSlot = Columns->AddChildToHorizontalBox(BuildChat(Tree));
	ChatSlot->SetVerticalAlignment(VAlign_Fill);
	HCUI::Place(Root, Columns, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f));

	// Scan lines over everything, like on the role card
	FullScreen(HCLook::Paint(Tree, FVector2D::ZeroVector, [](const FHCDraw& D, const FHCPaintState&) { D.ScanLines(0.14f); }));
}

UWidget* UHCMeetingWidget::BuildChat(UWidgetTree* Tree)
{
	UVerticalBox* Column = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	HCUI::AddRow(Column, HCLook::Label(Tree, NSLOCTEXT("HC", "ChatTitle", "CHAT"), 12.f, HCLook::BoneMuted, 320), 10.f);
	HCUI::AddRow(Column, HCLook::Rule(Tree, HCLook::Alpha(HCLook::Bone, 0.16f)), 12.f);

	ChatScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	ChatScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
	HCLook::StyleScrollBox(ChatScroll);
	UVerticalBoxSlot* ScrollSlot = Column->AddChildToVerticalBox(ChatScroll);
	ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	ScrollSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));

	ChatBox = HCUI::TextBox(Tree, NSLOCTEXT("HC", "ChatBoxHint", "Nachricht schreiben ..."));
	HCLook::StyleTextBox(ChatBox);
	// Enter sends and the box stays ready for the next line; Escape keeps the half-written text
	ChatBox->SetClearKeyboardFocusOnCommit(false);
	ChatBox->SetRevertTextOnEscape(false);
	ChatBox->OnTextCommitted.AddDynamic(this, &UHCMeetingWidget::HandleChatCommitted);
	ChatBox->OnTextChanged.AddDynamic(this, &UHCMeetingWidget::HandleChatChanged);
	HCUI::AddRow(Column, ChatBox, 8.f);

	ChatHint = HCLook::Text(Tree, FText::GetEmpty(), 11.f, HCLook::BoneMuted);
	HCUI::AddRow(Column, ChatHint, 0.f);

	UOverlay* Panel = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
	UOverlaySlot* BackSlot = Panel->AddChildToOverlay(HCLook::Paint(Tree, FVector2D::ZeroVector, [](const FHCDraw& D, const FHCPaintState&)
	{
		HCLook::DrawPlate(D, HCLook::Alpha(HCLook::BoneMuted, 0.28f), 0.88f);
		D.Brackets(FVector2D::ZeroVector, D.Size, 14.f, 2.f, HCLook::Alpha(HCLook::Bone, 0.5f));
	}));
	BackSlot->SetHorizontalAlignment(HAlign_Fill);
	BackSlot->SetVerticalAlignment(VAlign_Fill);
	UOverlaySlot* ContentSlot = Panel->AddChildToOverlay(Column);
	ContentSlot->SetHorizontalAlignment(HAlign_Fill);
	ContentSlot->SetVerticalAlignment(VAlign_Fill);
	ContentSlot->SetPadding(FMargin(18.f, 16.f));

	USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetWidthOverride(HCMeetingLayout::ChatWidth);
	Size->SetHeightOverride(HCMeetingLayout::ChatHeight);
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
		UTextBlock* Divider = HCLook::Label(Tree, FText::Format(NSLOCTEXT("HC", "ChatMeetingStart", "— MEETING {0} —"), static_cast<int32>(Line.MeetingNumber)),
			10.f, HCLook::Alpha(HCLook::BoneMuted, 0.8f), 320);
		Divider->SetJustification(ETextJustify::Center);
		Shown = Divider;
		Top = ChatScroll->GetChildrenCount() > 0 ? 12.f : 0.f;
	}
	else
	{
		// Who speaks in the suit colour on its own small line, the text below it
		const bool bOwn = Own && Own->GetPlayerId() == Line.SenderId;
		const FLinearColor Suit = HCIdentity::Color(Line.SenderIdentity);
		const FLinearColor NameColor = Line.bFromGhost ? HCLook::BoneMuted : bOwn ? HCLook::Bone : FMath::Lerp(Suit, HCLook::Bone, 0.45f);
		FText Name = FText::FromString(Line.SenderName);
		if (Line.bFromGhost)
		{
			Name = FText::Format(NSLOCTEXT("HC", "ChatGhostName", "{0} (Geist)"), Name);
		}
		UHorizontalBox* Head = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Head, HCUI::Swatch(Tree, HCLook::Alpha(Suit, Line.bFromGhost ? 0.4f : 1.f), 8.f), 8.f);
		HCUI::AddColumn(Head, HCLook::Label(Tree, Name, 11.f, NameColor, 180), 0.f);
		UVerticalBox* Entry = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		HCUI::AddRow(Entry, Head, 2.f);
		// The text of a player is shown as it is: a plain text block, no markup
		HCUI::AddRow(Entry, HCLook::Text(Tree, FText::FromString(Line.Text), 14.f, Line.bFromGhost ? HCLook::BoneMuted : HCLook::Bone), 0.f);
		Shown = Entry;
	}
	if (UScrollBoxSlot* LineSlot = Cast<UScrollBoxSlot>(ChatScroll->AddChild(Shown)))
	{
		LineSlot->SetPadding(FMargin(0.f, Top, 10.f, 10.f));
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

// ---------------------------------------------------------------------------
// The ballot

const AHCPlayerState* UHCMeetingWidget::FindPlayer(const AHCGameState* GS, int32 PlayerId) const
{
	if (GS)
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			if (PS && PS->GetPlayerId() == PlayerId)
			{
				return Cast<AHCPlayerState>(PS);
			}
		}
	}
	return nullptr;
}

bool UHCMeetingWidget::CanVoteNow(const AHCGameState* GS) const
{
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* Own = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	return GS && Own && GS->GetMeetingStage() == EHCMeetingStage::Voting && Own->IsAlive() && !Own->HasVoted();
}

bool UHCMeetingWidget::CanPickNow() const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	return CanVoteNow(GS) && FPlatformTime::Seconds() - VoteSentAt > HCMeetingLayout::VoteEcho;
}

bool UHCMeetingWidget::CanPickPlayer(int32 PlayerId) const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* Own = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	const AHCPlayerState* PS = FindPlayer(GS, PlayerId);
	return PS && PS != Own && PS->IsAlive() && CanPickNow();
}

void UHCMeetingWidget::PickTarget(int32 TargetPlayerId)
{
	if (TargetPlayerId == FHCVoteTally::SkipId ? !CanPickNow() : !CanPickPlayer(TargetPlayerId))
	{
		return;
	}
	// A second click on the same choice takes it back
	bHasPick = !(bHasPick && PickedTarget == TargetPlayerId);
	PickedTarget = TargetPlayerId;
	HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Click, 0.5f);
}

void UHCMeetingWidget::ConfirmVote()
{
	if (bHasPick && CanPickNow() && SubmitVote(PickedTarget))
	{
		HCProceduralAudio::Play2D(this, HCProceduralAudio::ESound::Click, 0.8f);
	}
}

bool UHCMeetingWidget::SubmitVote(int32 TargetPlayerId)
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	AHCPlayerController* PC = Cast<AHCPlayerController>(GetOwningPlayer());
	if (!PC || !CanVoteNow(GS))
	{
		return false;
	}
	bHasPick = false;
	bHasOwnVote = true;
	OwnVoteTarget = TargetPlayerId;
	VoteSentAt = FPlatformTime::Seconds();
	PC->ServerCastVote(TargetPlayerId);
	return true;
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
	uint32 Hash = GetTypeHash(GS->PlayerArray.Num());
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS))
		{
			Hash = HashCombine(Hash, GetTypeHash(HCPS->GetPlayerId() * 16 + HCPS->GetIdentity()));
		}
	}
	return Hash == 0 ? 1 : Hash;
}

void UHCMeetingWidget::Rebuild(const AHCGameState* GS)
{
	PlayerGrid->ClearChildren();

	TArray<const AHCPlayerState*> Players;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS))
		{
			Players.Add(HCPS);
		}
	}
	Players.Sort([](const AHCPlayerState& A, const AHCPlayerState& B) { return A.GetIdentity() < B.GetIdentity(); });

	// Two cards per row. A card reads the state of its player while it is painted, so votes need no rebuild.
	const TWeakObjectPtr<UHCMeetingWidget> Weak(this);
	UHorizontalBox* Row = nullptr;
	for (int32 Index = 0; Index < Players.Num(); ++Index)
	{
		if (Index % 2 == 0)
		{
			Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			HCUI::AddRow(PlayerGrid, Row, HCMeetingLayout::CardGap);
		}
		const int32 PlayerId = Players[Index]->GetPlayerId();
		UHCPaintBox* Card = HCLook::Control(WidgetTree, FVector2D(HCMeetingLayout::CardWidth, HCMeetingLayout::CardHeight),
			[Weak, PlayerId, Index](const FHCDraw& D, const FHCPaintState& S)
			{
				if (const UHCMeetingWidget* Widget = Weak.Get())
				{
					Widget->PaintCard(D, S, PlayerId, Index);
				}
			},
			[Weak, PlayerId](const FVector2D&, const FVector2D&)
			{
				if (UHCMeetingWidget* Widget = Weak.Get())
				{
					Widget->PickTarget(PlayerId);
				}
			});
		Card->WantsHand = [Weak, PlayerId]()
		{
			const UHCMeetingWidget* Widget = Weak.Get();
			return Widget && Widget->CanPickPlayer(PlayerId);
		};
		HCUI::AddColumn(Row, Card, Index % 2 == 0 ? HCMeetingLayout::CardGap : 0.f);
	}
}

// ---------------------------------------------------------------------------
// Painting

void UHCMeetingWidget::PaintBackdrop(const FHCDraw& D) const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const bool bReport = GS && !GS->GetMeetingVictimName().IsEmpty();
	FHCDraw F = D;
	F.Opacity *= HCLook::Appear(static_cast<float>(FPlatformTime::Seconds() - OpenedAt), 0.f, 0.35f);
	const FVector2D S = F.Size;

	F.Box(FVector2D::ZeroVector, S, HCLook::Alpha(HCLook::Ink, 0.82f));
	// Alarm light from above, stronger when somebody was found dead
	F.Gradient(FVector2D::ZeroVector, FVector2D(S.X, S.Y * 0.42), HCLook::Alpha(HCLook::Blood, bReport ? 0.2f : 0.09f), HCLook::Alpha(HCLook::Blood, 0.f), false);
	F.Gradient(FVector2D(0.0, S.Y * 0.68), FVector2D(S.X, S.Y * 0.32), HCLook::Alpha(HCLook::Ink, 0.f), HCLook::Alpha(HCLook::Ink, 0.75f), false);
	F.Brackets(FVector2D(36.0, 36.0), S - FVector2D(72.0, 72.0), 30.f, 2.f, HCLook::Alpha(HCLook::Bone, 0.3f));
	F.Box(FVector2D(66.0, 69.0), FVector2D(9.0, 9.0), HCLook::Blood);
	// Where the meeting is held: the place of the map and its meeting room ("Kepler-7 · Kommandozentrale")
	FText Place = NSLOCTEXT("HC", "MapKepler7Place", "Kepler-7");
	FText Room = HCText::Room(TEXT("Kommandozentrale"));
	for (TActorIterator<AHCStationBuilder> It(GetWorld()); It; ++It)
	{
		Place = HCMaps::ForLayout(It->GetLayout().MapName).Place;
		Room = HCText::Room(It->GetLayout().SpawnRoom);
	}
	F.Text(HCLook::Upper(FText::Format(NSLOCTEXT("HC", "MeetingPlaceFmt", "{0}  ·  {1}"), Place, Room)), FVector2D(86.0, 63.0), HCLook::Mono(12.f, 260), HCLook::BoneMuted);
	D.Layer = F.Layer;
}

void UHCMeetingWidget::PaintHeader(const FHCDraw& D) const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const float In = HCLook::Appear(static_cast<float>(Now - OpenedAt), 0.f, 0.4f);
	const float StageIn = HCLook::Appear(static_cast<float>(Now - StageSince), 0.f, 0.35f);
	const EHCMeetingStage Stage = GS->GetMeetingStage();
	const bool bReport = !GS->GetMeetingVictimName().IsEmpty();
	const double Width = D.Size.X;

	// Why everybody was called
	D.Box(FVector2D(0.0, 5.0), FVector2D(9.0, 9.0), HCLook::Alpha(bReport ? HCLook::Blood : HCLook::Bone, In));
	D.Text(HCLook::Upper(bReport ? NSLOCTEXT("HC", "MeetingKickReport", "Überreste gefunden") : NSLOCTEXT("HC", "MeetingKickButton", "Notfall-Knopf gedrückt")),
		FVector2D(20.0, 0.0), HCLook::Mono(12.f, 260), HCLook::Alpha(bReport ? HCLook::Blood : HCLook::BoneMuted, In));

	if (GS->IsTaskBarVisible() && GS->GetTotalTasks() > 0)
	{
		// How far the crew is, in the same line on the right (as on the HUD)
		const FSlateFontInfo TaskFont = HCLook::Caps(10.f, 240);
		const FString Count = FString::Printf(TEXT("%d / %d"), GS->GetCompletedTasks(), GS->GetTotalTasks());
		const FVector2D CountSize = D.TextRight(Count, Width, 2.0, TaskFont, HCLook::Alpha(HCLook::BoneMuted, In));
		const double BarWidth = 150.0;
		const double BarX = Width - CountSize.X - 12.0 - BarWidth;
		const float Done = FMath::Clamp(static_cast<float>(GS->GetCompletedTasks()) / GS->GetTotalTasks(), 0.f, 1.f);
		D.Box(FVector2D(BarX, 2.0 + CountSize.Y * 0.5 - 1.0), FVector2D(BarWidth, 2.0), HCLook::Alpha(HCLook::Bone, 0.16f * In));
		D.Box(FVector2D(BarX, 2.0 + CountSize.Y * 0.5 - 1.0), FVector2D(BarWidth * Done, 2.0), HCLook::Alpha(HCLook::Signal, In));
		D.TextRight(HCLook::Upper(NSLOCTEXT("HC", "HudCrewTasks", "AUFGABEN DER CREW")), BarX - 12.0, 2.0, TaskFont, HCLook::Alpha(HCLook::BoneMuted, In));
	}

	// The stage is the headline; its letters close up whenever it changes
	FText StageName;
	switch (Stage)
	{
	case EHCMeetingStage::Gathering:
		StageName = NSLOCTEXT("HC", "MeetingStageGather", "Versammlung");
		break;
	case EHCMeetingStage::Discussion:
		StageName = NSLOCTEXT("HC", "MeetingStageTalk", "Diskussion");
		break;
	case EHCMeetingStage::Voting:
		StageName = NSLOCTEXT("HC", "MeetingStageVote", "Abstimmung");
		break;
	case EHCMeetingStage::Results:
		StageName = NSLOCTEXT("HC", "MeetingStageResult", "Ergebnis");
		break;
	default:
		break;
	}
	D.Text(HCLook::Upper(StageName), FVector2D(-2.0, 16.0), HCLook::Caps(54.f, FMath::RoundToInt(FMath::Lerp(280.f, 60.f, StageIn))), HCLook::Alpha(HCLook::Bone, In * StageIn));

	// The countdown on the right; red and pulsing in the last ten seconds
	const float Remaining = GS->GetPhaseRemainingSeconds();
	const bool bCountdown = (Stage == EHCMeetingStage::Discussion || Stage == EHCMeetingStage::Voting) && Remaining > 0.f;
	const bool bLow = bCountdown && Remaining <= 10.f;
	const FLinearColor TimeColor = bLow ? HCLook::Blood : HCLook::Bone;
	if (bCountdown)
	{
		const float Pulse = bLow ? 0.65f + 0.35f * FMath::Abs(FMath::Sin(static_cast<float>(Now) * PI)) : 1.f;
		D.TextRight(FString::FromInt(FMath::CeilToInt(Remaining)), Width, 16.0, HCLook::Caps(54.f, 0), HCLook::Alpha(TimeColor, In * Pulse));
	}

	const FText Caller = FText::FromString(GS->GetMeetingCallerName());
	const FText Reason = bReport
		? FText::Format(NSLOCTEXT("HC", "MeetingByReport", "{0} hat die Überreste von {1} gefunden."), Caller, FText::FromString(GS->GetMeetingVictimName()))
		: FText::Format(NSLOCTEXT("HC", "MeetingByButton", "{0} hat den Notfall-Knopf gedrückt."), Caller);
	const FSlateFontInfo ReasonFont = HCLook::Body(16.f);
	D.Text(FHCDraw::Fit(Reason.ToString(), ReasonFont, Width), FVector2D(0.0, 104.0), ReasonFont, HCLook::Alpha(FMath::Lerp(HCLook::BoneMuted, HCLook::Bone, 0.4f), In));

	// A hairline that runs out with the stage (and fills while everybody gathers)
	const double BarY = D.Size.Y - 5.0;
	const float Fraction = StageTotal > 0.f ? FMath::Clamp(Remaining / StageTotal, 0.f, 1.f) : 0.f;
	D.Box(FVector2D(0.0, BarY), FVector2D(Width, 3.0), HCLook::Alpha(HCLook::Bone, 0.14f * In));
	D.Box(FVector2D(0.0, BarY), FVector2D(Width * (Stage == EHCMeetingStage::Gathering ? 1.f - Fraction : Fraction), 3.0), HCLook::Alpha(TimeColor, In));
}

int32 UHCMeetingWidget::PaintVotes(const FHCDraw& D, const AHCGameState* GS, int32 TargetPlayerId, double RightX, double CenterY) const
{
	const FHCVoteResultEntry* Entry = GS->GetVoteResults().FindByPredicate([TargetPlayerId](const FHCVoteResultEntry& Other) { return Other.TargetPlayerId == TargetPlayerId; });
	if (!Entry)
	{
		return 0;
	}
	const float T = static_cast<float>(FPlatformTime::Seconds() - StageSince);
	// With anonymous votes the server sends only the number: grey squares
	const int32 Pips = Entry->VoterIds.Num() > 0 ? Entry->VoterIds.Num() : Entry->Count;
	const FSlateFontInfo CountFont = HCLook::Caps(26.f, 0);
	const FString CountText = FString::FromInt(Entry->Count);
	const FVector2D CountSize = FHCDraw::Measure(CountText, CountFont);
	D.Text(CountText, FVector2D(RightX - CountSize.X, CenterY - CountSize.Y * 0.5), CountFont,
		HCLook::Alpha(Entry->Count > 0 ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, 0.5f), HCLook::Appear(T, 0.3f + 0.14f * Pips, 0.25f)));
	// One square per voter in their suit colour, dropping in one after the other
	double X = RightX - CountSize.X - 12.0;
	for (int32 Index = 0; Index < Pips; ++Index)
	{
		const float A = HCLook::Appear(T, 0.3f + 0.14f * Index, 0.2f);
		const AHCPlayerState* Voter = Entry->VoterIds.IsValidIndex(Index) ? FindPlayer(GS, Entry->VoterIds[Index]) : nullptr;
		const FLinearColor Color = Voter ? HCIdentity::Color(Voter->GetIdentity()) : HCLook::BoneMuted;
		X -= 11.0;
		D.Box(FVector2D(X, CenterY - 5.5 - (1.f - A) * 6.f), FVector2D(11.0, 11.0), HCLook::Alpha(Color, A));
		X -= 4.0;
	}
	return Entry->Count;
}

void UHCMeetingWidget::PaintCard(const FHCDraw& D, const FHCPaintState& S, int32 PlayerId, int32 CardIndex) const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	const AHCPlayerState* PS = FindPlayer(GS, PlayerId);
	if (!PS)
	{
		return;
	}
	const APlayerController* PC = GetOwningPlayer();
	const AHCPlayerState* Own = PC ? PC->GetPlayerState<AHCPlayerState>() : nullptr;
	// The cards come in one after the other when the screen opens
	FHCDraw F = D;
	F.Opacity *= HCLook::Appear(static_cast<float>(FPlatformTime::Seconds() - OpenedAt), 0.12f + 0.05f * CardIndex, 0.3f);

	const EHCMeetingStage Stage = GS->GetMeetingStage();
	const bool bVoting = Stage == EHCMeetingStage::Voting;
	const bool bResults = Stage == EHCMeetingStage::Results;
	const bool bAlive = PS->IsAlive();
	const bool bOwn = PS == Own;
	const bool bCanPick = CanPickPlayer(PlayerId);
	const bool bPicked = bCanPick && bHasPick && PickedTarget == PlayerId;
	const bool bOwnVote = bVoting && bHasOwnVote && OwnVoteTarget == PlayerId && !bCanPick;
	const bool bEjected = bResults && GS->GetEjectedPlayerId() == PlayerId;
	const bool bMarked = bPicked || bOwnVote || bEjected;
	const bool bHot = bCanPick && S.bHovered;
	const FLinearColor Suit = HCLook::Alpha(HCIdentity::Color(PS->GetIdentity()), bAlive ? 1.f : 0.35f);
	const FVector2D Size = F.Size;

	// Plate: blood red when it is picked, voted for or thrown out
	F.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Plate, bAlive ? 0.92f : 0.6f));
	if (bMarked)
	{
		F.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Blood, bEjected ? 0.24f : 0.15f));
	}
	else if (bHot)
	{
		F.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Bone, 0.07f));
	}
	F.Frame(FVector2D::ZeroVector, Size, bMarked ? 2.f : 1.f, bMarked ? HCLook::Blood : bHot ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, bCanPick ? 0.5f : 0.22f));
	F.Box(FVector2D::ZeroVector, FVector2D(4.0, Size.Y), Suit);

	// Suit symbol
	const FVector2D Tile(18.0, (Size.Y - 50.0) * 0.5);
	F.Box(Tile, FVector2D(50.0, 50.0), HCLook::Alpha(HCLook::Ink, 0.8f));
	F.Frame(Tile, FVector2D(50.0, 50.0), 1.f, HCLook::Alpha(Suit, 0.55f));
	F.Symbol(PS->GetIdentity(), Tile + FVector2D(25.0, 26.0), 15.f, Suit, 2.5f);

	// Name; a line through it for the dead
	const double TextX = 84.0;
	const FSlateFontInfo NameFont = HCLook::Body(19.f, true);
	const FLinearColor NameColor = bEjected ? HCLook::Blood : bAlive ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, 0.75f);
	const FVector2D NameSize = F.Text(FHCDraw::Fit(PS->GetPlayerName(), NameFont, Size.X - TextX - 120.0), FVector2D(TextX, 12.0), NameFont, NameColor);
	if (!bAlive)
	{
		F.Box(FVector2D(TextX - 3.0, 12.0 + NameSize.Y * 0.56), FVector2D(NameSize.X + 6.0, 1.5), HCLook::Alpha(HCLook::BoneMuted, 0.8f));
	}
	if (bOwn)
	{
		const FString You = HCLook::Upper(NSLOCTEXT("HC", "MeetingYouTag", "Du"));
		const FSlateFontInfo TagFont = HCLook::Caps(10.f, 200);
		const FVector2D TagSize = FHCDraw::Measure(You, TagFont);
		const FVector2D TagPos(TextX + NameSize.X + 10.0, 12.0 + (NameSize.Y - TagSize.Y - 6.0) * 0.5);
		F.Frame(TagPos, TagSize + FVector2D(10.0, 6.0), 1.f, HCLook::Alpha(HCLook::BoneMuted, 0.7f));
		F.Text(You, TagPos + FVector2D(6.0, 3.0), TagFont, HCLook::BoneMuted);
	}

	// What they are doing
	FText Status;
	FLinearColor StatusColor = HCLook::BoneMuted;
	bool bTick = false;
	if (!bAlive)
	{
		Status = NSLOCTEXT("HC", "Dead", "tot");
		StatusColor = HCLook::Alpha(HCLook::BoneMuted, 0.7f);
	}
	else if (bEjected)
	{
		Status = NSLOCTEXT("HC", "MeetingEjected", "wird ausgeworfen");
		StatusColor = HCLook::Blood;
	}
	else if (bVoting)
	{
		bTick = PS->HasVoted();
		Status = bTick ? NSLOCTEXT("HC", "HasVoted", "hat gewählt") : NSLOCTEXT("HC", "Thinking", "überlegt ...");
		StatusColor = bTick ? HCLook::Bone : HCLook::BoneMuted;
	}
	else if (PlayerId == GS->GetMeetingCallerId())
	{
		Status = NSLOCTEXT("HC", "MeetingCalled", "hat einberufen");
	}
	const double StatusY = 47.0;
	if (bTick)
	{
		F.Check(FVector2D(TextX + 5.0, StatusY + 8.0), 4.5f, HCLook::Bone, 2.f);
	}
	F.Text(HCLook::Upper(Status), FVector2D(TextX + (bTick ? 17.0 : 0.0), StatusY), HCLook::Caps(11.f, 200), StatusColor);

	// On the right: the ballot mark, or the votes this player got
	const double RightX = Size.X - 16.0;
	const FSlateFontInfo MarkFont = HCLook::Caps(11.f, 220);
	if (bResults)
	{
		if (bAlive)
		{
			PaintVotes(F, GS, PlayerId, RightX, Size.Y * 0.5);
		}
	}
	else if (bPicked)
	{
		F.TextRight(HCLook::Upper(NSLOCTEXT("HC", "VotePicked", "ausgewählt")), RightX, 16.0, MarkFont, HCLook::Blood);
	}
	else if (bOwnVote)
	{
		F.TextRight(HCLook::Upper(NSLOCTEXT("HC", "VoteOwn", "deine Stimme")), RightX, 16.0, MarkFont, HCLook::Blood);
	}
	else if (bHot)
	{
		F.TextRight(HCLook::Upper(NSLOCTEXT("HC", "VoteFor", "WÄHLEN")), RightX, 16.0, MarkFont, HCLook::Bone);
	}
	D.Layer = F.Layer;
}

void UHCMeetingWidget::PaintSkipLine(const FHCDraw& D, const FHCPaintState& S) const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	const bool bResults = GS->GetMeetingStage() == EHCMeetingStage::Results;
	const bool bCanPick = CanPickNow();
	const bool bPicked = bCanPick && bHasPick && PickedTarget == FHCVoteTally::SkipId;
	const bool bOwnVote = !bResults && bHasOwnVote && OwnVoteTarget == FHCVoteTally::SkipId && !bCanPick;
	const bool bMarked = bPicked || bOwnVote;
	const bool bHot = bCanPick && S.bHovered;
	const FVector2D Size = D.Size;

	D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Plate, 0.8f));
	if (bMarked)
	{
		D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Blood, 0.15f));
	}
	else if (bHot)
	{
		D.Box(FVector2D::ZeroVector, Size, HCLook::Alpha(HCLook::Bone, 0.07f));
	}
	D.Frame(FVector2D::ZeroVector, Size, bMarked ? 2.f : 1.f, bMarked ? HCLook::Blood : bHot ? HCLook::Bone : HCLook::Alpha(HCLook::BoneMuted, bCanPick ? 0.5f : 0.22f));

	const FSlateFontInfo Font = HCLook::Caps(12.f, 220);
	const FText Label = bResults ? NSLOCTEXT("HC", "VoteSkipped", "Übersprungen / nicht gewählt") : NSLOCTEXT("HC", "VoteSkipLine", "Überspringen  ·  niemanden auswerfen");
	const FVector2D LabelSize = FHCDraw::Measure(HCLook::Upper(Label), Font);
	const double TextY = (Size.Y - LabelSize.Y) * 0.5;
	D.Text(HCLook::Upper(Label), FVector2D(18.0, TextY), Font, bMarked ? HCLook::Bone : bHot ? HCLook::Bone : HCLook::BoneMuted);
	const double RightX = Size.X - 16.0;
	if (bResults)
	{
		PaintVotes(D, GS, FHCVoteTally::SkipId, RightX, Size.Y * 0.5);
	}
	else if (bPicked)
	{
		D.TextRight(HCLook::Upper(NSLOCTEXT("HC", "VotePicked", "ausgewählt")), RightX, TextY, HCLook::Caps(11.f, 220), HCLook::Blood);
	}
	else if (bOwnVote)
	{
		D.TextRight(HCLook::Upper(NSLOCTEXT("HC", "VoteOwn", "deine Stimme")), RightX, TextY, HCLook::Caps(11.f, 220), HCLook::Blood);
	}
}

void UHCMeetingWidget::PaintConfirm(const FHCDraw& D, const FHCPaintState& S) const
{
	FText Label = NSLOCTEXT("HC", "VoteConfirmNone", "Stimme abgeben");
	if (bHasPick)
	{
		const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
		const AHCPlayerState* Target = PickedTarget == FHCVoteTally::SkipId ? nullptr : FindPlayer(GS, PickedTarget);
		Label = FText::Format(NSLOCTEXT("HC", "VoteConfirmFmt", "Stimme abgeben: {0}"),
			Target ? FText::FromString(Target->GetPlayerName()) : NSLOCTEXT("HC", "VoteSkipName", "Überspringen"));
	}
	HCLook::DrawButton(D, S, HCLook::Upper(Label), HCLook::Blood, bHasPick, 13.f, true);
}

void UHCMeetingWidget::PaintInfo(const FHCDraw& D) const
{
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!GS || InfoLine.IsEmpty())
	{
		return;
	}
	const double Width = D.Size.X;
	if (GS->GetMeetingStage() == EHCMeetingStage::Results)
	{
		// The verdict, once the votes have dropped in
		const FSlateFontInfo Font = HCLook::Caps(22.f, 120);
		const float A = HCLook::Appear(static_cast<float>(FPlatformTime::Seconds() - StageSince), 0.9f, 0.4f);
		D.Text(FHCDraw::Fit(HCLook::Upper(InfoLine), Font, Width), FVector2D(0.0, 0.0), Font, HCLook::Alpha(InfoColor, A));
	}
	else
	{
		const FSlateFontInfo Font = HCLook::Body(15.f);
		D.Text(FHCDraw::Fit(InfoLine.ToString(), Font, Width), FVector2D(0.0, 6.0), Font, InfoColor);
	}
}

// ---------------------------------------------------------------------------

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
	const double Now = FPlatformTime::Seconds();
	if (Stage != ShownStage)
	{
		ShownStage = Stage;
		StageSince = Now;
		StageTotal = 0.f;
		bHasPick = false;
		if (Stage != EHCMeetingStage::Results)
		{
			bHasOwnVote = false;
		}
	}
	StageTotal = FMath::Max(StageTotal, GS->GetPhaseRemainingSeconds());

	const bool bCanPick = CanPickNow();
	if (bCanPick)
	{
		// The ballot is open (again: "Bitte neu wählen" takes a vote back)
		bHasOwnVote = false;
	}
	if (bHasPick && (!bCanPick || (PickedTarget != FHCVoteTally::SkipId && !CanPickPlayer(PickedTarget))))
	{
		bHasPick = false; // the picked player died or left
	}
#if !UE_BUILD_SHIPPING
	// Development test helper (-HCMeetingPick): mark the first card that can be picked, for pictures of the ballot
	if (bCanPick && !bHasPick && FParse::Param(FCommandLine::Get(), TEXT("HCMeetingPick")))
	{
		for (const APlayerState* PS : GS->PlayerArray)
		{
			if (PS && !bHasPick && CanPickPlayer(PS->GetPlayerId()))
			{
				bHasPick = true;
				PickedTarget = PS->GetPlayerId();
			}
		}
	}
#endif
	UpdateChat(Own);
	KeepKeyboardFocus();

	InfoLine = FText::GetEmpty();
	InfoColor = HCLook::BoneMuted;
	switch (Stage)
	{
	case EHCMeetingStage::Discussion:
		InfoLine = NSLOCTEXT("HC", "InfoDiscussion", "Redet miteinander oder schreibt im Chat. Danach wird abgestimmt.");
		break;
	case EHCMeetingStage::Voting:
		if (!Own || !Own->IsAlive())
		{
			InfoLine = NSLOCTEXT("HC", "VoteGhost", "Geister stimmen nicht ab.");
		}
		else if (!bCanPick)
		{
			InfoLine = NSLOCTEXT("HC", "InfoVoted", "Stimme abgegeben. Warte auf die anderen ...");
		}
		else
		{
			// The host's lobby choice: anonymous votes are said, so nobody fears showing their hand
			InfoLine = GS->AreVotesAnonymous()
				? NSLOCTEXT("HC", "InfoVotePickAnon", "Wähle eine Karte und bestätige dann. Die Stimme ist endgültig und anonym.")
				: NSLOCTEXT("HC", "InfoVotePick", "Wähle eine Karte und bestätige dann. Die Stimme ist endgültig.");
			InfoColor = HCLook::Bone;
		}
		// A message from the server ("Bitte neu wählen") has priority for its few seconds
		if (PC && !PC->GetClientMessage().IsEmpty())
		{
			InfoLine = PC->GetClientMessage();
			InfoColor = HCLook::Blood;
		}
		break;
	case EHCMeetingStage::Results:
		InfoLine = GS->GetMeetingText();
		InfoColor = GS->GetEjectedPlayerId() != INDEX_NONE ? HCLook::Blood : HCLook::Bone;
		break;
	default:
		break;
	}

	// Hidden, not collapsed: the screen keeps its height from stage to stage
	SkipLine->SetVisibility(Stage == EHCMeetingStage::Voting || Stage == EHCMeetingStage::Results ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
	ConfirmButton->SetVisibility(bCanPick ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
}
