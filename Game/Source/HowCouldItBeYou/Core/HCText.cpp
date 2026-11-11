#include "Core/HCText.h"

namespace HCText
{
	FText Room(FName SpaceId)
	{
		const FString Id = SpaceId.ToString();
		if (Id == TEXT("Kommandozentrale")) return NSLOCTEXT("HC", "RoomCommand", "Kommandozentrale");
		if (Id == TEXT("Quartiere")) return NSLOCTEXT("HC", "RoomQuarters", "Quartiere");
		if (Id == TEXT("Kommunikation")) return NSLOCTEXT("HC", "RoomComms", "Kommunikation");
		if (Id == TEXT("Hangar")) return NSLOCTEXT("HC", "RoomHangar", "Hangar");
		if (Id == TEXT("Lager")) return NSLOCTEXT("HC", "RoomStorage", "Lager");
		if (Id == TEXT("Medbay")) return NSLOCTEXT("HC", "RoomMedbay", "Medbay");
		if (Id == TEXT("BioLabor")) return NSLOCTEXT("HC", "RoomBioLab", "Bio-Labor");
		if (Id == TEXT("Atrium")) return NSLOCTEXT("HC", "RoomAtrium", "Atrium");
		if (Id == TEXT("Sicherheit")) return NSLOCTEXT("HC", "RoomSecurity", "Sicherheit");
		if (Id == TEXT("Reaktor")) return NSLOCTEXT("HC", "RoomReactor", "Reaktor");
		// The corridors are called "GangNord", "SpeicheWest", "KommandoGangOst" ...
		if (Id.Contains(TEXT("Gang")) || Id.Contains(TEXT("Speiche")))
		{
			return NSLOCTEXT("HC", "RoomCorridor", "Gang");
		}
		return FText::FromName(SpaceId);
	}

	FText RoomShort(FName SpaceId)
	{
		const FString Id = SpaceId.ToString();
		if (Id == TEXT("Kommandozentrale")) return NSLOCTEXT("HC", "RoomCommandShort", "Kommando");
		if (Id == TEXT("Kommunikation")) return NSLOCTEXT("HC", "RoomCommsShort", "Funk");
		return Room(SpaceId);
	}
}
