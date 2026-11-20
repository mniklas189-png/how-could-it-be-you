#pragma once

#include "CoreMinimal.h"

/**
 * The few things a player changes at the wardrobe in the lobby (World/HCWardrobe), on top of the colour of the
 * jacket (the identity, which no two players share): trousers, hair, boots and whether the backpack is worn.
 * Packed into one byte (FHCAppearance::Outfit), so a disguise copies it with the rest of the look.
 * The colours act on the material parameters of the Survival Character model (Tint, RootColor/TipColor).
 */
namespace HCOutfit
{
	enum class EPart : uint8
	{
		Trousers,
		Hair,
		Boots,
		Backpack,
		Count
	};

	/** Choices of a part (the first is the model's own look). */
	inline int32 Choices(EPart Part)
	{
		switch (Part)
		{
		case EPart::Trousers: return 4;
		case EPart::Hair: return 5;
		case EPart::Boots: return 3;
		case EPart::Backpack: return 2;
		default: return 1;
		}
	}

	/** Bit position and width of a part in the byte. */
	inline void Bits(EPart Part, int32& OutShift, int32& OutWidth)
	{
		switch (Part)
		{
		case EPart::Trousers: OutShift = 0; OutWidth = 2; return;
		case EPart::Hair: OutShift = 2; OutWidth = 3; return;
		case EPart::Boots: OutShift = 5; OutWidth = 2; return;
		default: OutShift = 7; OutWidth = 1; return;
		}
	}

	inline int32 Get(uint8 Outfit, EPart Part)
	{
		int32 Shift = 0, Width = 0;
		Bits(Part, Shift, Width);
		return FMath::Clamp(static_cast<int32>((Outfit >> Shift) & ((1 << Width) - 1)), 0, Choices(Part) - 1);
	}

	inline uint8 With(uint8 Outfit, EPart Part, int32 Choice)
	{
		int32 Shift = 0, Width = 0;
		Bits(Part, Shift, Width);
		const uint8 Mask = static_cast<uint8>(((1 << Width) - 1) << Shift);
		return static_cast<uint8>((Outfit & ~Mask) | ((FMath::Clamp(Choice, 0, Choices(Part) - 1) << Shift) & Mask));
	}

	/** Multiplier on the model's own colour (Tint): 1 = as it comes. */
	inline FLinearColor TrouserTint(int32 Choice)
	{
		static const FLinearColor Tints[] = { FLinearColor(1.f, 1.f, 1.f), FLinearColor(0.16f, 0.16f, 0.18f), FLinearColor(0.75f, 0.68f, 0.42f), FLinearColor(0.55f, 0.56f, 0.58f) };
		return Tints[FMath::Clamp(Choice, 0, 3)];
	}

	inline FLinearColor BootTint(int32 Choice)
	{
		static const FLinearColor Tints[] = { FLinearColor(1.f, 1.f, 1.f), FLinearColor(0.12f, 0.12f, 0.12f), FLinearColor(0.85f, 0.55f, 0.32f) };
		return Tints[FMath::Clamp(Choice, 0, 2)];
	}

	/** Hair at the root and at the tips (the model's own: brown). */
	inline void HairColors(int32 Choice, FLinearColor& OutRoot, FLinearColor& OutTip)
	{
		static const FLinearColor Roots[] = { FLinearColor(0.015f, 0.008f, 0.004f), FLinearColor(0.004f, 0.004f, 0.004f), FLinearColor(0.25f, 0.16f, 0.06f),
			FLinearColor(0.12f, 0.025f, 0.008f), FLinearColor(0.16f, 0.16f, 0.16f) };
		static const FLinearColor Tips[] = { FLinearColor(0.051f, 0.028f, 0.013f), FLinearColor(0.012f, 0.011f, 0.011f), FLinearColor(0.55f, 0.4f, 0.18f),
			FLinearColor(0.3f, 0.07f, 0.02f), FLinearColor(0.42f, 0.42f, 0.42f) };
		const int32 Index = FMath::Clamp(Choice, 0, 4);
		OutRoot = Roots[Index];
		OutTip = Tips[Index];
	}

	/** Colour for the wardrobe's swatches (how the choice looks, roughly). */
	inline FLinearColor Swatch(EPart Part, int32 Choice)
	{
		switch (Part)
		{
		case EPart::Trousers:
		{
			static const FLinearColor Looks[] = { FLinearColor(0.12f, 0.2f, 0.36f), FLinearColor(0.03f, 0.03f, 0.035f), FLinearColor(0.32f, 0.29f, 0.16f), FLinearColor(0.22f, 0.22f, 0.24f) };
			return Looks[FMath::Clamp(Choice, 0, 3)];
		}
		case EPart::Hair:
		{
			FLinearColor Root, Tip;
			HairColors(Choice, Root, Tip);
			return Tip * 1.6f;
		}
		case EPart::Boots:
		{
			static const FLinearColor Looks[] = { FLinearColor(0.2f, 0.13f, 0.08f), FLinearColor(0.025f, 0.025f, 0.025f), FLinearColor(0.42f, 0.25f, 0.12f) };
			return Looks[FMath::Clamp(Choice, 0, 2)];
		}
		default:
			return Choice == 0 ? FLinearColor(0.3f, 0.32f, 0.26f) : FLinearColor(0.06f, 0.06f, 0.06f);
		}
	}

	HOWCOULDITBEYOU_API FText PartName(EPart Part);
	HOWCOULDITBEYOU_API FText ChoiceName(EPart Part, int32 Choice);
}
