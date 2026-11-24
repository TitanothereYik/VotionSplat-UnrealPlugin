#include "VotionSplatComponent.h"

#include "VotionSplatAsset.h"
#include "VotionSplatSceneProxy.h"

UVotionSplatComponent::UVotionSplatComponent()
{
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	bUseAsOccluder = false;
	SetGenerateOverlapEvents(false);
	bHiddenInGame = false;
	SetMobility(EComponentMobility::Movable);
	PrimaryComponentTick.bCanEverTick = false;
}

FPrimitiveSceneProxy* UVotionSplatComponent::CreateSceneProxy()
{
	if (!SplatAsset || !SplatAsset->GetGPU() || SplatAsset->NumSplats <= 0)
	{
		return nullptr;
	}
	return new FVotionSplatSceneProxy(this);
}

FBoxSphereBounds UVotionSplatComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const FBox Box = (SplatAsset && SplatAsset->LocalBounds.IsValid) ? SplatAsset->LocalBounds : FBox(FVector(-1), FVector(1));
	return FBoxSphereBounds(Box).TransformBy(LocalToWorld);
}

#if WITH_EDITOR
void UVotionSplatComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	MarkRenderStateDirty();
}
#endif
