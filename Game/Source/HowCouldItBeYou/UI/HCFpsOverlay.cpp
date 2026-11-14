#include "UI/HCFpsOverlay.h"

#include "Core/HCGraphics.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "HAL/PlatformTime.h"
#include "UI/HCLook.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

bool FHCFpsMeter::AddFrame(double Seconds, float GpuFrameMs)
{
	// A stall (loading a map, the window was dragged) is not a picture rate: start the span again
	if (Seconds <= 0.0 || Seconds > 2.0)
	{
		SpanTime = 0.0;
		SpanFrames = 0;
		GpuSum = 0.0;
		GpuFrames = 0;
		return false;
	}
	SpanTime += Seconds;
	++SpanFrames;
	if (GpuFrameMs > 0.f)
	{
		GpuSum += GpuFrameMs;
		++GpuFrames;
	}
	if (SpanTime < SpanSeconds)
	{
		return false;
	}
	Fps = static_cast<float>(SpanFrames / SpanTime);
	FrameMs = static_cast<float>(SpanTime * 1000.0 / SpanFrames);
	GpuMs = GpuFrames > 0 ? static_cast<float>(GpuSum / GpuFrames) : 0.f;
	SpanTime = 0.0;
	SpanFrames = 0;
	GpuSum = 0.0;
	GpuFrames = 0;
	return true;
}

void FHCFpsMeter::Reset()
{
	*this = FHCFpsMeter();
}

float HCFps::OverlayScale(float WindowScale)
{
	return FMath::Clamp(WindowScale, SmallestScale, LargestScale);
}

FString HCFps::FpsText(const FHCFpsMeter& Meter)
{
	return Meter.Fps > 0.f ? FString::Printf(TEXT("%d FPS"), FMath::RoundToInt32(Meter.Fps)) : FString();
}

FString HCFps::DetailText(const FHCFpsMeter& Meter, FIntPoint RenderSize)
{
	if (Meter.Fps <= 0.f)
	{
		return FString();
	}
	FString Text = FString::Printf(TEXT("%.1f ms"), Meter.FrameMs);
	if (Meter.GpuMs > 0.f)
	{
		Text += FString::Printf(TEXT(" · GPU %.1f ms"), Meter.GpuMs);
	}
	if (RenderSize.X > 0 && RenderSize.Y > 0)
	{
		Text += FString::Printf(TEXT(" · %d x %d"), RenderSize.X, RenderSize.Y);
	}
	return Text;
}

void SHCFpsOverlay::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(true);
	const FLinearColor Shadow(0.f, 0.f, 0.f, 0.9f);
	ChildSlot
	.HAlign(HAlign_Right)
	.VAlign(VAlign_Top)
	[
		SNew(SDPIScaler)
		.DPIScale(this, &SHCFpsOverlay::GetExtraScale)
		[
			// The right edge lines up with the HUD's mini map, which starts below the two lines
			SNew(SBox)
			.Padding(FMargin(0.f, 8.f, 30.f, 0.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					// Every digit as wide as the next one: the text does not jump while the number changes
					SAssignNew(Label, STextBlock)
					.Font(HCLook::Mono(12.f))
					.ColorAndOpacity(FSlateColor(HCLook::Alpha(HCLook::Bone, 0.92f)))
					.ShadowOffset(FVector2D(1.f, 1.f))
					.ShadowColorAndOpacity(Shadow)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SAssignNew(Details, STextBlock)
					.Font(HCLook::Mono(10.f))
					.ColorAndOpacity(FSlateColor(HCLook::Alpha(HCLook::BoneMuted, 0.95f)))
					.ShadowOffset(FVector2D(1.f, 1.f))
					.ShadowColorAndOpacity(Shadow)
					.Visibility(EVisibility::Collapsed)
				]
			]
		]
	];
}

void SHCFpsOverlay::SetMode(int32 InMode)
{
	if (Mode != InMode)
	{
		Mode = InMode;
		Details->SetVisibility(Mode >= HCFps::Detailed ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
		Details->SetText(FText::GetEmpty()); // filled when the running span is over
	}
}

float SHCFpsOverlay::GetExtraScale() const
{
	FVector2D Size(1920., 1080.);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Size);
	}
	// Everything on the game's window is already scaled with the size of the window
	const float WindowScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(FIntPoint(FMath::RoundToInt32(Size.X), FMath::RoundToInt32(Size.Y)));
	return WindowScale > 0.f ? HCFps::OverlayScale(WindowScale) / WindowScale : 1.f;
}

void SHCFpsOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// Real time between two pictures (the game's own time can run slower or be capped)
	const double Now = FPlatformTime::Seconds();
	const double Seconds = LastFrameTime > 0.0 ? Now - LastFrameTime : 0.0;
	LastFrameTime = Now;
	if (!Meter.AddFrame(Seconds, FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0))))
	{
		return;
	}
	Label->SetText(FText::AsCultureInvariant(HCFps::FpsText(Meter)));
	if (Mode >= HCFps::Detailed)
	{
		FIntPoint RenderSize(0, 0);
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			RenderSize = HCGraphics::RenderSize(GEngine->GameViewport->Viewport->GetSizeXY(), HCGraphics::CurrentResolutionPercent());
		}
		Details->SetText(FText::AsCultureInvariant(HCFps::DetailText(Meter, RenderSize)));
	}
}
