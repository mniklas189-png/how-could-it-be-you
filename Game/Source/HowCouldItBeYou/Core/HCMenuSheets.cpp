// Development picture sheets of the main menu scene: -HCEmoteSheet (every emote, carrying, other clothes).
#include "Core/HCMenuGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/HCCharacter.h"
#include "Characters/HCOutfit.h"
#include "Components/PointLightComponent.h"
#include "Core/HCLog.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UI/HCMainMenuWidget.h"
#include "UnrealClient.h"

void AHCMenuHUD::BuildEmoteSheet(APlayerController* PC)
{
#if !UE_BUILD_SHIPPING
	using HCOutfit::EPart;
	UWorld* World = GetWorld();
	// Eight figures, one per emote, and a ninth that carries a container; every one in other clothes
	constexpr int32 Figures = static_cast<int32>(EHCEmote::Count) + 1;
	static const TCHAR* Names[Figures] = { TEXT("Mia"), TEXT("Ben"), TEXT("Kim"), TEXT("Tom"), TEXT("Lea"), TEXT("Jan"), TEXT("Ida"), TEXT("Max"), TEXT("Ole") };
	TArray<TWeakObjectPtr<AHCCharacter>> Row;
	for (int32 Index = 0; Index < Figures; ++Index)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// They look towards -X, at the camera; the capsule is 90 high
		AHCCharacter* Figure = World->SpawnActor<AHCCharacter>(AHCCharacter::StaticClass(), FVector(760.f, 160.f + Index * 115.f, 91.f), FRotator(0.f, 180.f, 0.f), Params);
		if (Figure)
		{
			uint8 Outfit = 0;
			Outfit = HCOutfit::With(Outfit, EPart::Trousers, Index % HCOutfit::Choices(EPart::Trousers));
			Outfit = HCOutfit::With(Outfit, EPart::Hair, Index % HCOutfit::Choices(EPart::Hair));
			Outfit = HCOutfit::With(Outfit, EPart::Boots, Index % HCOutfit::Choices(EPart::Boots));
			Outfit = HCOutfit::With(Outfit, EPart::Backpack, (Index / 2) % 2);
			Figure->ServerSetAppearance(Index % HCIdentity::Count, Names[Index], Outfit);
			if (Index == Figures - 1)
			{
				Figure->ServerSetCarrying(true);
			}
		}
		Row.Add(Figure);
	}
	for (const float LampY : { 250.f, 640.f, 1030.f })
	{
		if (APointLight* Lamp = World->SpawnActor<APointLight>(FVector(560.f, LampY, 250.f), FRotator::ZeroRotator))
		{
			Lamp->SetMobility(EComponentMobility::Movable);
			if (UPointLightComponent* LampLight = Cast<UPointLightComponent>(Lamp->GetLightComponent()))
			{
				LampLight->SetIntensityUnits(ELightUnits::Candelas);
				LampLight->SetIntensity(200.f);
				LampLight->SetAttenuationRadius(1200.f);
				LampLight->SetCastShadows(false);
			}
		}
	}
	FString Dir;
	FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir);
	const TWeakObjectPtr<ACameraActor> SheetCamera = MenuCamera;
	MenuCamera.Reset(); // no sway
	auto LookFrom = [SheetCamera](const FVector& From, const FVector& At, float Fov)
	{
		if (ACameraActor* Camera = SheetCamera.Get())
		{
			Camera->SetActorLocationAndRotation(From, (At - From).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(Fov);
		}
	};
	// One step every 2 s: the emote starts, the picture follows 1 s later (the middle of the 2.6 s gesture)
	struct FStep { FString Name; TFunction<void()> Do; };
	TArray<FStep> Steps;
	Steps.Add({ TEXT("all"), [LookFrom]() { LookFrom(FVector(180.f, 620.f, 140.f), FVector(760.f, 620.f, 100.f), 80.f); } });
	for (int32 Index = 0; Index < Figures; ++Index)
	{
		const float Y = 160.f + Index * 115.f;
		const TWeakObjectPtr<AHCCharacter> Figure = Row[Index];
		const bool bCarry = Index == Figures - 1;
		auto Play = [Figure, Index, bCarry]()
		{
			if (AHCCharacter* Who = Figure.Get(); Who && !bCarry)
			{
				Who->MulticastPlayEmote(static_cast<uint8>(Index));
			}
		};
		const FString Name = bCarry ? FString(TEXT("carry")) : HCGesture::Name(static_cast<EHCEmote>(Index)).ToString().Replace(TEXT(" "), TEXT("_"));
		Steps.Add({ FString::Printf(TEXT("%d_%s_front"), Index + 1, *Name), [LookFrom, Play, Y]() { Play(); LookFrom(FVector(520.f, Y, 135.f), FVector(760.f, Y, 110.f), 62.f); } });
		Steps.Add({ FString::Printf(TEXT("%d_%s_side"), Index + 1, *Name), [LookFrom, Play, Y]() { Play(); LookFrom(FVector(700.f, Y - 210.f, 135.f), FVector(760.f, Y, 110.f), 62.f); } });
	}
	// The own view while carrying: the container at the bottom of the picture, no flashlight
	{
		const TWeakObjectPtr<AHCCharacter> Carrier = Row.Last();
		Steps.Add({ TEXT("carry_own_view"), [PC, Carrier]()
		{
			if (AHCCharacter* Own = Carrier.Get())
			{
				PC->Possess(Own);
				PC->SetControlRotation(FRotator(-10.f, 180.f, 0.f));
				PC->SetViewTarget(Own);
			}
		} });
	}
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		FTimerHandle DoHandle, ShotHandle;
		GetWorldTimerManager().SetTimer(DoHandle, FTimerDelegate::CreateWeakLambda(this, [this, Step = Steps[Index]]()
		{
			if (Menu)
			{
				Menu->RemoveFromParent();
			}
			Step.Do();
		}), 12.f + Index * 2.f, false);
		GetWorldTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(this, [Dir, Index, Name = Steps[Index].Name]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("emote_%02d_%s.png"), Index + 1, *Name)), false, false);
		}), 13.f + Index * 2.f, false);
	}
	FTimerHandle QuitHandle;
	GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateWeakLambda(this, [this]() { GetOwningPlayerController()->ConsoleCommand(TEXT("quit")); }),
		14.f + Steps.Num() * 2.f, false);
	UE_LOG(LogHC, Display, TEXT("[HCSmoke] Emote sheet: %d figures, %d pictures"), Figures, Steps.Num());
#endif
}
