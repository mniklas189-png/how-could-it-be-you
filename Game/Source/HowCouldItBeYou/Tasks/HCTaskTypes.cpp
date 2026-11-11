#include "Tasks/HCTaskTypes.h"

namespace
{
	FHCTaskStepDef Step(EHCTaskStepType Type, float Duration, const TCHAR* Room, const FText& Label,
		EHCTaskOverlayKind Overlay = EHCTaskOverlayKind::None, float WaitAfter = 0.f, int32 ButtonCount = 3)
	{
		FHCTaskStepDef S;
		S.Type = Type;
		S.Duration = Duration;
		S.Room = Room;
		S.Label = Label;
		S.Overlay = Overlay;
		S.WaitAfter = WaitAfter;
		S.ButtonCount = ButtonCount;
		return S;
	}

	FHCTaskDefinition Task(const TCHAR* Id, const FText& Name, EHCTaskCategory Category, std::initializer_list<FHCTaskStepDef> Steps)
	{
		FHCTaskDefinition T;
		T.TaskId = Id;
		T.DisplayName = Name;
		T.Category = Category;
		T.Steps = Steps;
		return T;
	}
}

TArray<FHCTaskDefinition> FHCTaskCatalog::MakeDefaults()
{
	using T = EHCTaskStepType;
	using O = EHCTaskOverlayKind;
	using C = EHCTaskCategory;

	TArray<FHCTaskDefinition> Defs;
	// #1
	Defs.Add(Task(TEXT("Security_RegisterBadge"), NSLOCTEXT("HC", "Task_Security_RegisterBadge", "Ausweis registrieren"), C::Common, {
		Step(T::Hold, 3.f, TEXT("Sicherheit"), NSLOCTEXT("HC", "TaskStep_Security_RegisterBadge_1", "Ausweis registrieren"), O::HoldPad) }));
	// #3
	Defs.Add(Task(TEXT("Reactor_Valves"), NSLOCTEXT("HC", "Task_Reactor_Valves", "Kühlmittel-Ventile"), C::Short, {
		Step(T::Minigame, 8.f, TEXT("Reaktor"), NSLOCTEXT("HC", "TaskStep_Reactor_Valves_1", "Ventile einstellen"), O::Dials) }));
	// #4
	Defs.Add(Task(TEXT("Reactor_Calibrate"), NSLOCTEXT("HC", "Task_Reactor_Calibrate", "Kern kalibrieren"), C::Long, {
		Step(T::Hold, 12.f, TEXT("Reaktor"), NSLOCTEXT("HC", "TaskStep_Reactor_Calibrate_1", "Kern kalibrieren"), O::Numbers, 0.f, 10) }));
	// #5: take the sample, spin it (you may walk away while the centrifuge runs), analyse it
	Defs.Add(Task(TEXT("Sample_Analyze"), NSLOCTEXT("HC", "Task_Sample_Analyze", "Probe analysieren"), C::Long, {
		Step(T::Hold, 2.f, TEXT("Lager"), NSLOCTEXT("HC", "TaskStep_Sample_Analyze_1", "Probe entnehmen"), O::HoldPad),
		Step(T::Hold, 2.f, TEXT("BioLabor"), NSLOCTEXT("HC", "TaskStep_Sample_Analyze_2", "Zentrifuge starten"), O::HoldPad, 20.f),
		Step(T::Hold, 3.f, TEXT("Medbay"), NSLOCTEXT("HC", "TaskStep_Sample_Analyze_3", "Probe auswerten"), O::Numbers, 0.f, 4) }));
	// #6
	Defs.Add(Task(TEXT("Comms_AlignAntenna"), NSLOCTEXT("HC", "Task_Comms_AlignAntenna", "Antenne ausrichten"), C::Short, {
		Step(T::Minigame, 6.f, TEXT("Kommunikation"), NSLOCTEXT("HC", "TaskStep_Comms_AlignAntenna_1", "Antenne ausrichten"), O::Curve) }));
	// #7
	Defs.Add(Task(TEXT("Comms_TransmitData"), NSLOCTEXT("HC", "Task_Comms_TransmitData", "Funkdaten übertragen"), C::Long, {
		Step(T::Hold, 8.f, TEXT("Kommunikation"), NSLOCTEXT("HC", "TaskStep_Comms_TransmitData_1", "Funkdaten laden"), O::Transfer),
		Step(T::Hold, 8.f, TEXT("Hangar"), NSLOCTEXT("HC", "TaskStep_Comms_TransmitData_2", "Funkdaten senden"), O::Transfer) }));
	// #9
	Defs.Add(Task(TEXT("Storage_ScanCargo"), NSLOCTEXT("HC", "Task_Storage_ScanCargo", "Fracht scannen"), C::Short, {
		Step(T::Sequence, 6.f, TEXT("Lager"), NSLOCTEXT("HC", "TaskStep_Storage_ScanCargo_1", "Fracht scannen"), O::Buttons, 0.f, 3) }));
	// #11
	Defs.Add(Task(TEXT("Quarters_AirFilter"), NSLOCTEXT("HC", "Task_Quarters_AirFilter", "Luftfilter wechseln"), C::Short, {
		Step(T::Hold, 5.f, TEXT("Quartiere"), NSLOCTEXT("HC", "TaskStep_Quarters_AirFilter_1", "Luftfilter wechseln"), O::Filter, 0.f, 4) }));
	// #12
	Defs.Add(Task(TEXT("BioLab_Cooling"), NSLOCTEXT("HC", "Task_BioLab_Cooling", "Proben-Kühlung prüfen"), C::Short, {
		Step(T::Minigame, 6.f, TEXT("BioLabor"), NSLOCTEXT("HC", "TaskStep_BioLab_Cooling_1", "Kühlung stabilisieren"), O::Temperature) }));
	return Defs;
}
