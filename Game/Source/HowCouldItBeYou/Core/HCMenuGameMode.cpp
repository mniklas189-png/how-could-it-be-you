#include "Core/HCMenuGameMode.h"

#include "Core/HCGameInstance.h"
#include "Core/HCLog.h"
#include "Core/HCTypes.h"
#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "World/HCModelAssets.h"
#include "World/HCStationBuilder.h"
#include "Interaction/HCChargingStation.h"
#include "Meeting/HCEmergencyButton.h"
#include "Sabotage/HCDoor.h"
#include "Sabotage/HCFuseBox.h"
#include "Tasks/HCTaskStation.h"
#include "Core/HCGameInstance.h"
#include "UI/HCSettingsWidget.h"
#include "Abilities/HCRemains.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Online/HCSessionSubsystem.h"
#include "TimerManager.h"
#include "UI/HCMainMenuWidget.h"

AHCMenuGameMode::AHCMenuGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = APlayerController::StaticClass();
	HUDClass = AHCMenuHUD::StaticClass();
}

void AHCMenuHUD::BuildBackdrop(APlayerController* PC)
{
	UWorld* World = GetWorld();
	AHCStationBuilder* Room = World->SpawnActorDeferred<AHCStationBuilder>(AHCStationBuilder::StaticClass(), FTransform::Identity, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Room)
	{
		return;
	}
	FString TourList;
	FString TourMap = TEXT("Kepler7");
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("HCTour="), TourList, false);
	// -HCTourMap=Lobby tours the lobby room instead of the station
	FParse::Value(FCommandLine::Get(), TEXT("HCTourMap="), TourMap);
#endif
	Room->LayoutName = TourList.IsEmpty() ? FName(TEXT("Lobby")) : FName(*TourMap);
	Room->bShowLabels = false;
	Room->FinishSpawning(FTransform::Identity);

	// The camera stands at one end of the room and looks along it; the menu sits on the left, the figures on the right
	const FVector CameraAt(240.f, 540.f, 162.f);
	if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(CameraAt, FRotator(-2.f, 8.f, 0.f)))
	{
		Camera->GetCameraComponent()->SetFieldOfView(72.f);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
		PC->SetViewTarget(Camera);
		MenuCamera = Camera;
	}

#if !UE_BUILD_SHIPPING
	// Development: -HCTour=x,y,z,yaw,pitch;... (metres, metres, cm, degrees) builds the whole station instead of the
	// menu room, takes one picture per stop (-HCShotDir, tour_N.png) and quits: a look at the station without a round.
	if (!TourList.IsEmpty())
	{
		TArray<FString> Stops;
		TourList.ParseIntoArray(Stops, TEXT(";"));
		FString Dir;
		FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir);
		// -HCTourBlood=x,y;x,y puts remains on the floor (metres)
		FString BloodList;
		FParse::Value(FCommandLine::Get(), TEXT("HCTourBlood="), BloodList, false);
		TArray<FString> BloodSpots;
		BloodList.ParseIntoArray(BloodSpots, TEXT(";"));
		for (const FString& Spot : BloodSpots)
		{
			FString X, Y;
			if (Spot.Split(TEXT(","), &X, &Y))
			{
				const FTransform SpotAt(FVector(FCString::Atof(*X) * 100.f, FCString::Atof(*Y) * 100.f, 0.f));
				if (AHCRemains* Remains = World->SpawnActorDeferred<AHCRemains>(AHCRemains::StaticClass(), SpotAt))
				{
					Remains->Configure(TEXT("Mia"), 3);
					Remains->FinishSpawning(SpotAt);
				}
			}
		}
		const TWeakObjectPtr<ACameraActor> TourCamera = MenuCamera;
		MenuCamera.Reset(); // no sway
		for (int32 Index = 0; Index < Stops.Num(); ++Index)
		{
			TArray<FString> V;
			Stops[Index].ParseIntoArray(V, TEXT(","));
			if (V.Num() < 5)
			{
				continue;
			}
			const FVector At(FCString::Atof(*V[0]) * 100.f, FCString::Atof(*V[1]) * 100.f, FCString::Atof(*V[2]));
			const FRotator Look(FCString::Atof(*V[4]), FCString::Atof(*V[3]), 0.f);
			FTimerHandle MoveHandle, ShotHandle;
			GetWorldTimerManager().SetTimer(MoveHandle, FTimerDelegate::CreateWeakLambda(this, [this, TourCamera, At, Look]()
			{
				if (Menu)
				{
					Menu->RemoveFromParent();
				}
				if (ACameraActor* Camera = TourCamera.Get())
				{
					Camera->SetActorLocationAndRotation(At, Look);
				}
			}), 12.f + Index * 2.f, false);
			GetWorldTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(this, [Dir, Index]()
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("tour_%d.png"), Index + 1)), false, false);
			}), 13.5f + Index * 2.f, false);
		}
		FTimerHandle QuitHandle;
		GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateWeakLambda(this, [this]() { GetOwningPlayerController()->ConsoleCommand(TEXT("quit")); }),
			14.f + Stops.Num() * 2.f, false);
		return;
	}
	// Development: -HCPropSheet=<mesh path>,<mesh path>,... shows meshes in two rows with numbers, takes a picture
	// (-HCShotDir) and quits. Used to choose and check props from the model packs without opening the editor.
	FString SheetList;
	if (FParse::Value(FCommandLine::Get(), TEXT("HCPropSheet="), SheetList, false))
	{
		TArray<FString> Paths;
		SheetList.ParseIntoArray(Paths, TEXT(","));
		for (int32 Index = 0; Index < Paths.Num(); ++Index)
		{
			UStaticMesh* SheetMesh = Cast<UStaticMesh>(FSoftObjectPath(Paths[Index] + TEXT(".") + FPaths::GetBaseFilename(Paths[Index])).TryLoad());
			const int32 Column = Index % 7;
			const bool bFar = Index >= 7;
			const FVector At(bFar ? 1080.f : 760.f, 170.f + Column * (bFar ? 150.f : 125.f) + (bFar ? 0.f : 60.f), bFar ? 60.f : 1.f);
			if (SheetMesh)
			{
				AStaticMeshActor* Prop = World->SpawnActor<AStaticMeshActor>(At, FRotator(0.f, 180.f, 0.f));
				Prop->SetMobility(EComponentMobility::Movable);
				Prop->GetStaticMeshComponent()->SetStaticMesh(SheetMesh);
				const FBoxSphereBounds Bounds = SheetMesh->GetBounds();
				const float Biggest = static_cast<float>(Bounds.BoxExtent.GetMax()) * 2.f;
				const float Fit = Biggest < 55.f ? 80.f / FMath::Max(1.f, Biggest) : Biggest > 150.f ? 150.f / Biggest : 1.f;
				Prop->SetActorScale3D(FVector(Fit));
				UE_LOG(LogHC, Display, TEXT("[HCSmoke] Prop %d %s scale %.2f min %s max %s"), Index + 1, *FPaths::GetBaseFilename(Paths[Index]), Fit,
					*(Bounds.Origin - Bounds.BoxExtent).ToCompactString(), *(Bounds.Origin + Bounds.BoxExtent).ToCompactString());
			}
			ATextRenderActor* Number = World->SpawnActor<ATextRenderActor>(At + FVector(-40.f, 0.f, bFar ? 190.f : 175.f), FRotator(0.f, 180.f, 0.f));
			Number->GetTextRender()->SetText(FText::FromString(FString::Printf(TEXT("%d%s"), Index + 1, SheetMesh ? TEXT("") : TEXT(" ?"))));
			Number->GetTextRender()->SetWorldSize(26.f);
			Number->GetTextRender()->SetHorizontalAlignment(EHTA_Center);
			Number->GetTextRender()->SetTextRenderColor(FColor(255, 230, 120));
		}
		if (const UHCModelSet* SheetModels = FParse::Param(FCommandLine::Get(), TEXT("HCSheetMonster")) ? HCModels::Get() : nullptr)
		{
			// The monster body with its idle, walk and run animation, facing the camera
			UAnimSequence* Moves[] = { SheetModels->MonsterIdle.Get(), SheetModels->MonsterWalk.Get(), SheetModels->MonsterRun.Get() };
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(Moves); ++Index)
			{
				ASkeletalMeshActor* Figure = World->SpawnActor<ASkeletalMeshActor>(FVector(620.f, 430.f + Index * 170.f, 1.f), FRotator(0.f, 90.f, 0.f));
				if (Figure && SheetModels->MonsterMesh)
				{
					Figure->GetSkeletalMeshComponent()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					Figure->GetSkeletalMeshComponent()->SetSkeletalMesh(SheetModels->MonsterMesh);
					if (Moves[Index])
					{
						Figure->GetSkeletalMeshComponent()->PlayAnimation(Moves[Index], true);
					}
				}
			}
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("HCSheetActors")))
		{
			// The real blocks of the game with their fronts to the camera
			const FRotator Facing(0.f, 180.f, 0.f);
			for (const float Y : { 260.f, 390.f, 520.f })
			{
				World->SpawnActor<AHCTaskStation>(FVector(760.f, Y, 55.f), Facing);
			}
			World->SpawnActor<AHCChargingStation>(FVector(760.f, 650.f, 70.f), Facing);
			World->SpawnActor<AHCFuseBox>(FVector(760.f, 770.f, 130.f), Facing);
			World->SpawnActor<AHCEmergencyButton>(FVector(760.f, 880.f, 50.f), Facing);
			const FTransform DoorAt(FRotator::ZeroRotator, FVector(1100.f, 1040.f, 0.f));
			if (AHCDoor* Door = World->SpawnActorDeferred<AHCDoor>(AHCDoor::StaticClass(), DoorAt))
			{
				Door->Configure(280.f, 240.f, 20.f, false, TEXT("Sheet"));
				Door->FinishSpawning(DoorAt);
			}
		}
		if (APointLight* Lamp = World->SpawnActor<APointLight>(FVector(520.f, 600.f, 300.f), FRotator::ZeroRotator))
		{
			Lamp->SetMobility(EComponentMobility::Movable);
			if (UPointLightComponent* LampLight = Cast<UPointLightComponent>(Lamp->GetLightComponent()))
			{
				LampLight->SetIntensityUnits(ELightUnits::Candelas);
				LampLight->SetIntensity(260.f);
				LampLight->SetAttenuationRadius(1600.f);
				LampLight->SetCastShadows(false);
			}
		}
		FString Dir;
		FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir);
		FTimerHandle ShotHandle;
		GetWorldTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(this, [this, Dir]()
		{
			if (Menu)
			{
				Menu->RemoveFromParent();
			}
			if (ACameraActor* SheetCamera = MenuCamera.Get())
			{
				MenuCamera.Reset(); // no sway for the sheet
				SheetCamera->SetActorLocationAndRotation(FVector(180.f, 600.f, 190.f), FRotator(-6.f, 0.f, 0.f));
			}
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, TEXT("propsheet.png")), false, false);
			FTimerHandle QuitHandle;
			GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateWeakLambda(this, [this]() { GetOwningPlayerController()->ConsoleCommand(TEXT("quit")); }), 2.f, false);
		}), 14.f, false);
		return;
	}
#endif
	// "One of you": a few crew members stand in the half-dark and look at you. Needs the model packs.
	const UHCModelSet* Models = HCModels::Get();
	if (Models && Models->CharacterMesh)
	{
		const FVector2D Spots[] = { FVector2D(1010.f, 700.f), FVector2D(1150.f, 870.f), FVector2D(960.f, 1010.f), FVector2D(1340.f, 610.f) };
		const int32 Colors[] = { 1, 4, 2, 6 };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Spots); ++Index)
		{
			const FVector At(Spots[Index].X, Spots[Index].Y, 1.f);
			// The model looks along its own +Y, so it is turned a quarter back from the direction to the camera
			const FRotator Facing(0.f, (CameraAt - At).Rotation().Yaw - 90.f, 0.f);
			ASkeletalMeshActor* Figure = World->SpawnActor<ASkeletalMeshActor>(At, Facing);
			if (!Figure)
			{
				continue;
			}
			USkeletalMeshComponent* Body = Figure->GetSkeletalMeshComponent();
			Body->SetSkeletalMesh(Models->CharacterMesh);
			if (Models->CharacterAnimClass)
			{
				Body->SetAnimInstanceClass(Models->CharacterAnimClass);
			}
			for (const FName Slot : Models->TintSlots)
			{
				const int32 SlotIndex = Body->GetMaterialIndex(Slot);
				if (UMaterialInstanceDynamic* Tinted = SlotIndex != INDEX_NONE ? Body->CreateDynamicMaterialInstance(SlotIndex) : nullptr)
				{
					Tinted->SetVectorParameterValue(Models->TintParameter, HCIdentity::Color(Colors[Index]));
				}
			}
		}
	}
	// Red emergency light from behind the group
	if (APointLight* Red = World->SpawnActor<APointLight>(FVector(1500.f, 900.f, 240.f), FRotator::ZeroRotator))
	{
		Red->SetMobility(EComponentMobility::Movable);
		if (UPointLightComponent* Light = Cast<UPointLightComponent>(Red->GetLightComponent()))
		{
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetIntensity(45.f);
			Light->SetAttenuationRadius(1200.f);
			Light->SetLightColor(FLinearColor(1.f, 0.07f, 0.05f));
			Light->SetCastShadows(false);
		}
	}
}

void AHCMenuHUD::DrawHUD()
{
	Super::DrawHUD();
	// The view sways very slowly, so the picture behind the menu is not frozen
	if (ACameraActor* Camera = MenuCamera.Get())
	{
		const float Time = GetWorld()->GetTimeSeconds();
		Camera->SetActorRotation(FRotator(-2.f + 0.6f * FMath::Sin(Time * 0.23f), 8.f + 3.5f * FMath::Sin(Time * 0.11f), 0.f));
	}
}

void AHCMenuHUD::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	BuildBackdrop(PC);
	if (UHCGameInstance* Instance = Cast<UHCGameInstance>(GetGameInstance()))
	{
		Instance->ApplyMasterVolume();
	}
#if !UE_BUILD_SHIPPING
	// Development: -HCSettingsShot opens the settings menu, steps a value down and up again, takes a picture and quits
	if (FParse::Param(FCommandLine::Get(), TEXT("HCSettingsShot")))
	{
		FString ShotDir;
		FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), ShotDir);
		FTimerHandle OpenHandle, ShotHandle, QuitHandle;
		GetWorldTimerManager().SetTimer(OpenHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (UHCSettingsWidget* Settings = UHCSettingsWidget::Open(GetOwningPlayerController()))
			{
				Settings->Adjust(UHCSettingsWidget::Volume, -1);
				Settings->Adjust(UHCSettingsWidget::Volume, 1);
			}
		}), 10.f, false);
		GetWorldTimerManager().SetTimer(ShotHandle, FTimerDelegate::CreateWeakLambda(this, [ShotDir]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(ShotDir, TEXT("settings.png")), true, false);
		}), 12.f, false);
		GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateWeakLambda(this, [this]() { GetOwningPlayerController()->ConsoleCommand(TEXT("quit")); }), 14.f, false);
	}
#endif
	Menu = CreateWidget<UHCMainMenuWidget>(PC, UHCMainMenuWidget::StaticClass());
	Menu->AddToViewport();
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Menu->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);

#if !UE_BUILD_SHIPPING
	// Smoke test: -HCAutoHost [-HCName=<n>] hosts a lobby; -HCAutoJoinCodeFile=<path> joins with the code the host wrote
	FString Name = TEXT("Tester");
	FParse::Value(FCommandLine::Get(), TEXT("HCName="), Name);
	UHCSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UHCSessionSubsystem>();
	// Back in the menu after a played round: last screenshot (shows the "host left" message) and quit
	UHCGameInstance* GI = Cast<UHCGameInstance>(GetGameInstance());
	if (GI && GI->Smoke.bSawGameOver && FParse::Param(FCommandLine::Get(), TEXT("HCQuitAfterRound")))
	{
		FString Dir;
		FParse::Value(FCommandLine::Get(), TEXT("HCShotDir="), Dir);
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Dir, Name]()
		{
			if (!Dir.IsEmpty())
			{
				FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString::Printf(TEXT("%s_99_MainMenu.png"), *Name)), true, false);
			}
			FTimerHandle QuitHandle;
			GetWorldTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				UE_LOG(LogHC, Display, TEXT("[HCSmoke] Done in main menu"));
				GetOwningPlayerController()->ConsoleCommand(TEXT("quit"));
			}), 1.5f, false);
		}), 2.5f, false);
		return;
	}
	if (Sessions && FParse::Param(FCommandLine::Get(), TEXT("HCAutoHost")))
	{
		GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [Sessions, Name]() { Sessions->HostLobby(Name); }));
	}

	FString CodeFile;
	if (Sessions && FParse::Value(FCommandLine::Get(), TEXT("HCAutoJoinCodeFile="), CodeFile))
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Sessions, Name, CodeFile]()
		{
			FString Code;
			if (FFileHelper::LoadFileToString(Code, *CodeFile) && !Code.TrimStartAndEnd().IsEmpty() && !Sessions->IsBusy() && ++AutoJoinAttempts <= 10)
			{
				UE_LOG(LogHCNet, Display, TEXT("[HCSmoke] Joining with code %s (attempt %d)"), *Code.TrimStartAndEnd(), AutoJoinAttempts);
				Sessions->Join(Code.TrimStartAndEnd(), Name);
			}
		}), 4.f, true, 2.f);
	}
#endif
}
