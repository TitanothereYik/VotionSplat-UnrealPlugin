#pragma once

#include "CoreMinimal.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "RenderResource.h"

class FVotionSplatGPU : public FRenderResource
{
public:
	TResourceArray<FVector4f> SplatInit;
	TResourceArray<float> ShInit;
	int32 NumSplats = 0;
	int32 ShDegree = 0;
	int32 ShCoeffs = 0;
	uint32 Profile = 0;
	float NearPlanePly = 0.01f;

	FBufferRHIRef SplatBuffer;
	FBufferRHIRef ShBuffer;
	FShaderResourceViewRHIRef SplatSRV;
	FShaderResourceViewRHIRef ShSRV;

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
};
