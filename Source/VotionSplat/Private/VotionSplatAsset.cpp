#include "VotionSplatAsset.h"

#include "VotionSplatGPU.h"
#include "VotionSplatLog.h"

#include "RenderingThread.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

void UVotionSplatAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	int32 Version = 1;
	Ar << Version;
	if (Ar.IsLoading() && Version != 1)
	{
		UE_LOG(LogVotionSplat, Error, TEXT("Votion splat asset %s has unknown version %d"), *GetName(), Version);
		SplatData.Empty();
		ShRest.Empty();
		NumSplats = 0;
		return;
	}
	SplatData.BulkSerialize(Ar);
	ShRest.BulkSerialize(Ar);
}

void UVotionSplatAsset::PostLoad()
{
	Super::PostLoad();
	if (NumSplats > 0 && !IsTemplate())
	{
		RebuildGPU();
	}
}

#if WITH_EDITOR
void UVotionSplatAsset::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject) && !AssetImportData)
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
}
#endif

uint32 UVotionSplatAsset::GetProfileIndex() const
{
	if (RasterProfile == TEXT("gsplat_classic"))
	{
		return 1;
	}
	if (RasterProfile == TEXT("litegs_classic"))
	{
		return 2;
	}
	return 0;
}

void UVotionSplatAsset::RebuildGPU()
{
	if (NumSplats <= 0 || SplatData.Num() < NumSplats * 4)
	{
		GPU.Reset();
		return;
	}
	FVotionSplatGPU* Raw = new FVotionSplatGPU();
	Raw->NumSplats = NumSplats;
	Raw->ShDegree = ShDegree;
	Raw->ShCoeffs = FMath::Max(0, (ShDegree + 1) * (ShDegree + 1) - 1);
	Raw->Profile = GetProfileIndex();
	Raw->NearPlanePly = NearPlanePly;
	Raw->SplatInit.Append(SplatData);
	Raw->ShInit.Append(ShRest);
	BeginInitResource(Raw);
	GPU = TSharedPtr<FVotionSplatGPU, ESPMode::ThreadSafe>(Raw, [](FVotionSplatGPU* Resource)
	{
		if (Resource)
		{
			ENQUEUE_RENDER_COMMAND(VotionSplatReleaseGPU)([Resource](FRHICommandListImmediate&)
			{
				Resource->ReleaseResource();
				delete Resource;
			});
		}
	});
#if !WITH_EDITOR
	SplatData.Empty();
	ShRest.Empty();
#endif
}
