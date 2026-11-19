#pragma once

#include "CoreMinimal.h"

/**
 * Which hand holds the flashlight. A few players (Flo, Flo_MS, FloMS, Finn ...) hold it in the left hand.
 * Decided by the name the figure shows, so a Wandler disguised as such a player holds it the same way.
 */
namespace HCHandedness
{
	/** Letters only, lower case, doubled letters merged: "Flo_MS" -> "floms", "Finn07" -> "fin", "FLOOO" -> "flo". */
	inline FString NameKey(const FString& Name)
	{
		FString Key;
		for (const TCHAR Char : Name)
		{
			if (!FChar::IsAlpha(Char))
			{
				continue;
			}
			const TCHAR Lower = FChar::ToLower(Char);
			if (Key.IsEmpty() || Key[Key.Len() - 1] != Lower)
			{
				Key.AppendChar(Lower);
			}
		}
		return Key;
	}

	/** True for names that start like Flo or Finn (flo, flo_ms, floms, Florian, finn, Fin, Finn_2 ...). */
	inline bool IsLeftHandedName(const FString& Name)
	{
		const FString Key = NameKey(Name);
		return Key.StartsWith(TEXT("flo"), ESearchCase::CaseSensitive) || Key.StartsWith(TEXT("fin"), ESearchCase::CaseSensitive);
	}
}
