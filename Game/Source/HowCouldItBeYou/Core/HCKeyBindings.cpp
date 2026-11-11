#include "Core/HCKeyBindings.h"

#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* const Section = TEXT("HowCouldItBeYou.Keys");

	struct FBindingInfo
	{
		const TCHAR* Id;
		FKey Default;
	};

	const FBindingInfo& Info(EHCBinding Binding)
	{
		static const FBindingInfo Infos[HCKeys::Count] = {
			{ TEXT("Forward"), EKeys::W },
			{ TEXT("Back"), EKeys::S },
			{ TEXT("Left"), EKeys::A },
			{ TEXT("Right"), EKeys::D },
			{ TEXT("Sprint"), EKeys::LeftShift },
			{ TEXT("Interact"), EKeys::E },
			{ TEXT("Flashlight"), EKeys::F },
			{ TEXT("Kill"), EKeys::LeftMouseButton },
			{ TEXT("Shapeshift"), EKeys::Q },
			{ TEXT("TrueForm"), EKeys::R },
			{ TEXT("Map"), EKeys::M },
			{ TEXT("TaskList"), EKeys::Tab },
			{ TEXT("GhostUp"), EKeys::SpaceBar },
			{ TEXT("GhostDown"), EKeys::LeftControl },
			{ TEXT("Talk"), EKeys::V },
		};
		return Infos[FMath::Clamp(static_cast<int32>(Binding), 0, HCKeys::Count - 1)];
	}

	TArray<FKey>& Current()
	{
		static TArray<FKey> Keys;
		if (Keys.Num() == 0)
		{
			Keys.SetNum(HCKeys::Count);
			for (int32 Index = 0; Index < HCKeys::Count; ++Index)
			{
				const FBindingInfo& Entry = Info(static_cast<EHCBinding>(Index));
				FString Name;
				FKey Saved;
				if (GConfig && GConfig->GetString(Section, Entry.Id, Name, GGameUserSettingsIni) && !Name.IsEmpty())
				{
					Saved = FKey(*Name);
				}
				Keys[Index] = Saved.IsValid() && HCKeys::CanBind(Saved) ? Saved : Entry.Default;
			}
		}
		return Keys;
	}

	int32 Revision = 0;

	void Save()
	{
		if (!GConfig)
		{
			return;
		}
		const TArray<FKey>& Keys = Current();
		for (int32 Index = 0; Index < HCKeys::Count; ++Index)
		{
			GConfig->SetString(Section, Info(static_cast<EHCBinding>(Index)).Id, *Keys[Index].GetFName().ToString(), GGameUserSettingsIni);
		}
		GConfig->Flush(false, GGameUserSettingsIni);
	}
}

namespace HCKeys
{
	FKey Get(EHCBinding Binding)
	{
		return Current()[FMath::Clamp(static_cast<int32>(Binding), 0, Count - 1)];
	}

	FKey GetDefault(EHCBinding Binding)
	{
		return Info(Binding).Default;
	}

	void Set(EHCBinding Binding, const FKey& Key)
	{
		if (!CanBind(Key))
		{
			return;
		}
		TArray<FKey>& Keys = Current();
		const int32 Own = FMath::Clamp(static_cast<int32>(Binding), 0, Count - 1);
		const FKey Old = Keys[Own];
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Index != Own && Keys[Index] == Key)
			{
				Keys[Index] = Old;
			}
		}
		Keys[Own] = Key;
		Save();
		++Revision;
	}

	void ResetAll()
	{
		TArray<FKey>& Keys = Current();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Keys[Index] = Info(static_cast<EHCBinding>(Index)).Default;
		}
		Save();
		++Revision;
	}

	bool CanBind(const FKey& Key)
	{
		return Key.IsValid() && !Key.IsGamepadKey() && !Key.IsTouch() && !Key.IsAxis1D() && !Key.IsAxis2D() && !Key.IsAxis3D()
			&& Key != EKeys::Escape && Key != EKeys::F1 && Key != EKeys::AnyKey && Key != EKeys::Invalid;
	}

	int32 GetRevision()
	{
		return Revision;
	}

	FText Label(EHCBinding Binding)
	{
		switch (Binding)
		{
		case EHCBinding::Forward: return NSLOCTEXT("HC", "KeyForward", "Vorwärts");
		case EHCBinding::Back: return NSLOCTEXT("HC", "KeyBack", "Rückwärts");
		case EHCBinding::Left: return NSLOCTEXT("HC", "KeyLeft", "Links");
		case EHCBinding::Right: return NSLOCTEXT("HC", "KeyRight", "Rechts");
		case EHCBinding::Sprint: return NSLOCTEXT("HC", "KeySprint", "Rennen");
		case EHCBinding::Interact: return NSLOCTEXT("HC", "KeyInteract", "Benutzen / Melden");
		case EHCBinding::Flashlight: return NSLOCTEXT("HC", "KeyFlashlight", "Taschenlampe");
		case EHCBinding::Kill: return NSLOCTEXT("HC", "KeyKill", "Töten (Wandler)");
		case EHCBinding::Shapeshift: return NSLOCTEXT("HC", "KeyShapeshift", "Verwandeln (Wandler)");
		case EHCBinding::TrueForm: return NSLOCTEXT("HC", "KeyTrueForm", "Wahre Gestalt (Wandler)");
		case EHCBinding::Map: return NSLOCTEXT("HC", "KeyMap", "Stationskarte");
		case EHCBinding::TaskList: return NSLOCTEXT("HC", "KeyTaskList", "Aufgabenliste / Lobby-Menü");
		case EHCBinding::GhostUp: return NSLOCTEXT("HC", "KeyGhostUp", "Als Geist hoch");
		case EHCBinding::GhostDown: return NSLOCTEXT("HC", "KeyGhostDown", "Als Geist runter");
		case EHCBinding::Talk: return NSLOCTEXT("HC", "KeyTalk", "Sprechen (Sprach-Chat)");
		default: return FText::GetEmpty();
		}
	}

	FText LongName(const FKey& Key)
	{
		return Key.GetDisplayName(true);
	}

	FText ShortName(const FKey& Key)
	{
		return Key.GetDisplayName(false);
	}

	FText ShortName(EHCBinding Binding)
	{
		return ShortName(Get(Binding));
	}

	bool Is(EHCBinding Binding, const FKey& Key)
	{
		return Get(Binding) == Key;
	}
}
