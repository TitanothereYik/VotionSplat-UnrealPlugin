#pragma once

#include "RenderResource.h"

class FVotionInverseViewLut : public FRenderResource
{
public:
	FTextureRHIRef Texture;
	int32 Size = 1;
	bool bValid = false;

	virtual void InitRHI(FRHICommandListBase& RHICmdList) override;
	virtual void ReleaseRHI() override;
};

extern TGlobalResource<FVotionInverseViewLut> GVotionInverseViewLut;
