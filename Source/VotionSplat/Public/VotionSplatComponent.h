#pragma once

#include "Components/PrimitiveComponent.h"
#include "VotionSplatComponent.generated.h"

class UVotionSplatAsset;

UCLASS(ClassGroup = (Rendering), meta = (BlueprintSpawnableComponent))
class VOTIONSPLAT_API UVotionSplatComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UVotionSplatComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Votion Splat")
	TObjectPtr<UVotionSplatAsset> SplatAsset;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
