#pragma once

#include "CoreMinimal.h"
#include "RenderGraphFwd.h"

struct FVotionViewParams
{
	FMatrix44f ViewToClip{ EForceInit::ForceInitToZero };
	FVector2f ViewSize = FVector2f::ZeroVector;
	FVector4f Intrinsics = FVector4f::Zero();
	FRDGTextureRef SceneDepth = nullptr;
	FIntPoint DepthRectMin = FIntPoint::ZeroValue;
	FVector2f DepthScale = FVector2f(1.f, 1.f);
	uint32 bDepthTest = 1;
	uint32 bDebugQuads = 0;
};

struct FVotionSortScratch
{
	int32 Capacity = 0;
	FBufferRHIRef Key[2];
	FBufferRHIRef Value[2];
	FShaderResourceViewRHIRef KeySRV[2];
	FShaderResourceViewRHIRef ValueSRV[2];
	FUnorderedAccessViewRHIRef KeyUAV[2];
	FUnorderedAccessViewRHIRef ValueUAV[2];
};
