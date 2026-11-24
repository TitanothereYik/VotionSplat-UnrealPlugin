#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FVotionProjectCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionProjectCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionProjectCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumSplats)
		SHADER_PARAMETER(uint32, ShCoeffs)
		SHADER_PARAMETER(uint32, ShDegree)
		SHADER_PARAMETER(uint32, Profile)
		SHADER_PARAMETER(FMatrix44f, LocalToView)
		SHADER_PARAMETER(FMatrix44f, ViewToClip)
		SHADER_PARAMETER(FVector3f, CameraPosLocal)
		SHADER_PARAMETER(FVector2f, ViewSize)
		SHADER_PARAMETER(FVector4f, Intrinsics)
		SHADER_PARAMETER(float, NearCm)
		SHADER_PARAMETER(float, FarCm)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SplatData)
		SHADER_PARAMETER_SRV(StructuredBuffer<float>, ShRest)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, Projected)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionSortKeysCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionSortKeysCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionSortKeysCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumSplats)
		SHADER_PARAMETER(FVector3f, DepthAxis)
		SHADER_PARAMETER_SRV(StructuredBuffer<float4>, SplatData)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, OutKeys)
		SHADER_PARAMETER_UAV(RWBuffer<uint>, OutValues)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionRasterVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionRasterVS);
	SHADER_USE_PARAMETER_STRUCT(FVotionRasterVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Projected)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SortedIndices)
		SHADER_PARAMETER(FVector2f, ViewSize)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionRasterPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionRasterPS);
	SHADER_USE_PARAMETER_STRUCT(FVotionRasterPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER(FIntPoint, DepthRectMin)
		SHADER_PARAMETER(FVector2f, DepthScale)
		SHADER_PARAMETER(uint32, bDepthTest)
		SHADER_PARAMETER(float, AlphaMax)
		SHADER_PARAMETER(float, AlphaMin)
		SHADER_PARAMETER(uint32, bDebugQuads)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionCompositePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionCompositePS);
	SHADER_USE_PARAMETER_STRUCT(FVotionCompositePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTex)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, AccumTex)
		SHADER_PARAMETER(FIntPoint, OutputRectMin)
		SHADER_PARAMETER(FIntPoint, SceneRectMin)
		SHADER_PARAMETER(uint32, Mode)
		SHADER_PARAMETER(uint32, bOutputIsSrgbTexture)
		SHADER_PARAMETER_TEXTURE(Texture3D, InverseViewLut)
		SHADER_PARAMETER_SAMPLER(SamplerState, LutSampler)
		SHADER_PARAMETER(float, LutSize)
		SHADER_PARAMETER(FVector4f, Rec709ToWorking0)
		SHADER_PARAMETER(FVector4f, Rec709ToWorking1)
		SHADER_PARAMETER(FVector4f, Rec709ToWorking2)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

BEGIN_SHADER_PARAMETER_STRUCT(FVotionRasterPassParams, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVotionRasterVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVotionRasterPS::FParameters, PS)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FVotionSortPassParams, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, SortedOut)
END_SHADER_PARAMETER_STRUCT()
