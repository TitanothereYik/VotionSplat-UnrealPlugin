#include "VotionSplatSceneProxy.h"

#include "VotionSplatAsset.h"
#include "VotionSplatComponent.h"
#include "VotionSplatRegistry.h"
#include "VotionSplatShaders.h"

#include "CommonRenderResources.h"
#include "GPUSort.h"
#include "HAL/IConsoleManager.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneManagement.h"
#include "SceneView.h"

static TAutoConsoleVariable<int32> CVarVotionSortReuse(
	TEXT("r.VotionSplat.SortReuse"),
	1,
	TEXT("Reuse the sorted splat indices while the view direction is unchanged."),
	ECVF_RenderThreadSafe);

FVotionSplatSceneProxy::FVotionSplatSceneProxy(const UVotionSplatComponent* Component)
	: FPrimitiveSceneProxy(Component)
	, GPU(Component->SplatAsset ? Component->SplatAsset->GetGPU() : nullptr)
{
}

FVotionSplatSceneProxy::~FVotionSplatSceneProxy()
{
	FVotionSplatRegistry::Get().Remove(this);
}

SIZE_T FVotionSplatSceneProxy::GetTypeHash() const
{
	static size_t Unique;
	return reinterpret_cast<size_t>(&Unique);
}

FPrimitiveViewRelevance FVotionSplatSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Relevance;
	Relevance.bDrawRelevance = IsShown(View);
	Relevance.bDynamicRelevance = true;
	return Relevance;
}

void FVotionSplatSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	if (!IsSelected())
	{
		return;
	}
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		if (VisibilityMap & (1u << ViewIndex))
		{
			DrawWireBox(Collector.GetPDI(ViewIndex), GetBounds().GetBox(), FLinearColor(1.f, 0.5f, 0.f), SDPG_World);
		}
	}
}

void FVotionSplatSceneProxy::CreateRenderThreadResources(FRHICommandListBase& RHICmdList)
{
	FPrimitiveSceneProxy::CreateRenderThreadResources(RHICmdList);
	FVotionSplatRegistry::Get().Add(this);
}

void FVotionSplatSceneProxy::DestroyRenderThreadResources()
{
	FVotionSplatRegistry::Get().Remove(this);
	FPrimitiveSceneProxy::DestroyRenderThreadResources();
}

void FVotionSplatSceneProxy::EnsureScratch(FRHICommandListBase& RHICmdList)
{
	const int32 Count = GPU.IsValid() ? GPU->NumSplats : 0;
	if (Count <= 0 || (Scratch.IsValid() && Scratch->Capacity == Count))
	{
		return;
	}
	Scratch = MakeShared<FVotionSortScratch, ESPMode::ThreadSafe>();
	Scratch->Capacity = Count;
	const uint32 Bytes = uint32(Count) * sizeof(uint32);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FRHIResourceCreateInfo KeyInfo(TEXT("VotionSplat.SortKey"));
		Scratch->Key[Index] = RHICmdList.CreateVertexBuffer(Bytes, BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, KeyInfo);
		Scratch->KeySRV[Index] = RHICmdList.CreateShaderResourceView(Scratch->Key[Index], sizeof(uint32), PF_R32_UINT);
		Scratch->KeyUAV[Index] = RHICmdList.CreateUnorderedAccessView(Scratch->Key[Index], PF_R32_UINT);
		FRHIResourceCreateInfo ValueInfo(TEXT("VotionSplat.SortValue"));
		Scratch->Value[Index] = RHICmdList.CreateVertexBuffer(Bytes, BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, ValueInfo);
		Scratch->ValueSRV[Index] = RHICmdList.CreateShaderResourceView(Scratch->Value[Index], sizeof(uint32), PF_R32_UINT);
		Scratch->ValueUAV[Index] = RHICmdList.CreateUnorderedAccessView(Scratch->Value[Index], PF_R32_UINT);
	}
}

void FVotionSplatSceneProxy::RenderSplats(FRDGBuilder& GraphBuilder, const FSceneView& View, const FVotionViewParams& Params, FRDGTextureRef Accum)
{
	if (!GPU.IsValid() || GPU->NumSplats <= 0 || Params.ViewSize.X < 1.f || Params.ViewSize.Y < 1.f)
	{
		return;
	}
	EnsureScratch(GraphBuilder.RHICmdList);
	const int32 Count = GPU->NumSplats;
	const FMatrix LocalToViewD = GetLocalToWorld() * View.ViewMatrices.GetViewMatrix();
	const FMatrix44f LocalToView(LocalToViewD);
	const FVector CamLocal = GetLocalToWorld().InverseFast().TransformPosition(View.ViewMatrices.GetViewOrigin());
	const float PlyToCm = 100.f * FMath::Max(GetLocalToWorld().GetScaleVector().GetAbsMax(), KINDA_SMALL_NUMBER);
	const FVector3f DepthAxis(LocalToView.M[0][2], LocalToView.M[1][2], LocalToView.M[2][2]);
	const bool bLite = GPU->Profile == 2;

	FRDGBufferRef Projected = GraphBuilder.CreateBuffer(
		FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), uint32(Count) * 4), TEXT("VotionSplat.Projected"));
	{
		FVotionProjectCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionProjectCS::FParameters>();
		Pass->NumSplats = uint32(Count);
		Pass->ShCoeffs = uint32(FMath::Max(GPU->ShCoeffs, 0));
		Pass->ShDegree = uint32(GPU->ShDegree);
		Pass->Profile = GPU->Profile;
		Pass->LocalToView = LocalToView;
		Pass->ViewToClip = Params.ViewToClip;
		Pass->CameraPosLocal = FVector3f(CamLocal);
		Pass->ViewSize = Params.ViewSize;
		Pass->Intrinsics = Params.Intrinsics;
		Pass->NearCm = GPU->NearPlanePly * PlyToCm;
		Pass->FarCm = 1.0e10f;
		Pass->SplatData = GPU->SplatSRV;
		Pass->ShRest = GPU->ShSRV;
		Pass->Projected = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Projected));
		TShaderMapRef<FVotionProjectCS> Shader(GetGlobalShaderMap(View.GetFeatureLevel()));
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.Project"), Shader, Pass,
			FIntVector(FMath::DivideAndRoundUp(Count, 256), 1, 1));
	}

	const uint32 Frame = GFrameNumberRenderThread;
	for (TMap<uint64, FViewSortCache>::TIterator It(SortCache); It; ++It)
	{
		if (Frame - It.Value().LastFrame > 300)
		{
			It.RemoveCurrent();
		}
	}
	const uint32 ViewKey = View.GetViewKey();
	const bool bReuse = CVarVotionSortReuse.GetValueOnRenderThread() != 0 && ViewKey != 0;
	const uint64 CacheKey = (uint64(ViewKey) << 8) | uint8(View.StereoViewIndex);
	FViewSortCache* Cache = bReuse ? &SortCache.FindOrAdd(CacheKey) : nullptr;
	const bool bSameAxis = Cache && Cache->Sorted.IsValid() && Cache->N == Count
		&& FMemory::Memcmp(&Cache->LastAxis, &DepthAxis, sizeof(FVector3f)) == 0;

	FRDGBufferRef Sorted = nullptr;
	const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();
	if (bSameAxis)
	{
		Sorted = GraphBuilder.RegisterExternalBuffer(Cache->Sorted, TEXT("VotionSplat.Sorted"));
		Cache->LastFrame = Frame;
	}
	else
	{
		Sorted = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), uint32(Count)), TEXT("VotionSplat.Sorted"), ERDGBufferFlags::MultiFrame);
		FVotionSortPassParams* SortPass = GraphBuilder.AllocParameters<FVotionSortPassParams>();
		SortPass->SortedOut = GraphBuilder.CreateUAV(Sorted, PF_R32_UINT);
		TSharedPtr<FVotionSortScratch, ESPMode::ThreadSafe> ScratchRef = Scratch;
		FShaderResourceViewRHIRef SplatSRV = GPU->SplatSRV;
		GraphBuilder.AddPass(RDG_EVENT_NAME("VotionSplat.Sort"), SortPass,
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull | ERDGPassFlags::NeverParallel,
			[ScratchRef, SplatSRV, DepthAxis, Count, FeatureLevel, SortPass](FRHICommandList& RHICmdList)
			{
				RHICmdList.Transition({
					FRHITransitionInfo(ScratchRef->KeyUAV[0], ERHIAccess::Unknown, ERHIAccess::UAVCompute),
					FRHITransitionInfo(ScratchRef->ValueUAV[0], ERHIAccess::Unknown, ERHIAccess::UAVCompute)
				});
				TShaderMapRef<FVotionSortKeysCS> Shader(GetGlobalShaderMap(FeatureLevel));
				FVotionSortKeysCS::FParameters Keys;
				Keys.NumSplats = uint32(Count);
				Keys.DepthAxis = DepthAxis;
				Keys.SplatData = SplatSRV;
				Keys.OutKeys = ScratchRef->KeyUAV[0];
				Keys.OutValues = ScratchRef->ValueUAV[0];
				FComputeShaderUtils::Dispatch(RHICmdList, Shader, Keys, FIntVector(FMath::DivideAndRoundUp(Count, 256), 1, 1));

				FGPUSortBuffers Buffers;
				for (int32 Index = 0; Index < 2; ++Index)
				{
					Buffers.RemoteKeySRVs[Index] = ScratchRef->KeySRV[Index];
					Buffers.RemoteKeyUAVs[Index] = ScratchRef->KeyUAV[Index];
					Buffers.RemoteValueSRVs[Index] = ScratchRef->ValueSRV[Index];
					Buffers.RemoteValueUAVs[Index] = ScratchRef->ValueUAV[Index];
				}
				Buffers.FinalValuesUAV = SortPass->SortedOut->GetRHI();
				SortGPUBuffers(RHICmdList, Buffers, 0, 0xFFFFFFFFu, Count, FeatureLevel);
			});
		if (Cache)
		{
			Cache->Sorted = GraphBuilder.ConvertToExternalBuffer(Sorted);
			Cache->LastAxis = DepthAxis;
			Cache->N = Count;
			Cache->LastFrame = Frame;
		}
	}

	FVotionRasterPassParams* Raster = GraphBuilder.AllocParameters<FVotionRasterPassParams>();
	Raster->VS.Projected = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Projected));
	Raster->VS.SortedIndices = GraphBuilder.CreateSRV(Sorted, PF_R32_UINT);
	Raster->VS.ViewSize = Params.ViewSize;
	Raster->PS.SceneDepth = Params.SceneDepth;
	Raster->PS.DepthRectMin = Params.DepthRectMin;
	Raster->PS.DepthScale = Params.DepthScale;
	Raster->PS.bDepthTest = Params.SceneDepth ? Params.bDepthTest : 0;
	Raster->PS.AlphaMax = bLite ? (255.f / 256.f) : 0.999f;
	Raster->PS.AlphaMin = bLite ? (1.f / 256.f) : (1.f / 255.f);
	Raster->PS.bDebugQuads = Params.bDebugQuads;
	Raster->RenderTargets[0] = FRenderTargetBinding(Accum, ERenderTargetLoadAction::ELoad);

	const FIntPoint Size(int32(Params.ViewSize.X), int32(Params.ViewSize.Y));
	GraphBuilder.AddPass(RDG_EVENT_NAME("VotionSplat.Raster"), Raster, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull,
		[Raster, FeatureLevel, Size, Count](FRHICommandList& RHICmdList)
		{
			TShaderMapRef<FVotionRasterVS> VS(GetGlobalShaderMap(FeatureLevel));
			TShaderMapRef<FVotionRasterPS> PS(GetGlobalShaderMap(FeatureLevel));
			RHICmdList.SetViewport(0, 0, 0.f, Size.X, Size.Y, 1.f);
			FGraphicsPipelineStateInitializer PSO;
			RHICmdList.ApplyCachedRenderTargets(PSO);
			PSO.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
			PSO.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			PSO.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			PSO.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
			PSO.BoundShaderState.VertexShaderRHI = VS.GetVertexShader();
			PSO.BoundShaderState.PixelShaderRHI = PS.GetPixelShader();
			PSO.PrimitiveType = PT_TriangleStrip;
			SetGraphicsPipelineState(RHICmdList, PSO, 0);
			SetShaderParameters(RHICmdList, VS, VS.GetVertexShader(), Raster->VS);
			SetShaderParameters(RHICmdList, PS, PS.GetPixelShader(), Raster->PS);
			RHICmdList.DrawPrimitive(0, 2, uint32(Count));
		});
}
