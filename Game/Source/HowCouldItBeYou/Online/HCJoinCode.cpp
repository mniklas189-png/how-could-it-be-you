#include "Online/HCJoinCode.h"

#include "Math/RandomStream.h"

const TCHAR* FHCJoinCode::Alphabet()
{
	return TEXT("ABCDEFGHJKMNPQRSTUVWXYZ23456789");
}

FString FHCJoinCode::Make(FRandomStream& Random)
{
	const FString Chars = Alphabet();
	FString Code;
	for (int32 i = 0; i < Length; ++i)
	{
		Code.AppendChar(Chars[Random.RandRange(0, Chars.Len() - 1)]);
	}
	return Code;
}

bool FHCJoinCode::Normalize(const FString& Input, FString& OutCode)
{
	const FString Chars = Alphabet();
	OutCode.Reset();
	for (const TCHAR C : Input.ToUpper())
	{
		if (C == TEXT(' ') || C == TEXT('-') || C == TEXT('\t'))
		{
			continue;
		}
		int32 Index;
		if (!Chars.FindChar(C, Index))
		{
			return false;
		}
		OutCode.AppendChar(C);
	}
	return OutCode.Len() == Length;
}

FString FHCJoinCode::Format(const FString& Code)
{
	return Code.Len() == Length ? Code.Left(3) + TEXT(" ") + Code.Right(3) : Code;
}

bool FHCJoinCode::LooksLikeAddress(const FString& Input)
{
	const FString Trimmed = Input.TrimStartAndEnd();
	FString Host = Trimmed;
	FString Port;
	FString Left, Right;
	if (Trimmed.Split(TEXT(":"), &Left, &Right))
	{
		Host = Left;
		Port = Right;
		if (Host.IsEmpty() || Port.IsEmpty())
		{
			return false;
		}
	}
	if (Host.Equals(TEXT("localhost"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	TArray<FString> Parts;
	Host.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 4)
	{
		return false;
	}
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty() || Part.Len() > 3 || !Part.IsNumeric() || FCString::Atoi(*Part) > 255)
		{
			return false;
		}
	}
	return Port.IsEmpty() || (Port.IsNumeric() && FCString::Atoi(*Port) > 0 && FCString::Atoi(*Port) < 65536);
}
