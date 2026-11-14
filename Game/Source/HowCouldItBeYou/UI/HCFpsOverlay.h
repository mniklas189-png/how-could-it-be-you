#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;

/**
 * Counts the pictures of short spans of time: the number on screen changes twice a second instead of with every
 * picture, so it can be read.
 */
struct HOWCOULDITBEYOU_API FHCFpsMeter
{
	/** Length of one span. */
	static constexpr double SpanSeconds = 0.5;

	/**
	 * One picture took Seconds; GpuFrameMs is the time the graphics card needed for it (0 = not known).
	 * True when a span is over and Fps, FrameMs and GpuMs hold new numbers.
	 */
	bool AddFrame(double Seconds, float GpuFrameMs);
	void Reset();

	/** Pictures per second, time per picture and time of the graphics card per picture over the last span. */
	float Fps = 0.f;
	float FrameMs = 0.f;
	float GpuMs = 0.f;

private:
	double SpanTime = 0.0;
	int32 SpanFrames = 0;
	double GpuSum = 0.0;
	int32 GpuFrames = 0;
};

namespace HCFps
{
	/** What the counter can show (settings, picture page, "FPS-Anzeige"). */
	enum EMode : int32 { Off, Simple, Detailed, ModeCount };

	/**
	 * The menus scale with the window (1 at 1080p, 0.67 at 720p, 2 at 2160p); the counter only between these two, so
	 * that it stays readable in a small window and stays above the HUD's mini map on a big screen.
	 */
	constexpr float SmallestScale = 0.85f;
	constexpr float LargestScale = 1.25f;
	/** The scale of the counter for a window of this size. */
	HOWCOULDITBEYOU_API float OverlayScale(float WindowScale);

	/** First line of the counter: "72 FPS"; empty before the first span is over. */
	HOWCOULDITBEYOU_API FString FpsText(const FHCFpsMeter& Meter);
	/**
	 * Second line (only with details): time per picture, time of the graphics card and the size the 3D picture is
	 * computed at, e.g. "13.9 ms · GPU 9.8 ms · 1536 x 864". Parts that are not known are left out.
	 */
	HOWCOULDITBEYOU_API FString DetailText(const FHCFpsMeter& Meter, FIntPoint RenderSize);
}

/**
 * The FPS counter: small text in the top right corner, on top of everything (menus, lobby, round). It lets the mouse
 * through. Put on the game's window by UHCGraphicsSubsystem while the setting is on.
 */
class SHCFpsOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHCFpsOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetMode(int32 InMode);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** What is missing from the window's own scale to the scale of the counter. */
	float GetExtraScale() const;

	TSharedPtr<STextBlock> Label;
	TSharedPtr<STextBlock> Details;
	FHCFpsMeter Meter;
	int32 Mode = HCFps::Simple;
	double LastFrameTime = 0.0;
};
