// Console commands hc.* (docs/02 §18). Open the console with ^ (German keyboard) or ` / F10.
// State-changing commands run on the server; typed on a client they are forwarded with a debug RPC.

#include "Characters/HCCharacter.h"
#include "Core/HCGameInstance.h"
#include "Core/HCGameMode.h"
#include "Core/HCLog.h"
#include "Core/HCPlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Online/HCSessionSubsystem.h"
#include "World/HCStationBuilder.h"
#include "World/HCStationLayout.h"

namespace HCDebug
{
	static AHCCharacter* GetLocalCharacter(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? Cast<AHCCharacter>(PC->GetPawn()) : nullptr;
	}

	/** Runs on the host directly, or forwards to the host from a client. */
	static void RunOnServer(UWorld* World, const FString& Command, const TArray<FString>& Args)
	{
		if (!World)
		{
			return;
		}
		if (AHCGameMode* GM = World->GetAuthGameMode<AHCGameMode>())
		{
			GM->ExecuteDebugCommand(Command, Args, nullptr);
		}
		else if (AHCPlayerController* PC = Cast<AHCPlayerController>(World->GetFirstPlayerController()))
		{
			PC->ServerDebugCommand(Command, Args);
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdUnstuck(
		TEXT("hc.Unstuck"),
		TEXT("Teleports your character to the nearest free safe point (same as /unstuck)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (AHCCharacter* Character = GetLocalCharacter(World))
			{
				Character->ServerUnstuck();
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdExposure(
		TEXT("hc.Exposure"),
		TEXT("hc.Exposure <EV100>  Sets the fixed exposure of the station (lower = brighter). Default 3."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1 || !World)
			{
				UE_LOG(LogHC, Display, TEXT("Usage: hc.Exposure <EV100>"));
				return;
			}
			const float EV = FCString::Atof(*Args[0]);
			for (TActorIterator<AHCStationBuilder> It(World); It; ++It)
			{
				It->SetExposure(EV);
			}
			UE_LOG(LogHC, Display, TEXT("Exposure set to EV100 %.2f"), EV);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdWhere(
		TEXT("hc.Where"),
		TEXT("Prints your position and the room you are in."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			AHCCharacter* Character = GetLocalCharacter(World);
			if (!Character)
			{
				return;
			}
			FName Space = NAME_None;
			for (TActorIterator<AHCStationBuilder> It(World); It; ++It)
			{
				Space = It->GetSpaceNameAt(Character->GetActorLocation());
			}
			UE_LOG(LogHC, Display, TEXT("Position %s, room %s"), *Character->GetActorLocation().ToCompactString(), *Space.ToString());
		}));

	static FAutoConsoleCommand CmdValidateLayout(
		TEXT("hc.ValidateLayout"),
		TEXT("Checks the Kepler-7 layout (sizes, doors, reachability) and prints the result."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const FHCStationLayout Layout = FHCStationLayout::MakeKepler7();
			const TArray<FString> Problems = FHCStationGrid(Layout).Validate();
			if (Problems.IsEmpty())
			{
				UE_LOG(LogHC, Display, TEXT("Kepler7 layout OK (%d spaces, %d doors)"), Layout.Spaces.Num(), Layout.Doors.Num());
			}
			for (const FString& Problem : Problems)
			{
				UE_LOG(LogHC, Error, TEXT("Layout: %s"), *Problem);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdConnect(
		TEXT("hc.Connect"),
		TEXT("hc.Connect <IP[:Port]>  Joins a host in the LAN directly (adds name and version)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1 || !World)
			{
				UE_LOG(LogHC, Display, TEXT("Usage: hc.Connect <IP>"));
				return;
			}
			UHCGameInstance* GI = Cast<UHCGameInstance>(World->GetGameInstance());
			if (UHCSessionSubsystem* Sessions = GI ? GI->GetSubsystem<UHCSessionSubsystem>() : nullptr)
			{
				Sessions->Join(Args[0], GI->GetPlayerName());
			}
		}));

#if !UE_BUILD_SHIPPING
	static FAutoConsoleCommandWithWorldAndArgs CmdStartMatch(
		TEXT("hc.StartMatch"),
		TEXT("Host: starts the round (from the lobby, or right here on a directly opened match map)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("StartMatch"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdSkipIntro(
		TEXT("hc.SkipIntro"),
		TEXT("Ends the intro immediately."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("SkipIntro"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdEndMatch(
		TEXT("hc.EndMatch"),
		TEXT("hc.EndMatch <Crew|Monster>  Ends the round with this winner."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("EndMatch"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdSetTimer(
		TEXT("hc.SetTimer"),
		TEXT("hc.SetTimer <Seconds>  Sets the remaining time of the current phase."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("SetTimer"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdForceRole(
		TEXT("hc.ForceRole"),
		TEXT("hc.ForceRole <Player> <Crew|Monster>  Forces a role for the next role assignment."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("ForceRole"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdCompleteAllTasks(
		TEXT("hc.CompleteAllTasks"),
		TEXT("Sets the task progress to 100 % and runs the win check."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("CompleteAllTasks"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdGotoStation(
		TEXT("hc.GotoStation"),
		TEXT("hc.GotoStation <TaskId> [Step]  Puts you in front of a task station (test helper)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("GotoStation"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdKillMe(
		TEXT("hc.KillMe"),
		TEXT("Own death with jumpscare and ghost, without a monster."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("KillMe"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdJumpscare(
		TEXT("hc.Jumpscare"),
		TEXT("hc.Jumpscare [1-4]  Shows a jumpscare without dying (only on this PC): 1 Quake, 2 Lights out, 3 Dragged down, 4 Signal lost; without a number a random one."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AHCPlayerController* PC = World ? Cast<AHCPlayerController>(World->GetFirstPlayerController()) : nullptr)
			{
				PC->StartJumpscarePreview(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdSabotage(
		TEXT("hc.Sabotage"),
		TEXT("hc.Sabotage Power <Nord|Ost|SW>  |  hc.Sabotage Door <DoorId>  |  hc.Sabotage Repair"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("Sabotage"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdGotoFuseBox(
		TEXT("hc.GotoFuseBox"),
		TEXT("hc.GotoFuseBox <Nord|Ost|SW>  Puts you in front of a fuse box (test helper)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("GotoFuseBox"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdStartMeeting(
		TEXT("hc.StartMeeting"),
		TEXT("Starts a meeting right away, as if you had pressed the emergency button."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("StartMeeting"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdGotoButton(
		TEXT("hc.GotoButton"),
		TEXT("Puts you in front of the emergency button (test helper)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("GotoButton"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdListPlayers(
		TEXT("hc.ListPlayers"),
		TEXT("Players, ids and state into the log. Roles only on the host."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { RunOnServer(World, TEXT("ListPlayers"), Args); }));

	static FAutoConsoleCommandWithWorldAndArgs CmdSimulateHostLeave(
		TEXT("hc.SimulateHostLeave"),
		TEXT("Host leaves without confirmation - tests the 'host left' path on all clients."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (World && World->GetNetMode() == NM_ListenServer)
			{
				if (UHCSessionSubsystem* Sessions = World->GetGameInstance()->GetSubsystem<UHCSessionSubsystem>())
				{
					Sessions->LeaveToMainMenu();
				}
			}
			else
			{
				UE_LOG(LogHC, Display, TEXT("hc.SimulateHostLeave only works on the host"));
			}
		}));
#endif
}
