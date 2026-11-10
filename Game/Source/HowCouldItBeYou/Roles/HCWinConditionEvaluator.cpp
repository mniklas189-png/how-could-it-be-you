#include "Roles/HCWinConditionEvaluator.h"

EHCWinner FHCWinConditionEvaluator::Evaluate(const FHCWinInput& In, FText& OutReason)
{
	// 1. No monster left -> crew wins
	if (In.AliveMonsters <= 0)
	{
		if (In.bMonsterLeft)
		{
			OutReason = NSLOCTEXT("HC", "WinMonsterLeft", "Der Wandler hat das Spiel verlassen");
		}
		else if (In.bMonsterEjected)
		{
			OutReason = NSLOCTEXT("HC", "WinEjected", "Der Wandler wurde ausgeworfen");
		}
		else
		{
			OutReason = NSLOCTEXT("HC", "WinNoMonster", "Kein Wandler mehr an Bord");
		}
		return EHCWinner::Crew;
	}

	// 2. Parity: living monsters >= living crew -> monster wins (all modes)
	if (In.bParityEnabled && In.AliveMonsters >= In.AliveCrew)
	{
		OutReason = NSLOCTEXT("HC", "WinParity", "Die Crew wurde gefressen");
		return EHCWinner::Monster;
	}

	// 3. All tasks done (only with a non-empty task list)
	if (In.bCrewWinsByTasks && In.TotalTasks > 0 && In.CompletedTasks >= In.TotalTasks)
	{
		OutReason = NSLOCTEXT("HC", "WinTasks", "Alle Aufgaben erledigt");
		return EHCWinner::Crew;
	}

	// 4. Timer (Evacuation)
	if (In.bTimerEnabled && In.bTimerExpired)
	{
		if (In.AliveCrew >= In.MinCrewAliveAtTimeout)
		{
			OutReason = NSLOCTEXT("HC", "WinRescue", "Rettungsschiff angedockt");
			return EHCWinner::Crew;
		}
		OutReason = NSLOCTEXT("HC", "WinTooFewSurvivors", "Zu wenige Überlebende");
		return EHCWinner::Monster;
	}

	OutReason = FText::GetEmpty();
	return EHCWinner::None;
}
