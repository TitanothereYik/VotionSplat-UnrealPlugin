#pragma once

#include "PrimitiveSceneProxy.h"
#include "RenderGraphResources.h"
#include "RHIGPUReadback.h"
#include "VotionSplatGPU.h"
#include "VotionSplatRender.h"

class UVotionSplatComponent;
class FRDGBuilder;

class FVotionSplatSceneProxy : public FPrimitiveSceneProxy
{
public:
	explicit FVotionSplatSceneProxy(const UVotionSplatComponent* Component);
	virtual ~FVotionSplatSceneProxy() override;

	virtual SIZE_T GetTypeHash() const override;
	virtual uint32 GetMemoryFootprint() const override { return sizeof(*this); }
	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override;
	virtual void CreateRenderThreadResources(FRHICommandListBase& RHICmdList) override;
	virtual void DestroyRenderThreadResources() override;

	// bFirstInAccum: nothing has been drawn into Accum yet, so the tile rasterizer may overwrite it.
	void RenderSplats(FRDGBuilder& GraphBuilder, const FSceneView& View, const FVotionViewParams& Params, FRDGTextureRef Accum, bool bFirstInAccum);

	TSharedPtr<FVotionSplatGPU, ESPMode::ThreadSafe> GPU;

private:
	void EnsureScratch(FRHICommandListBase& RHICmdList);
	void UpdatePairCapacity(int32 Count);

	struct FProjectKey
	{
		FMatrix44f LocalToView;
		FMatrix44f ViewToClip;
		FVector4f Intrinsics;
		FVector3f CamLocal;
		FVector2f ViewSize;
		float NearCm = 0.f;
		int32 N = 0;
		uint32 Profile = 0;
		const void* Gpu = nullptr;
	};

	struct FViewSortCache
	{
		FViewSortCache()
		{
			FMemory::Memzero(ProjectKey);
		}

		FVector3f LastAxis{ ForceInitToZero };
		int32 N = 0;
		TRefCountPtr<FRDGPooledBuffer> Sorted;
		uint32 LastFrame = 0;
		FProjectKey ProjectKey;
		TRefCountPtr<FRDGPooledBuffer> Projected;
		TRefCountPtr<FRDGPooledBuffer> DrawRecords;
		TRefCountPtr<FRDGPooledBuffer> DrawArgs;
		uint32 TileCapacity = 0;
		TRefCountPtr<FRDGPooledBuffer> TileValues;
		TRefCountPtr<FRDGPooledBuffer> TileStart;
		TRefCountPtr<FRDGPooledBuffer> TileEnd;
		TRefCountPtr<FRDGPooledBuffer> TileTotals;
		TRefCountPtr<FRDGPooledBuffer> TileHwArgs;
	};

	TSharedPtr<FVotionSortScratch, ESPMode::ThreadSafe> Scratch;
	TMap<uint64, FViewSortCache> SortCache;
	uint32 PairCapacity = 0;
	TUniquePtr<FRHIGPUBufferReadback> PairReadback;
	bool bPairReadbackPending = false;
	uint32 LastPairs = 0;
};
