#include "Core/HCMenuGameMode.h"

#include "Core/HCGameInstance.h"
#include "Core/HCLog.h"
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

void AHCMenuHUD::BeginPlay()
{
	Super::BeginPlay();
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
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
