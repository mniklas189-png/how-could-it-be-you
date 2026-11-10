#include "UI/HCShapeshiftWidget.h"

#include "Abilities/HCShapeshiftComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Characters/HCCharacter.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Core/HCGameState.h"
#include "Core/HCPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "UI/HCHUD.h"
#include "UI/HCUIStyle.h"

void UHCShapePickProxy::HandleClicked()
{
	if (UHCShapeshiftWidget* Widget = Owner.Get())
	{
		Widget->Pick(TargetPlayerId);
	}
}

void UHCShapeshiftWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	UWidgetTree* Tree = WidgetTree;
	UCanvasPanel* Root = HCUI::MakeRoot(Tree, FLinearColor(0.02f, 0.f, 0.f, 0.45f));
	UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	UTextBlock* Title = HCUI::Text(Tree, NSLOCTEXT("HC", "ShiftTitle", "IN WEN VERWANDELN?"), 26, HCUI::AccentRed, true);
	Title->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, Title, 12.f, HAlign_Fill);

	UBorder* Panel = HCUI::Panel(Tree, HCUI::PanelColor, FMargin(20.f, 16.f));
	PlayerList = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Panel->SetContent(PlayerList);
	HCUI::AddRow(Box, Panel, 10.f);

	UTextBlock* Hint = HCUI::Text(Tree, NSLOCTEXT("HC", "ShiftHint", "Klick oder Zifferntaste wählt · Q oder Esc bricht ab"), 13, HCUI::MutedColor);
	Hint->SetJustification(ETextJustify::Center);
	HCUI::AddRow(Box, Hint, 0.f, HAlign_Fill);

	HCUI::Place(Root, Box, FVector2D(0.5f, 0.5f), FVector2D(0.5f, 0.5f), 420.f);
}

void UHCShapeshiftWidget::Close()
{
	const APlayerController* PC = GetOwningPlayer();
	if (AHCHUD* HUD = PC ? PC->GetHUD<AHCHUD>() : nullptr)
	{
		HUD->CloseShapeshiftMenu();
	}
}

bool UHCShapeshiftWidget::Pick(int32 TargetPlayerId)
{
	const APlayerController* PC = GetOwningPlayer();
	const AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	if (!Character || !ShownIds.Contains(TargetPlayerId))
	{
		return false;
	}
	Character->GetShapeshift()->ServerRequestShapeshift(TargetPlayerId);
	Close();
	return true;
}

FReply UHCShapeshiftWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Q || Key == EKeys::Escape || Key == EKeys::F1)
	{
		Close();
		return FReply::Handled();
	}
	static const FKey Numbers[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Numbers); ++Index)
	{
		if (Key == Numbers[Index] && ShownIds.IsValidIndex(Index))
		{
			Pick(ShownIds[Index]);
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UHCShapeshiftWidget::Rebuild()
{
	PlayerList->ClearChildren();
	Proxies.Reset();
	ShownIds.Reset();
	const AHCGameState* GS = GetWorld()->GetGameState<AHCGameState>();
	const APlayerController* PC = GetOwningPlayer();
	const APlayerState* Own = PC ? PC->PlayerState.Get() : nullptr;

	// Only living players can be copied; the dead and the ejected are not offered (docs/01 §4.2)
	TArray<const AHCPlayerState*> Players;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS);
		if (HCPS && HCPS != Own && HCPS->IsAlive())
		{
			Players.Add(HCPS);
		}
	}
	Players.Sort([](const AHCPlayerState& A, const AHCPlayerState& B) { return A.GetIdentity() < B.GetIdentity(); });
	for (const AHCPlayerState* PS : Players)
	{
		ShownIds.Add(PS->GetPlayerId());
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HCUI::AddColumn(Row, HCUI::Text(WidgetTree, FText::AsNumber(ShownIds.Num()), 14, HCUI::MutedColor), 10.f);
		HCUI::AddColumn(Row, HCUI::Swatch(WidgetTree, HCIdentity::Color(PS->GetIdentity()), 22.f), 12.f);
		UButton* PickButton = HCUI::Button(WidgetTree, FText::FromString(PS->GetPlayerName()));
		UHCShapePickProxy* Proxy = NewObject<UHCShapePickProxy>(this);
		Proxy->TargetPlayerId = PS->GetPlayerId();
		Proxy->Owner = this;
		Proxies.Add(Proxy);
		PickButton->OnClicked.AddDynamic(Proxy, &UHCShapePickProxy::HandleClicked);
		HCUI::AddColumn(Row, PickButton, 0.f, true);
		HCUI::AddRow(PlayerList, Row, 8.f);
	}
	if (Players.IsEmpty())
	{
		HCUI::AddRow(PlayerList, HCUI::Text(WidgetTree, NSLOCTEXT("HC", "ShiftNobody", "Niemand da, den du kopieren könntest."), 15, HCUI::MutedColor), 0.f);
	}
}

void UHCShapeshiftWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const AHCGameState* GS = GetWorld() ? GetWorld()->GetGameState<AHCGameState>() : nullptr;
	if (!GS)
	{
		return;
	}
	uint32 Signature = 17;
	for (const APlayerState* PS : GS->PlayerArray)
	{
		if (const AHCPlayerState* HCPS = Cast<AHCPlayerState>(PS))
		{
			Signature = HashCombine(Signature, GetTypeHash(HCPS->GetPlayerId() * 2 + (HCPS->IsAlive() ? 1 : 0)));
			Signature = HashCombine(Signature, GetTypeHash(HCPS->GetPlayerName()));
		}
	}
	if (Signature != ShownSignature)
	{
		ShownSignature = Signature;
		Rebuild();
	}
	// The menu is only for a Wandler who could shapeshift right now
	const APlayerController* PC = GetOwningPlayer();
	const AHCCharacter* Character = PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	if (!Character || Character->IsKillLocked() || Character->GetShapeshift()->GetState() != EHCShapeshiftState::Ready)
	{
		Close();
	}
}
