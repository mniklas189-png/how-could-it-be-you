#include "Meeting/HCChat.h"

namespace
{
	/** Direction overrides and zero-width characters: invisible, but they can turn or hide parts of a line. */
	bool IsInvisibleMark(TCHAR C)
	{
		return (C >= 0x200B && C <= 0x200F) || (C >= 0x202A && C <= 0x202E) || (C >= 0x2060 && C <= 0x2069) || C == 0xFEFF;
	}

	bool IsSpaceLike(TCHAR C)
	{
		return C < 32 || (C >= 0x7F && C <= 0x9F) || C == 0x2028 || C == 0x2029 || FChar::IsWhitespace(C);
	}
}

FString HCChat::Sanitize(const FString& Raw)
{
	FString Out;
	Out.Reserve(FMath::Min(Raw.Len(), MaxLength));
	bool bPendingSpace = false;
	for (const TCHAR C : Raw)
	{
		if (IsInvisibleMark(C))
		{
			continue;
		}
		if (IsSpaceLike(C))
		{
			bPendingSpace = !Out.IsEmpty();
			continue;
		}
		const int32 Needed = bPendingSpace ? 2 : 1;
		if (Out.Len() + Needed > MaxLength)
		{
			break;
		}
		if (bPendingSpace)
		{
			Out.AppendChar(TEXT(' '));
			bPendingSpace = false;
		}
		Out.AppendChar(C);
	}
	// The cut must not leave half of a character that needs two UTF-16 units (emoji)
	if (!Out.IsEmpty() && Out[Out.Len() - 1] >= 0xD800 && Out[Out.Len() - 1] <= 0xDBFF)
	{
		Out.LeftChopInline(1);
		Out.TrimEndInline();
	}
	return Out;
}
