#include "Characters/HCBodyMeshComponent.h"

void UHCBodyMeshComponent::FinalizeBoneTransform()
{
	// Only a freshly worked-out pose is waiting to be shown; without one the buffer holds an old frame
	if (PoseEdit && bNeedToFlipSpaceBaseBuffers)
	{
		PoseEdit(GetEditableComponentSpaceTransforms());
	}
	Super::FinalizeBoneTransform();
}

UHCViewArmComponent::UHCViewArmComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UHCViewArmComponent::ShowPose(TFunctionRef<void(TArray<FTransform>&)> Fill)
{
	if (!GetSkinnedAsset() || GetNumComponentSpaceTransforms() == 0)
	{
		return;
	}
	Fill(GetEditableComponentSpaceTransforms());
	bNeedToFlipSpaceBaseBuffers = true;
	FinalizeBoneTransform();
	UpdateChildTransforms();
	UpdateBounds();
	MarkRenderTransformDirty();
	MarkRenderDynamicDataDirty();
}
