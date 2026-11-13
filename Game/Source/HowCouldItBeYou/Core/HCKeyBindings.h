#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/** Every action whose key the player can change in the settings menu (page "Steuerung"). */
enum class EHCBinding : uint8
{
	Forward,
	Back,
	Left,
	Right,
	Sprint,
	Interact,
	Flashlight,
	Kill,
	Shapeshift,
	TrueForm,
	Map,
	TaskList,
	GhostUp,
	GhostDown,
	GhostJump,
	Talk,
	Count
};

/**
 * The player's own key for each action. Stored by key name in GameUserSettings.ini, section HowCouldItBeYou.Keys.
 * Esc and F1 always open the pause menu and cannot be given to anything else; the mouse always looks around.
 */
namespace HCKeys
{
	constexpr int32 Count = static_cast<int32>(EHCBinding::Count);

	HOWCOULDITBEYOU_API FKey Get(EHCBinding Binding);
	HOWCOULDITBEYOU_API FKey GetDefault(EHCBinding Binding);
	/** Gives the key to this action. An action that had the key before gets this action's old key (they swap). */
	HOWCOULDITBEYOU_API void Set(EHCBinding Binding, const FKey& Key);
	HOWCOULDITBEYOU_API void ResetAll();
	/** Keys that can be bound: keyboard keys and mouse buttons, not Esc/F1, not the mouse axes, no gamepad. */
	HOWCOULDITBEYOU_API bool CanBind(const FKey& Key);
	/** Goes up with every change, so that systems notice it without asking for every key. */
	HOWCOULDITBEYOU_API int32 GetRevision();

	/** Name of the action, as the settings menu shows it. */
	HOWCOULDITBEYOU_API FText Label(EHCBinding Binding);
	/** Key name for the settings menu ("Linke Maustaste"). */
	HOWCOULDITBEYOU_API FText LongName(const FKey& Key);
	/** Key name for hints on the screen ("LMB", "E"). */
	HOWCOULDITBEYOU_API FText ShortName(const FKey& Key);
	HOWCOULDITBEYOU_API FText ShortName(EHCBinding Binding);
	/** Is this the key of that action? */
	HOWCOULDITBEYOU_API bool Is(EHCBinding Binding, const FKey& Key);
}
