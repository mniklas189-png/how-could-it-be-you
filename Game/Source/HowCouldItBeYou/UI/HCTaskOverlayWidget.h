#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Tasks/HCTaskTypes.h"
#include "HCTaskOverlayWidget.generated.h"

class AHCTaskStation;
class UHCPaintBox;
class UTextBlock;
class UVerticalBox;
class UWidget;
struct FHCDraw;
struct FHCPaintState;

/**
 * The task menu: a framed device screen in the middle of the view, never the whole screen (docs/01 §5), in the
 * game's own look (UI/HCLook.h). Its instruments are painted: valves with a zone to hit, a signal scope, a
 * temperature gauge, tiles that fill while they are read, a hold ring, transfer packets.
 * All kinds share one rule: the progress line fills over the step duration, but only while the puzzle is
 * "in the green". So a step never finishes faster than its duration, which is what the server checks.
 * Esc or a movement key cancels at once; the edge of the screen stays visible.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCTaskOverlayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Call right after CreateWidget, before the widget is shown. */
	void Setup(AHCTaskStation* InStation);

	bool IsFinished() const { return bFinished; }
	void Cancel();
	/** A tile of the menu was pressed. */
	void PressButton(int32 Index);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	void HandleStart();

	void BuildDials();
	void BuildCurve();
	void BuildTemperature();
	void BuildButtons(int32 Count);
	void BuildNumbers(int32 Count);
	void BuildFilter();
	void BuildHoldPad();
	void BuildTransfer();
	void BuildWires(int32 Count);
	void BuildSwitches(int32 Count);
	void BuildMemory(int32 Count);
	/** Count tiles in rows; Order = the sequence they must be pressed in. */
	void BuildButtonGrid(const TArray<FText>& Labels, int32 Columns, bool bShuffleOrder, bool bShufflePositions, float FontSize, float TileHeight);
	/** A slider for Values[Index]; with bShowTarget the zone to hit is drawn on its track. */
	void AddSliderRow(const FText& RowLabel, int32 Index, bool bShowTarget);
	void SetSlider(int32 Index, const FVector2D& Local, const FVector2D& Size);

	bool TickDials();
	bool TickCurve();
	bool TickTemperature(float DeltaTime);
	void TickButtons(float DeltaTime);
	void TickHoldPad(float DeltaTime);
	void TickTransfer(float DeltaTime);
	bool TickWires();
	bool TickSwitches();
	/** Mouse down on the cable panel: picks a cable on the left, or plugs the picked one into a socket on the right. */
	void PressWires(const FVector2D& Local, const FVector2D& Size);
	/** Mouse up: a cable dragged onto a socket is plugged in. */
	void ReleaseWires();
	void PlugWire(int32 SocketRow);
	void PressSwitch(const FVector2D& Local, const FVector2D& Size);
	void TickMemory();
	void PressMemory(const FVector2D& Local, const FVector2D& Size);
	/** Shows the order of the running round (again); with bNewOrder a new one is rolled first. */
	void StartMemoryRound(bool bNewOrder);
	int32 MemoryLength() const;
	/** The player has been at it for a long time: the menu helps (a tip line, marks, wider zones). */
	bool IsHintActive() const;
	FText GetHintText() const;
	float SliderTolerance() const;
	void Complete();
	float RollTarget(float Avoid) const;
	/** This tile was already read in the running sequence. */
	bool IsTileDone(int32 Index) const;
	bool IsSliderGood(int32 Index) const;

	void PaintHeader(const FHCDraw& D, const FString& Title, const FString& Room) const;
	void PaintProgress(const FHCDraw& D) const;
	void PaintSlider(const FHCDraw& D, const FHCPaintState& S, int32 Index, const FString& RowLabel, bool bShowTarget) const;
	void PaintScope(const FHCDraw& D) const;
	void PaintGauge(const FHCDraw& D) const;
	void PaintTile(const FHCDraw& D, const FHCPaintState& S, int32 Index, float FontSize) const;
	void PaintOrder(const FHCDraw& D) const;
	void PaintHoldPad(const FHCDraw& D, const FHCPaintState& S) const;
	void PaintTransfer(const FHCDraw& D, const FHCPaintState& S) const;
	void PaintWires(const FHCDraw& D, const FHCPaintState& S) const;
	void PaintSwitches(const FHCDraw& D, const FHCPaintState& S) const;
	void PaintMemory(const FHCDraw& D, const FHCPaintState& S) const;

	TWeakObjectPtr<AHCTaskStation> Station;
	EHCTaskOverlayKind Kind = EHCTaskOverlayKind::None;
	float Duration = 6.f;
	float Progress = 0.f;
	bool bFinished = false;
	/** The progress grew in the last tick: the puzzle is in the green. */
	bool bAdvancing = false;
	/** Development test helper (-HCAutoSolve): the puzzle keeps itself in the green. */
	bool bAutoSolve = false;
	/** For pictures of the test bots: -HCAutoSolveDelay=<s> leaves the puzzle alone at first, -HCHintAfter=<s> shows the tip early. */
	bool bAutoSolveWanted = false;
	float AutoSolveDelay = 0.f;
	float HintAfterOverride = -1.f;
	double OpenedAt = 0.0;

	UPROPERTY(Transient) TObjectPtr<UWidget> FramePanel;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ContentBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	/** The tip; hidden until IsHintActive. */
	UPROPERTY(Transient) TObjectPtr<UTextBlock> HintText;
	bool bHintShown = false;

	// Dials and curve: slider values 0..1 and where they have to be
	TArray<float> Values;
	TArray<float> Targets;
	int32 ShiftsDone = 0;

	// Temperature
	float Temperature = 0.8f;
	/** Space bar: cools, and holds the hold pad. */
	bool bCoolKeyDown = false;
	UPROPERTY(Transient) TObjectPtr<UHCPaintBox> CoolButton;

	// Tiles in order
	TArray<int32> Order;
	int32 OrderIndex = 0;
	/** < 0 = waiting for the next press, else 0..1 while the pressed tile is being read. */
	float SegmentProgress = -1.f;
	TArray<FText> ButtonLabels;
	/** Status lines of the tile menus, e.g. "Kiste {0} wird gescannt ...". */
	FText RunningFormat;
	FText NextText;
	FText WrongText;
	/** The tile that was pressed out of turn flashes red for a moment. */
	int32 WrongIndex = INDEX_NONE;
	double WrongAt = 0.0;

	// Hold pad and transfer
	UPROPERTY(Transient) TObjectPtr<UHCPaintBox> HoldPad;
	bool bTransferRunning = false;

	// Cables: plugs on the left, sockets on the right, both in their own shuffled order
	/** WirePlugs[row on the left] = the cable that starts there. */
	TArray<int32> WirePlugs;
	/** WireSockets[row on the right] = the cable that belongs there. */
	TArray<int32> WireSockets;
	TArray<bool> WireConnected;
	int32 WireSelected = INDEX_NONE;
	bool bWireDragging = false;
	FVector2D WireDragPos = FVector2D::ZeroVector;
	FVector2D WirePanelSize = FVector2D::ZeroVector;

	// Switches: how they stand and how the lamps want them
	TArray<bool> SwitchOn;
	TArray<bool> SwitchTarget;

	// Memory: the order of the running round, and how far the player has repeated it
	TArray<int32> MemoryOrder;
	int32 MemoryTiles = 6;
	int32 MemoryRound = 0;
	int32 MemoryInput = 0;
	bool bMemoryShowing = false;
	double MemoryShowStart = 0.0;
	/** The field pressed last lights up for a moment. */
	int32 MemoryPressed = INDEX_NONE;
	double MemoryPressedAt = 0.0;
};
