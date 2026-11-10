#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/** Join codes: 6 characters without the confusable 0 O 1 I L (docs/03 §7.3). Pure logic. */
struct HOWCOULDITBEYOU_API FHCJoinCode
{
	static constexpr int32 Length = 6;

	/** The 31 allowed characters. */
	static const TCHAR* Alphabet();

	static FString Make(FRandomStream& Random);

	/**
	 * Cleans user input: upper-case, removes spaces and dashes.
	 * Returns false if it contains invalid characters or has the wrong length.
	 */
	static bool Normalize(const FString& Input, FString& OutCode);

	/** "K7M4QX" -> "K7M 4QX" */
	static FString Format(const FString& Code);

	/** True for "192.168.0.5" or "192.168.0.5:7777" (direct LAN connect instead of a code). */
	static bool LooksLikeAddress(const FString& Input);
};
