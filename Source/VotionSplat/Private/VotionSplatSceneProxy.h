#pragma once

#include "PrimitiveSceneProxy.h"
#include "RenderGraphResources.h"
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

	void RenderSplats(FRDGBuilder& GraphBuilder, const FSceneView& View, const FVotionViewParams& Params, FRDGTextureRef Accum);

	TSharedPtr<FVotionSplatGPU, ESPMode::ThreadSafe> GPU;

private:
	void EnsureScratch(FRHICommandListBase& RHICmdList);

	struct FViewSortCache
	{
		FVector3f LastAxis{ ForceInitToZero };
		int32 N = 0;
		TRefCountPtr<FRDGPooledBuffer> Sorted;
		uint32 LastFrame = 0;
	};

	TSharedPtr<FVotionSortScratch, ESPMode::ThreadSafe> Scratch;
	TMap<uint64, FViewSortCache> SortCache;
};
