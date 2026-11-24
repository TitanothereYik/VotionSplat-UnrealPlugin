#pragma once

#include "CoreMinimal.h"

class FVotionSplatGPU;
class UAssetImportData;

#include "VotionSplatAsset.generated.h"

UCLASS(BlueprintType, hidecategories = Object)
class VOTIONSPLAT_API UVotionSplatAsset : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, Category = "Votion Splat")
	int32 NumSplats = 0;

	UPROPERTY(VisibleAnywhere, Category = "Votion Splat")
	int32 ShDegree = 0;

	UPROPERTY(VisibleAnywhere, Category = "Votion Splat")
	FString RasterProfile;

	UPROPERTY(VisibleAnywhere, Category = "Votion Splat")
	float NearPlanePly = 0.01f;

	UPROPERTY(VisibleAnywhere, Category = "Votion Splat")
	FBox LocalBounds = FBox(ForceInit);

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Instanced, Category = "Import Settings")
	TObjectPtr<UAssetImportData> AssetImportData;
#endif

	TArray<FVector4f> SplatData;
	TArray<float> ShRest;

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostInitProperties() override;
#endif

	void RebuildGPU();
	TSharedPtr<FVotionSplatGPU, ESPMode::ThreadSafe> GetGPU() const { return GPU; }
	uint32 GetProfileIndex() const;

private:
	TSharedPtr<FVotionSplatGPU, ESPMode::ThreadSafe> GPU;
};
