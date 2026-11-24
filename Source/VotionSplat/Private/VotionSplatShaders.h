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
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DrawRecords)
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

class FVotionCompactCountCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionCompactCountCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionCompactCountCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumSplats)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Projected)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SortedIndices)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, GroupCounts)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionCompactScanCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionCompactScanCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionCompactScanCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumGroups)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GroupCountsIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, GroupOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, DrawArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionCompactWriteCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionCompactWriteCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionCompactWriteCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumSplats)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, Projected)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SortedIndices)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GroupOffsetsIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float4>, DrawRecords)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionScanReduceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionScanReduceCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionScanReduceCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumItems)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ScanIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, GroupSums)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionScanGroupsCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionScanGroupsCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionScanGroupsCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumGroups)
		SHADER_PARAMETER(uint32, Capacity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GroupSumsIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, GroupOffsets)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, TotalsOut)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionScanWriteCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionScanWriteCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionScanWriteCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumItems)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ScanIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GroupOffsetsIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ScanOut)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionRadixHistCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionRadixHistCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionRadixHistCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, Shift)
		SHADER_PARAMETER(uint32, NumBlocks)
		SHADER_PARAMETER(uint32, PairCapacity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TotalsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, KeysIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, Hist)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionRadixScatterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionRadixScatterCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionRadixScatterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, Shift)
		SHADER_PARAMETER(uint32, NumBlocks)
		SHADER_PARAMETER(uint32, PairCapacity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TotalsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, KeysIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ValsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, HistOffsetsIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, KeysOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ValsOut)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionTileCountCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionTileCountCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionTileCountCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumSplats)
		SHADER_PARAMETER(FIntPoint, ViewSizeI)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DrawArgsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DrawRecords)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, PairCounts)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionTileEmitCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionTileEmitCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionTileEmitCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumSplats)
		SHADER_PARAMETER(uint32, TilesX)
		SHADER_PARAMETER(FIntPoint, ViewSizeI)
		SHADER_PARAMETER(uint32, PairCapacity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DrawArgsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DrawRecords)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PairOffsetsIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, KeysOut)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ValsOut)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionTileRangesCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionTileRangesCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionTileRangesCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, PairCapacity)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TotalsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, KeysIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, TileStart)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, TileEnd)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionTileFinalizeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionTileFinalizeCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionTileFinalizeCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, DrawArgsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TotalsIn)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, HwArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FVotionTileRasterCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVotionTileRasterCS);
	SHADER_USE_PARAMETER_STRUCT(FVotionTileRasterCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, TilesX)
		SHADER_PARAMETER(FIntPoint, ViewSizeI)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TotalsIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TileStartIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TileEndIn)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, PairValues)
		SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, DrawRecords)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepth)
		SHADER_PARAMETER(FIntPoint, DepthRectMin)
		SHADER_PARAMETER(FVector2f, DepthScale)
		SHADER_PARAMETER(uint32, bDepthTest)
		SHADER_PARAMETER(float, AlphaMax)
		SHADER_PARAMETER(float, AlphaMin)
		SHADER_PARAMETER(float, StopT)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, AccumOut)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

BEGIN_SHADER_PARAMETER_STRUCT(FVotionRasterPassParams, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVotionRasterVS::FParameters, VS)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVotionRasterPS::FParameters, PS)
	RDG_BUFFER_ACCESS(DrawArgs, ERHIAccess::IndirectArgs)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FVotionSortPassParams, )
	SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, SortedOut)
END_SHADER_PARAMETER_STRUCT()
