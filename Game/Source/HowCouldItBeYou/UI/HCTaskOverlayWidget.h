#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Tasks/HCTaskTypes.h"
#include "HCTaskOverlayWidget.generated.h"

class AHCTaskStation;
class UButton;
class UProgressBar;
class USizeBox;
class USlider;
class UTextBlock;
class UVerticalBox;

class UHCTaskOverlayWidget;

/** Carries the index of one button of a task menu (UButton::OnClicked has no parameters). */
UCLASS()
class HOWCOULDITBEYOU_API UHCTaskClickProxy : public UObject
{
	GENERATED_BODY()

public:
	int32 Index = 0;
	TWeakObjectPtr<UHCTaskOverlayWidget> Owner;

	UFUNCTION()
	void HandleClicked();
};

/**
 * The task menu: a framed device screen in the middle of the view, never the whole screen (docs/01 §5).
 * Semi-transparent overlay in the middle of the screen for Sequence and Minigame steps (docs/01 §5).
 * All kinds share one rule: a progress bar fills over the step duration, but only while the puzzle is
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
	/** A button of the menu was pressed (also used by the click proxies). */
	void PressButton(int32 Index);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnKeyUp(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	UFUNCTION() void HandleStart();
	UFUNCTION() void HandleCancel();

	void BuildDials();
	void BuildCurve();
	void BuildTemperature();
	void BuildButtons(int32 Count);
	void BuildNumbers(int32 Count);
	void BuildFilter();
	void BuildHoldPad();
	void BuildTransfer();
	/** Count buttons in rows; Order = the sequence they must be pressed in. */
	void BuildButtonGrid(const TArray<FText>& Labels, int32 Columns, bool bShuffleOrder, bool bShufflePositions, int32 FontSize, float TileHeight);
	void TickHoldPad(float DeltaTime);
	void TickTransfer(float DeltaTime);
	USlider* AddSliderRow(const FText& RowLabel, UTextBlock*& OutValueText);

	bool TickDials();
	bool TickCurve();
	bool TickTemperature(float DeltaTime);
	void TickButtons(float DeltaTime);
	void Complete();
	float RollTarget(float Avoid) const;

	TWeakObjectPtr<AHCTaskStation> Station;
	EHCTaskOverlayKind Kind = EHCTaskOverlayKind::None;
	float Duration = 6.f;
	float Progress = 0.f;
	bool bFinished = false;
	/** Development test helper (-HCAutoSolve): the puzzle keeps itself in the green. */
	bool bAutoSolve = false;

	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ContentBox;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> ProgressBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;

	// Dials and curve
	UPROPERTY(Transient) TArray<TObjectPtr<USlider>> Sliders;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> SliderTexts;
	TArray<float> Targets;
	int32 ShiftsDone = 0;
	UPROPERTY(Transient) TObjectPtr<USizeBox> CurveArea;

	// Temperature
	float Temperature = 0.8f;
	bool bCoolKeyDown = false;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> TemperatureBar;
	UPROPERTY(Transient) TObjectPtr<UButton> CoolButton;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TemperatureText;

	// Buttons in order
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> OrderButtons;
	TArray<int32> Order;
	int32 OrderIndex = 0;
	/** < 0 = waiting for the next press, else 0..1 while the pressed item is being scanned. */
	float SegmentProgress = -1.f;
	UPROPERTY(Transient) TArray<TObjectPtr<UHCTaskClickProxy>> ClickProxies;
	TArray<FText> ButtonLabels;
	/** Status lines of the button menus, e.g. "Kiste {0} wird gescannt ...". */
	FText RunningFormat;
	FText NextText;
	FText WrongText;
	// Hold pad and transfer
	UPROPERTY(Transient) TObjectPtr<UButton> HoldButton;
	UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
	bool bTransferRunning = false;
};
