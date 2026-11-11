#include "Core/HCInputConfig.h"

#include "Core/HCKeyBindings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(Outer, Name);
		Action->ValueType = Type;
		return Action;
	}

	void AddNegate(UObject* Outer, FEnhancedActionKeyMapping& Mapping, bool bX, bool bY)
	{
		UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(Outer);
		Negate->bX = bX;
		Negate->bY = bY;
		Negate->bZ = false;
		Mapping.Modifiers.Add(Negate);
	}

	void AddSwizzleYX(UObject* Outer, FEnhancedActionKeyMapping& Mapping)
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Outer);
		Swizzle->Order = EInputAxisSwizzle::YXZ;
		Mapping.Modifiers.Add(Swizzle);
	}
}

UHCInputConfig* UHCInputConfig::Create(UObject* Outer)
{
	UHCInputConfig* Config = NewObject<UHCInputConfig>(Outer, TEXT("HCInputConfig"));

	Config->Move = MakeAction(Config, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	Config->Look = MakeAction(Config, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	Config->Sprint = MakeAction(Config, TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	Config->Flashlight = MakeAction(Config, TEXT("IA_Flashlight"), EInputActionValueType::Boolean);
	Config->Interact = MakeAction(Config, TEXT("IA_Interact"), EInputActionValueType::Boolean);
	Config->Kill = MakeAction(Config, TEXT("IA_Kill"), EInputActionValueType::Boolean);
	Config->Shapeshift = MakeAction(Config, TEXT("IA_Shapeshift"), EInputActionValueType::Boolean);
	Config->Map = MakeAction(Config, TEXT("IA_Map"), EInputActionValueType::Boolean);
	Config->TrueForm = MakeAction(Config, TEXT("IA_TrueForm"), EInputActionValueType::Boolean);
	Config->GhostUp = MakeAction(Config, TEXT("IA_GhostUp"), EInputActionValueType::Boolean);
	Config->GhostDown = MakeAction(Config, TEXT("IA_GhostDown"), EInputActionValueType::Boolean);
	Config->Pause = MakeAction(Config, TEXT("IA_Pause"), EInputActionValueType::Boolean);
	Config->LobbyMenu = MakeAction(Config, TEXT("IA_TaskList"), EInputActionValueType::Boolean);

	Config->DefaultContext = NewObject<UInputMappingContext>(Config, TEXT("IMC_Default"));
	Config->RebuildMappings();
	return Config;
}

void UHCInputConfig::RebuildMappings()
{
	UInputMappingContext* Ctx = DefaultContext;
	Ctx->UnmapAll();
	AppliedRevision = HCKeys::GetRevision();

	// Move: X = right, Y = forward
	{
		FEnhancedActionKeyMapping& Forward = Ctx->MapKey(Move, HCKeys::Get(EHCBinding::Forward));
		AddSwizzleYX(Ctx, Forward);
		FEnhancedActionKeyMapping& Back = Ctx->MapKey(Move, HCKeys::Get(EHCBinding::Back));
		AddSwizzleYX(Ctx, Back);
		AddNegate(Ctx, Back, true, true);
		Ctx->MapKey(Move, HCKeys::Get(EHCBinding::Right));
		FEnhancedActionKeyMapping& Left = Ctx->MapKey(Move, HCKeys::Get(EHCBinding::Left));
		AddNegate(Ctx, Left, true, false);
	}

	// Look: mouse, Y negated so moving the mouse up looks up
	{
		FEnhancedActionKeyMapping& Mouse = Ctx->MapKey(Look, EKeys::Mouse2D);
		AddNegate(Ctx, Mouse, false, true);
	}

	// The player's own keys (settings menu, page "Steuerung")
	Ctx->MapKey(Sprint, HCKeys::Get(EHCBinding::Sprint));
	Ctx->MapKey(Flashlight, HCKeys::Get(EHCBinding::Flashlight));
	Ctx->MapKey(Interact, HCKeys::Get(EHCBinding::Interact));
	Ctx->MapKey(Kill, HCKeys::Get(EHCBinding::Kill));
	Ctx->MapKey(Shapeshift, HCKeys::Get(EHCBinding::Shapeshift));
	Ctx->MapKey(Map, HCKeys::Get(EHCBinding::Map));
	Ctx->MapKey(TrueForm, HCKeys::Get(EHCBinding::TrueForm));
	Ctx->MapKey(GhostUp, HCKeys::Get(EHCBinding::GhostUp));
	Ctx->MapKey(GhostDown, HCKeys::Get(EHCBinding::GhostDown));
	Ctx->MapKey(LobbyMenu, HCKeys::Get(EHCBinding::TaskList));
	// Fixed
	Ctx->MapKey(Pause, EKeys::Escape);
	Ctx->MapKey(Pause, EKeys::F1); // in the editor Esc ends Play-In-Editor
}
