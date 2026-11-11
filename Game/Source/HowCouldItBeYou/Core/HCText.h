#pragma once

#include "CoreMinimal.h"

/**
 * Words the player reads that come from ids in the game data (rooms of the station layout). The ids stay German and
 * never change; what the player reads follows the language (settings menu).
 */
namespace HCText
{
	/** "Bio-Labor" for the id "BioLabor"; every corridor is "Gang". */
	HOWCOULDITBEYOU_API FText Room(FName SpaceId);
	/** Short form where the room is small on a plan ("Funk" for Kommunikation). */
	HOWCOULDITBEYOU_API FText RoomShort(FName SpaceId);
}
