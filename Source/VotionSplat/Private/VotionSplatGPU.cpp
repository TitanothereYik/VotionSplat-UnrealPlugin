#include "VotionSplatGPU.h"

#include "RHICommandList.h"
#include "RHIResources.h"

void FVotionSplatGPU::InitRHI(FRHICommandListBase& RHICmdList)
{
	if (SplatInit.Num() == 0)
	{
		return;
	}
	FRHIResourceCreateInfo SplatInfo(TEXT("VotionSplat.Data"), &SplatInit);
	const uint32 SplatBytes = SplatInit.GetResourceDataSize();
	SplatBuffer = RHICmdList.CreateStructuredBuffer(sizeof(FVector4f), SplatBytes, BUF_Static | BUF_ShaderResource, SplatInfo);
	SplatSRV = RHICmdList.CreateShaderResourceView(SplatBuffer);

	if (ShInit.Num() == 0)
	{
		ShInit.Add(0.f);
	}
	FRHIResourceCreateInfo ShInfo(TEXT("VotionSplat.SH"), &ShInit);
	ShBuffer = RHICmdList.CreateStructuredBuffer(sizeof(float), ShInit.GetResourceDataSize(), BUF_Static | BUF_ShaderResource, ShInfo);
	ShSRV = RHICmdList.CreateShaderResourceView(ShBuffer);
}

void FVotionSplatGPU::ReleaseRHI()
{
	SplatSRV.SafeRelease();
	ShSRV.SafeRelease();
	SplatBuffer.SafeRelease();
	ShBuffer.SafeRelease();
}
