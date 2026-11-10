#include "Core/HCInputConfig.h"

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
	Config->GhostUp = MakeAction(Config, TEXT("IA_GhostUp"), EInputActionValueType::Boolean);
	Config->GhostDown = MakeAction(Config, TEXT("IA_GhostDown"), EInputActionValueType::Boolean);
	Config->Pause = MakeAction(Config, TEXT("IA_Pause"), EInputActionValueType::Boolean);
	Config->LobbyMenu = MakeAction(Config, TEXT("IA_TaskList"), EInputActionValueType::Boolean);

	UInputMappingContext* Ctx = NewObject<UInputMappingContext>(Config, TEXT("IMC_Default"));
	Config->DefaultContext = Ctx;

	// Move: X = right, Y = forward
	{
		FEnhancedActionKeyMapping& W = Ctx->MapKey(Config->Move, EKeys::W);
		AddSwizzleYX(Ctx, W);
		FEnhancedActionKeyMapping& S = Ctx->MapKey(Config->Move, EKeys::S);
		AddSwizzleYX(Ctx, S);
		AddNegate(Ctx, S, true, true);
		Ctx->MapKey(Config->Move, EKeys::D);
		FEnhancedActionKeyMapping& A = Ctx->MapKey(Config->Move, EKeys::A);
		AddNegate(Ctx, A, true, false);
	}

	// Look: mouse, Y negated so moving the mouse up looks up
	{
		FEnhancedActionKeyMapping& Mouse = Ctx->MapKey(Config->Look, EKeys::Mouse2D);
		AddNegate(Ctx, Mouse, false, true);
	}

	Ctx->MapKey(Config->Sprint, EKeys::LeftShift);
	Ctx->MapKey(Config->Flashlight, EKeys::F);
	Ctx->MapKey(Config->Interact, EKeys::E);
	Ctx->MapKey(Config->Kill, EKeys::LeftMouseButton);
	Ctx->MapKey(Config->Shapeshift, EKeys::Q);
	Ctx->MapKey(Config->GhostUp, EKeys::SpaceBar);
	Ctx->MapKey(Config->GhostDown, EKeys::LeftControl);
	Ctx->MapKey(Config->Pause, EKeys::Escape);
	Ctx->MapKey(Config->Pause, EKeys::F1); // in the editor Esc ends Play-In-Editor
	Ctx->MapKey(Config->LobbyMenu, EKeys::Tab);

	return Config;
}
