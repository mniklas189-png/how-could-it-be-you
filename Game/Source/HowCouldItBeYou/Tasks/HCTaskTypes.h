#pragma once

#include "CoreMinimal.h"
#include "HCTaskTypes.generated.h"

/** How one step of a task is done (docs/01 §5). "Multi-step" = a task with more than one step. */
UENUM(BlueprintType)
enum class EHCTaskStepType : uint8
{
	/** Hold something in the task menu (hold pad, transfer, numbers); the server measures the time. */
	Hold,
	/** Press things in a given order (overlay). */
	Sequence,
	/** Small skill puzzle as overlay. */
	Minigame
};

UENUM(BlueprintType)
enum class EHCTaskCategory : uint8
{
	Common,
	Short,
	Long
};

/** Which overlay a Sequence/Minigame step shows. */
UENUM(BlueprintType)
enum class EHCTaskOverlayKind : uint8
{
	None,
	/** 3 dials that must be set to their target values. */
	Dials,
	/** Match a signal curve with two sliders. */
	Curve,
	/** Keep a temperature inside a band. */
	Temperature,
	/** Press buttons in the shown order; each press takes a moment. */
	Buttons,
	/** Press and hold one field on the screen until the step is done. */
	HoldPad,
	/** Start a data transfer and stay at the terminal until it is through. */
	Transfer,
	/** Tap the numbered fields in ascending order (ButtonCount fields); each one takes a moment. */
	Numbers,
	/** Four named work steps in a fixed order (filter change). */
	Filter
};

/** Who may see the task progress bar and when (docs/01 §12). Also controls what the server replicates. */
UENUM(BlueprintType)
enum class EHCTaskBarVisibility : uint8
{
	Always,
	MeetingOnly,
	Never
};

USTRUCT(BlueprintType)
struct FHCTaskStepDef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Task")
	EHCTaskStepType Type = EHCTaskStepType::Hold;

	/** Seconds this step takes (hold time, or minimum play time of the overlay). */
	UPROPERTY(EditAnywhere, Config, Category = "Task")
	float Duration = 3.f;

	/** Room of the station (space name of the station layout), also the "area" for spreading tasks. */
	UPROPERTY(EditAnywhere, Config, Category = "Task")
	FName Room;

	UPROPERTY(EditAnywhere, Config, Category = "Task")
	EHCTaskOverlayKind Overlay = EHCTaskOverlayKind::None;

	/** Number of buttons for Overlay = Buttons. */
	UPROPERTY(EditAnywhere, Config, Category = "Task")
	int32 ButtonCount = 3;

	/** Seconds after this step until the next step can be started (e.g. the centrifuge). */
	UPROPERTY(EditAnywhere, Config, Category = "Task")
	float WaitAfter = 0.f;

	/** What the player does here, e.g. "Probe entnehmen". */
	UPROPERTY(EditAnywhere, Config, Category = "Task")
	FText Label;
};

USTRUCT(BlueprintType)
struct FHCTaskDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Config, Category = "Task")
	FName TaskId;

	UPROPERTY(EditAnywhere, Config, Category = "Task")
	FText DisplayName;

	UPROPERTY(EditAnywhere, Config, Category = "Task")
	EHCTaskCategory Category = EHCTaskCategory::Short;

	/** Others can see an effect while this task is done (at most one such task per player). */
	UPROPERTY(EditAnywhere, Config, Category = "Task")
	bool bVisible = false;

	UPROPERTY(EditAnywhere, Config, Category = "Task")
	TArray<FHCTaskStepDef> Steps;

	/** Distinct rooms of all steps. */
	TArray<FName> GetRooms() const
	{
		TArray<FName> Rooms;
		for (const FHCTaskStepDef& Step : Steps)
		{
			Rooms.AddUnique(Step.Room);
		}
		return Rooms;
	}
};

/** One entry of the personal task list of a player, replicated only to the owner. */
USTRUCT(BlueprintType)
struct FHCTaskEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Task")
	FName TaskId;

	UPROPERTY(BlueprintReadOnly, Category = "Task")
	uint8 CurrentStep = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Task")
	uint8 NumSteps = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Task")
	bool bCompleted = false;

	/** Server time from which the current step can be started (0 = right away). */
	UPROPERTY(BlueprintReadOnly, Category = "Task")
	double StepAvailableAt = 0.0;
};

/** The built-in task set of the prototype (docs/06 M3: tasks #1, #3, #4, #5, #6, #7, #9, #11, #12 of docs/01 §5). */
struct HOWCOULDITBEYOU_API FHCTaskCatalog
{
	static TArray<FHCTaskDefinition> MakeDefaults();
};
