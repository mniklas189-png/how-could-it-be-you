#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/HCPlanShape.h"
#include "HCStationMapWidget.generated.h"

class AHCStationBuilder;
struct FHCDraw;
struct FHCPaintState;

/** What the mouse can be on in the station map. */
enum class EHCMapItem : uint8
{
	None,
	/** Row of a power sector (Index = sector). */
	Sector,
	/** "Cut power" of a sector (Index = sector). */
	CutPower,
	/** A door that can be locked right now, in the list or on the plan (Id = door). */
	Door,
	/** The same door while it cannot be locked: the mouse only shows where it is. */
	DoorLook,
	/** One of your own tasks, in the list or on the plan (Index = entry in the task list). */
	Task
};

struct FHCMapHit
{
	EHCMapItem Item = EHCMapItem::None;
	int32 Index = INDEX_NONE;
	FName Id;

	bool operator==(const FHCMapHit& Other) const { return Item == Other.Item && Index == Other.Index && Id == Other.Id; }
	bool IsClickable() const { return Item == EHCMapItem::CutPower || Item == EHCMapItem::Door; }
};

/**
 * Station map (M): the plan of the rooms with your own position and your own open tasks - no other players - and
 * next to it the power of the sectors and your task list (docs/02 §11.1). The Wandler sabotages from this very
 * screen, so from the outside sabotaging looks like reading the map.
 * The whole screen is one painted control in the game's look (UI/HCLook), like the camera monitors and the admin plan.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCStationMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Sends the request and closes the map. False if this player is not the Wandler. Also used by the test bots. */
	bool RequestSabotage(uint8 SabotageType, FName TargetId);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	bool IsOwnerMonster() const;
	void Close();
	const AHCStationBuilder* FindBuilder() const;

	/** Everything on the screen with its place: built the same way for painting and for clicks. */
	struct FLayoutItem
	{
		FHCMapHit Hit;
		FBox2D Rect;
	};
	struct FScreen;
	void BuildScreen(const FVector2D& Size, FScreen& Out) const;
	FHCMapHit HitTest(const FVector2D& Local, const FVector2D& Size) const;

	void PaintMap(const FHCDraw& D, const FHCPaintState& S) const;
	void PaintPlan(const FHCDraw& D, const FScreen& Screen, const FHCMapHit& Hot, double Time) const;
	void PaintPanel(const FHCDraw& D, const FHCPaintState& S, const FScreen& Screen, const FHCMapHit& Hot) const;

	mutable TWeakObjectPtr<const AHCStationBuilder> Builder;
	/** Floors, walls and doors of the map, worked out once. */
	mutable FHCPlanShape Shape;
	/** What the mouse was on when it was last painted (for the hand cursor). */
	mutable FHCMapHit HotItem;
	/** Where the left button went down, and where the mouse is while it is held. */
	FHCMapHit PressedItem;
	FVector2D PressMouse = FVector2D::ZeroVector;
	double OpenedAt = 0.0;
};
