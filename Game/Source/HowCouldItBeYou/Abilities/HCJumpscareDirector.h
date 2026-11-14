#pragma once

#include "CoreMinimal.h"
#include "Audio/HCProceduralAudio.h"
#include "Core/HCTypes.h"
#include "GameFramework/Actor.h"
#include "HCJumpscareDirector.generated.h"

class AHCCharacter;
class AHCPlayerController;
class UAudioComponent;
class UCameraComponent;
class UCanvas;
class UPointLightComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTexture2D;

/** The four scares (numbers as in hc.Jumpscare <n> and -HCJumpscareStyle=<n>). */
namespace HCScareStyle
{
	inline constexpr int32 Quake = 1;
	inline constexpr int32 LightsOut = 2;
	inline constexpr int32 DraggedDown = 3;
	inline constexpr int32 SignalLost = 4;
	inline constexpr int32 Count = 4;
}

/** Timing of the "Signal lost" scare, shared by the picture (director) and its sound (HCProceduralAudio::ESound::Static). */
namespace HCScareTimeline
{
	/** The picture freezes from X to Y and then jumps Z seconds ahead. */
	inline constexpr float SignalHolds[6][3] = { {0.50f, 0.58f, 0.f}, {0.74f, 0.79f, 0.06f}, {1.02f, 1.16f, 0.12f}, {1.38f, 1.43f, 0.f}, {1.62f, 1.71f, 0.1f}, {1.86f, 1.90f, 0.05f} };
	/** Single frames of a blood-red close-up. */
	inline constexpr float SignalFlashes[4] = { 0.66f, 1.20f, 1.55f, 1.95f };
	inline constexpr float SignalFlashLength = 0.04f;
	/** The first strike crackles the picture. */
	inline constexpr float SignalFirstBurst = 0.38f;
	/** The picture collapses like an old tube switching off. */
	inline constexpr float SignalCollapseStart = 2.04f;
	inline constexpr float SignalCollapseEnd = 2.30f;
}

/**
 * The "director" of the 2-3 seconds of horror (docs/02 §7.4). Exists ONLY on the victim's machine, is never
 * replicated: takes the camera, turns it towards the attacker, shows the true form of the Wandler right in
 * front of the camera, plays the scream and the stinger and cuts to black. Witnesses never see any of this.
 *
 * Every kill picks one of four scares (never the same one twice in a row on this PC), all with a screen shake
 * built from trauma: smooth noise plus a punch for every strike of the monster.
 *  1 Quake        the shake builds up, a punch with every strike, motion blur, red hits
 *  2 Lights out   the lights flicker and die; silence, a heartbeat, two eyes in the dark; then it slams into you
 *  3 Dragged down the first strike throws you to the floor, it crouches over you, blood on the lens
 *  4 Signal lost  the picture breaks: freezes, single-frame close-ups, static, an old tube switching off
 * Intensity "Reduced" scales shake, flashes and sound down; "Minimal" only fades to black.
 * It always ends by itself (client failsafe), even if the server never answers.
 */
UCLASS(NotPlaceable)
class HOWCOULDITBEYOU_API AHCJumpscareDirector : public AActor
{
	GENERATED_BODY()

public:
	AHCJumpscareDirector();

	/**
	 * Spawns the director locally and starts it.
	 * @param bPreview hc.Jumpscare: only watch, the player stays alive and gets the camera back.
	 * @param ForcedStyle 1..4 picks the scare (hc.Jumpscare <n>, -HCJumpscareStyle=<n>); 0 = a random one.
	 */
	static AHCJumpscareDirector* Start(AHCPlayerController* PC, const FVector& AttackerLocation, bool bPreview, int32 ForcedStyle = 0);

	/** A random scare 1..4 that is not Previous (Random01 in [0, 1)). */
	static int32 PickStyle(int32 Previous, float Random01);

	/** Starts making the blood pictures in the background, so the scare does not stall the game (once per process). */
	static void PrepareAssets();

	/** Seconds from the start to the cut to black. */
	static float DurationOf(int32 Style);

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Screen overlays (blood, static, torn lines, the picture collapsing), drawn by the HUD while the scare runs. */
	void DrawOverlay(UCanvas* Canvas);

protected:
	void Begin(AHCPlayerController* InPC, const FVector& InAttackerLocation, bool bInPreview, int32 ForcedStyle);
	void UpdateScare(float DeltaSeconds);
	void Strike(float Power);
	void PlaySounds();
	void PlayScare(HCProceduralAudio::ESound Sound, float Volume, bool bLoud);
	void SetAmbience(float Level);
	void ShowNameTags();
	void CutToBlack();
	void Finish();

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USceneComponent> FaceRoot;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Head;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UStaticMeshComponent> Maw;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TArray<TObjectPtr<UStaticMeshComponent>> Eyes;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TArray<TObjectPtr<UStaticMeshComponent>> UpperTeeth;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TArray<TObjectPtr<UStaticMeshComponent>> LowerTeeth;

	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> FaceLight;

	/** Lights out: in the dark, the only light sits just below the monster's face. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<UPointLightComponent> EyeLight;

	/** The real monster model, if the model packs are installed; replaces the face built from basic shapes. */
	UPROPERTY(VisibleAnywhere, Category = "HC")
	TObjectPtr<USkeletalMeshComponent> Monster;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> SplatTextures;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> NoiseTextures;

	bool bRealMonster = false;

	TWeakObjectPtr<AHCPlayerController> PC;
	TWeakObjectPtr<UCameraComponent> Camera;
	FVector AttackerLocation = FVector::ZeroVector;
	FVector LastCameraLocation = FVector::ZeroVector;
	FVector SavedCameraRelative = FVector::ZeroVector;
	FRotator StartRotation = FRotator::ZeroRotator;
	FRotator TargetRotation = FRotator::ZeroRotator;
	FRotator CurrentRotation = FRotator::ZeroRotator;
	FPostProcessSettings SavedPostProcess;
	EHCJumpscareIntensity Intensity = EHCJumpscareIntensity::Full;
	int32 Style = HCScareStyle::Quake;
	float Elapsed = 0.f;
	float Duration = 2.3f;
	float TurnTime = 0.2f;
	float SavedFOV = 90.f;
	bool bPreview = false;
	bool bStingerPlayed = false;
	bool bScreamPlayed = false;
	bool bFlashed = false;
	bool bBlack = false;
	bool bFinished = false;
	bool bAmbienceChanged = false;
	int32 DebugShots = 0;

	// Shake: trauma decays by itself, every strike adds to it and punches the camera
	float Trauma = 0.f;
	float StrikeAge = 10.f;
	float StrikePower = 0.f;
	float StrikeRollSign = 1.f;
	float NoiseTime = 0.f;
	int32 NextStrike = 0;
	int32 NextCue = 0;
	int32 NextFlash = 0;
	/** Dragged down: the edges darken with every splash of blood. */
	float Darkness = 0.f;
	/** Signal lost: the picture stands still (and so do its loud sounds). */
	bool bHolding = false;
	bool bSubliminal = false;
	bool bVoicesPaused = false;
	float Muffle = 0.f;
	FRotator HeldRotation = FRotator::ZeroRotator;
	FVector HeldLocation = FVector::ZeroVector;
	float HeldFOV = 90.f;
	FRandomStream Random;

	/** Scream, stinger and hits: cut at the black, muffled on the floor, paused while the signal freezes. */
	TArray<TWeakObjectPtr<UAudioComponent>> LoudVoices;

	/** Names over the heads of the others (the attacker's would float over the monster), hidden while the scare runs. */
	TArray<TWeakObjectPtr<AHCCharacter>> HiddenNameTags;

	struct FSplat
	{
		FVector2f Pos = FVector2f::ZeroVector;
		float Size = 0.3f;
		float Angle = 0.f;
		float Born = 0.f;
		int32 Texture = 0;
	};
	TArray<FSplat> Splats;
};
