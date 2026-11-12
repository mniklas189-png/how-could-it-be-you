#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "HCIntroWidget.generated.h"

/** One page of the story: a log line on top, the text that is typed out below. The last page is the big closing line. */
struct FHCStoryBeat
{
	FText Header;
	FText Body;
	float Seconds = 0.f;
	bool bFinal = false;
};

/**
 * The story at the start of a round (phase Intro, docs/01 §2): what happened on Kepler-7 tonight, typed out
 * page by page like an emergency log, then "Es ist einer von euch." The first round of a session gets the whole
 * story, later rounds only the last part (the situation). Everyone sees the same, nothing depends on the roles.
 * Holding the interact key votes to skip; the majority ends it for everyone.
 * Painted by hand (FHCPainter); takes no input of its own, so the game keys keep working.
 */
UCLASS()
class HOWCOULDITBEYOU_API UHCIntroWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	static TArray<FHCStoryBeat> GetBeats();
	/** Length of the phase: the whole story (first round of a session) or only its last part. */
	static float GetStorySeconds(bool bFull);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Seconds into the whole story (the short version starts late), from the replicated phase end. */
	float GetStoryTime() const;

	int32 LastBeat = -1;
	/** 0..1 while the interact key is held; at 1 the vote is sent. */
	float SkipHold = 0.f;
	bool bSkipSent = false;
	double OpenedAt = 0.0;
};
