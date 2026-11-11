#include "Tasks/HCTaskTypes.h"

namespace
{
	FHCTaskStepDef Step(EHCTaskStepType Type, float Duration, const TCHAR* Room, const TCHAR* Label,
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

	FHCTaskDefinition Task(const TCHAR* Id, const TCHAR* Name, EHCTaskCategory Category, std::initializer_list<FHCTaskStepDef> Steps)
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
	Defs.Add(Task(TEXT("Security_RegisterBadge"), TEXT("Ausweis registrieren"), C::Common, {
		Step(T::Hold, 3.f, TEXT("Sicherheit"), TEXT("Ausweis registrieren"), O::HoldPad) }));
	// #3
	Defs.Add(Task(TEXT("Reactor_Valves"), TEXT("Kühlmittel-Ventile"), C::Short, {
		Step(T::Minigame, 8.f, TEXT("Reaktor"), TEXT("Ventile einstellen"), O::Dials) }));
	// #4
	Defs.Add(Task(TEXT("Reactor_Calibrate"), TEXT("Kern kalibrieren"), C::Long, {
		Step(T::Hold, 12.f, TEXT("Reaktor"), TEXT("Kern kalibrieren"), O::Numbers, 0.f, 10) }));
	// #5: take the sample, spin it (you may walk away while the centrifuge runs), analyse it
	Defs.Add(Task(TEXT("Sample_Analyze"), TEXT("Probe analysieren"), C::Long, {
		Step(T::Hold, 2.f, TEXT("Lager"), TEXT("Probe entnehmen"), O::HoldPad),
		Step(T::Hold, 2.f, TEXT("BioLabor"), TEXT("Zentrifuge starten"), O::HoldPad, 20.f),
		Step(T::Hold, 3.f, TEXT("Medbay"), TEXT("Probe auswerten"), O::Numbers, 0.f, 4) }));
	// #6
	Defs.Add(Task(TEXT("Comms_AlignAntenna"), TEXT("Antenne ausrichten"), C::Short, {
		Step(T::Minigame, 6.f, TEXT("Kommunikation"), TEXT("Antenne ausrichten"), O::Curve) }));
	// #7
	Defs.Add(Task(TEXT("Comms_TransmitData"), TEXT("Funkdaten übertragen"), C::Long, {
		Step(T::Hold, 8.f, TEXT("Kommunikation"), TEXT("Funkdaten laden"), O::Transfer),
		Step(T::Hold, 8.f, TEXT("Hangar"), TEXT("Funkdaten senden"), O::Transfer) }));
	// #9
	Defs.Add(Task(TEXT("Storage_ScanCargo"), TEXT("Fracht scannen"), C::Short, {
		Step(T::Sequence, 6.f, TEXT("Lager"), TEXT("Fracht scannen"), O::Buttons, 0.f, 3) }));
	// #11
	Defs.Add(Task(TEXT("Quarters_AirFilter"), TEXT("Luftfilter wechseln"), C::Short, {
		Step(T::Hold, 5.f, TEXT("Quartiere"), TEXT("Luftfilter wechseln"), O::Filter, 0.f, 4) }));
	// #12
	Defs.Add(Task(TEXT("BioLab_Cooling"), TEXT("Proben-Kühlung prüfen"), C::Short, {
		Step(T::Minigame, 6.f, TEXT("BioLabor"), TEXT("Kühlung stabilisieren"), O::Temperature) }));
	return Defs;
}
