#include "VotionSplatSceneProxy.h"

#include "VotionSplatAsset.h"
#include "VotionSplatComponent.h"
#include "VotionSplatLog.h"
#include "VotionSplatRegistry.h"
#include "VotionSplatShaders.h"

#include "CommonRenderResources.h"
#include "GPUSort.h"
#include "HAL/IConsoleManager.h"
#include "PipelineStateCache.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "SceneManagement.h"
#include "SceneView.h"

static TAutoConsoleVariable<int32> CVarVotionSortReuse(
	TEXT("r.VotionSplat.SortReuse"),
	1,
	TEXT("Reuse the sorted splat indices while the view direction is unchanged."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVotionStats(
	TEXT("r.VotionSplat.Stats"),
	0,
	TEXT("Log how many views, projections, sorts and compactions ran, every 120 frames."),
	ECVF_RenderThreadSafe);

struct FVotionPassStats
{
	uint32 Views = 0;
	uint32 Project = 0;
	uint32 Sort = 0;
	uint32 Compact = 0;
	uint32 Tile = 0;
	uint32 LastLog = 0;
};
static FVotionPassStats GVotionStats;

static TAutoConsoleVariable<int32> CVarVotionViewCache(
	TEXT("r.VotionSplat.ViewCache"),
	1,
	TEXT("Reuse projected splats while the view, the actor transform and the viewport size are bitwise unchanged."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarVotionTileRaster(
	TEXT("r.VotionSplat.TileRaster"),
	1,
	TEXT("1 blends splats front to back in 16x16 compute tiles and stops a pixel once it is opaque. 0 draws instanced quads with hardware blending."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarVotionTileStopT(
	TEXT("r.VotionSplat.TileStopT"),
	1.0e-4f,
	TEXT("The tile rasterizer stops a pixel when its transmittance would fall to this value (gsplat uses 1e-4). 0 blends every splat."),
	ECVF_RenderThreadSafe);

static constexpr uint32 GVotionMinPairs = 1u << 22;
static constexpr uint32 GVotionMaxPairs = 1u << 26;

static void AddExclusiveScan(FRDGBuilder& GraphBuilder, FGlobalShaderMap* ShaderMap, FRDGBufferRef In, FRDGBufferRef Out,
	uint32 NumItems, FRDGBufferRef Totals, uint32 Capacity)
{
	const uint32 NumGroups = FMath::DivideAndRoundUp(NumItems, 1024u);
	check(NumGroups > 0 && NumGroups <= 65535u);
	FRDGBufferRef Sums = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumGroups), TEXT("VotionSplat.ScanSums"));
	FRDGBufferRef Offsets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumGroups), TEXT("VotionSplat.ScanOffsets"));
	{
		FVotionScanReduceCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionScanReduceCS::FParameters>();
		Pass->NumItems = NumItems;
		Pass->ScanIn = GraphBuilder.CreateSRV(In, PF_R32_UINT);
		Pass->GroupSums = GraphBuilder.CreateUAV(Sums, PF_R32_UINT);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.ScanReduce"), TShaderMapRef<FVotionScanReduceCS>(ShaderMap), Pass,
			FIntVector(NumGroups, 1, 1));
	}
	{
		FVotionScanGroupsCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionScanGroupsCS::FParameters>();
		Pass->NumGroups = NumGroups;
		Pass->Capacity = Capacity;
		Pass->GroupSumsIn = GraphBuilder.CreateSRV(Sums, PF_R32_UINT);
		Pass->GroupOffsets = GraphBuilder.CreateUAV(Offsets, PF_R32_UINT);
		Pass->TotalsOut = GraphBuilder.CreateUAV(Totals, PF_R32_UINT);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.ScanGroups"), TShaderMapRef<FVotionScanGroupsCS>(ShaderMap), Pass,
			FIntVector(1, 1, 1));
	}
	{
		FVotionScanWriteCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionScanWriteCS::FParameters>();
		Pass->NumItems = NumItems;
		Pass->ScanIn = GraphBuilder.CreateSRV(In, PF_R32_UINT);
		Pass->GroupOffsetsIn = GraphBuilder.CreateSRV(Offsets, PF_R32_UINT);
		Pass->ScanOut = GraphBuilder.CreateUAV(Out, PF_R32_UINT);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.ScanWrite"), TShaderMapRef<FVotionScanWriteCS>(ShaderMap), Pass,
			FIntVector(NumGroups, 1, 1));
	}
}

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

void FVotionSplatSceneProxy::UpdatePairCapacity(int32 Count)
{
	if (PairCapacity == 0)
	{
		PairCapacity = FMath::Clamp(FMath::RoundUpToPowerOfTwo(uint32(Count) * 4u), GVotionMinPairs, GVotionMaxPairs);
	}
	if (!bPairReadbackPending || !PairReadback.IsValid() || !PairReadback->IsReady())
	{
		return;
	}
	const uint32* Data = static_cast<const uint32*>(PairReadback->Lock(2 * sizeof(uint32)));
	LastPairs = Data[0];
	PairReadback->Unlock();
	bPairReadbackPending = false;
	const uint64 Wanted = uint64(LastPairs) + LastPairs / 4;
	if (Wanted > PairCapacity && PairCapacity < GVotionMaxPairs)
	{
		const uint32 Grown = uint32(FMath::Min<uint64>(FMath::RoundUpToPowerOfTwo64(Wanted), GVotionMaxPairs));
		UE_LOG(LogVotionSplat, Log, TEXT("Tile pairs %u need more room than %u; growing to %u."), LastPairs, PairCapacity, Grown);
		PairCapacity = Grown;
	}
	else if (LastPairs > GVotionMaxPairs)
	{
		static bool bWarned = false;
		if (!bWarned)
		{
			bWarned = true;
			UE_LOG(LogVotionSplat, Warning, TEXT("Tile pairs %u exceed the %u limit; this view draws with quads."), LastPairs, GVotionMaxPairs);
		}
	}
}

void FVotionSplatSceneProxy::RenderSplats(FRDGBuilder& GraphBuilder, const FSceneView& View, const FVotionViewParams& Params, FRDGTextureRef Accum, bool bFirstInAccum)
{
	if (!GPU.IsValid() || GPU->NumSplats <= 0 || Params.ViewSize.X < 1.f || Params.ViewSize.Y < 1.f)
	{
		return;
	}
	++GVotionStats.Views;
	EnsureScratch(GraphBuilder.RHICmdList);
	const int32 Count = GPU->NumSplats;
	const FMatrix LocalToViewD = GetLocalToWorld() * View.ViewMatrices.GetViewMatrix();
	const FMatrix44f LocalToView(LocalToViewD);
	const FVector CamLocal = GetLocalToWorld().InverseFast().TransformPosition(View.ViewMatrices.GetViewOrigin());
	const float PlyToCm = 100.f * FMath::Max(GetLocalToWorld().GetScaleVector().GetAbsMax(), KINDA_SMALL_NUMBER);
	const FVector3f DepthAxis(LocalToView.M[0][2], LocalToView.M[1][2], LocalToView.M[2][2]);
	const bool bLite = GPU->Profile == 2;
	const float NearCm = GPU->NearPlanePly * PlyToCm;

	const uint32 Frame = GFrameNumberRenderThread;
	for (TMap<uint64, FViewSortCache>::TIterator It(SortCache); It; ++It)
	{
		if (Frame - It.Value().LastFrame > 300)
		{
			It.RemoveCurrent();
		}
	}
	const uint32 ViewKey = View.GetViewKey();
	const bool bUseCache = ViewKey != 0 && (CVarVotionSortReuse.GetValueOnRenderThread() != 0 || CVarVotionViewCache.GetValueOnRenderThread() != 0);
	const uint64 CacheKey = (uint64(ViewKey) << 8) | uint8(View.StereoViewIndex);
	FViewSortCache* Cache = bUseCache ? &SortCache.FindOrAdd(CacheKey) : nullptr;

	FProjectKey Key;
	FMemory::Memzero(Key);
	Key.Gpu = GPU.Get();
	Key.Profile = GPU->Profile;
	Key.N = Count;
	Key.ViewToClip = Params.ViewToClip;
	Key.Intrinsics = Params.Intrinsics;
	Key.ViewSize = Params.ViewSize;
	Key.CamLocal = FVector3f(CamLocal);
	Key.NearCm = NearCm;
	Key.LocalToView = LocalToView;
	const bool bSameProject = Cache && CVarVotionViewCache.GetValueOnRenderThread() != 0 && Cache->Projected.IsValid()
		&& FMemory::Memcmp(&Cache->ProjectKey, &Key, sizeof(FProjectKey)) == 0;
	const bool bSameAxis = Cache && CVarVotionSortReuse.GetValueOnRenderThread() != 0 && Cache->Sorted.IsValid() && Cache->N == Count
		&& FMemory::Memcmp(&Cache->LastAxis, &DepthAxis, sizeof(FVector3f)) == 0;
	const bool bSameDraw = Cache && bSameProject && bSameAxis && Cache->DrawRecords.IsValid() && Cache->DrawArgs.IsValid();

	FRDGBufferRef Projected = nullptr;
	FRDGBufferRef Sorted = nullptr;
	FRDGBufferRef DrawRecords = nullptr;
	FRDGBufferRef DrawArgs = nullptr;
	const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();
	if (bSameDraw)
	{
		DrawRecords = GraphBuilder.RegisterExternalBuffer(Cache->DrawRecords, TEXT("VotionSplat.DrawRecords"));
		DrawArgs = GraphBuilder.RegisterExternalBuffer(Cache->DrawArgs, TEXT("VotionSplat.DrawArgs"));
		Cache->LastFrame = Frame;
	}
	else
	{
		if (Cache)
		{
			Cache->DrawRecords.SafeRelease();
			Cache->DrawArgs.SafeRelease();
		}
		if (bSameProject)
		{
			Projected = GraphBuilder.RegisterExternalBuffer(Cache->Projected, TEXT("VotionSplat.Projected"));
			Cache->LastFrame = Frame;
		}
		else
		{
			Projected = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), uint32(Count) * 4), TEXT("VotionSplat.Projected"), ERDGBufferFlags::MultiFrame);
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
			Pass->NearCm = NearCm;
			Pass->FarCm = 1.0e10f;
			Pass->SplatData = GPU->SplatSRV;
			Pass->ShRest = GPU->ShSRV;
			Pass->Projected = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Projected));
			TShaderMapRef<FVotionProjectCS> Shader(GetGlobalShaderMap(FeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.Project"), Shader, Pass,
				FIntVector(FMath::DivideAndRoundUp(Count, 256), 1, 1));
			++GVotionStats.Project;
			if (Cache)
			{
				Cache->Projected = GraphBuilder.ConvertToExternalBuffer(Projected);
				Cache->ProjectKey = Key;
				Cache->LastFrame = Frame;
			}
		}

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
			++GVotionStats.Sort;
			if (Cache)
			{
				Cache->Sorted = GraphBuilder.ConvertToExternalBuffer(Sorted);
				Cache->LastAxis = DepthAxis;
				Cache->N = Count;
				Cache->LastFrame = Frame;
			}
		}

		const uint32 NumGroups = FMath::DivideAndRoundUp(uint32(Count), 1024u);
		check(NumGroups <= 65535u);
		FRDGBufferRef GroupCounts = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumGroups), TEXT("VotionSplat.GroupCounts"));
		FRDGBufferRef GroupOffsets = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumGroups), TEXT("VotionSplat.GroupOffsets"));
		DrawArgs = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("VotionSplat.DrawArgs"), ERDGBufferFlags::MultiFrame);
		DrawRecords = GraphBuilder.CreateBuffer(
			FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector4f), uint32(Count) * 4), TEXT("VotionSplat.DrawRecords"), ERDGBufferFlags::MultiFrame);

		{
			FVotionCompactCountCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionCompactCountCS::FParameters>();
			Pass->NumSplats = uint32(Count);
			Pass->Projected = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Projected));
			Pass->SortedIndices = GraphBuilder.CreateSRV(Sorted, PF_R32_UINT);
			Pass->GroupCounts = GraphBuilder.CreateUAV(GroupCounts, PF_R32_UINT);
			TShaderMapRef<FVotionCompactCountCS> Shader(GetGlobalShaderMap(FeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.CompactCount"), Shader, Pass, FIntVector(NumGroups, 1, 1));
		}
		{
			FVotionCompactScanCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionCompactScanCS::FParameters>();
			Pass->NumGroups = NumGroups;
			Pass->GroupCountsIn = GraphBuilder.CreateSRV(GroupCounts, PF_R32_UINT);
			Pass->GroupOffsets = GraphBuilder.CreateUAV(GroupOffsets, PF_R32_UINT);
			Pass->DrawArgs = GraphBuilder.CreateUAV(DrawArgs, PF_R32_UINT);
			TShaderMapRef<FVotionCompactScanCS> Shader(GetGlobalShaderMap(FeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.CompactScan"), Shader, Pass, FIntVector(1, 1, 1));
		}
		{
			FVotionCompactWriteCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionCompactWriteCS::FParameters>();
			Pass->NumSplats = uint32(Count);
			Pass->Projected = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Projected));
			Pass->SortedIndices = GraphBuilder.CreateSRV(Sorted, PF_R32_UINT);
			Pass->GroupOffsetsIn = GraphBuilder.CreateSRV(GroupOffsets, PF_R32_UINT);
			Pass->DrawRecords = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(DrawRecords));
			TShaderMapRef<FVotionCompactWriteCS> Shader(GetGlobalShaderMap(FeatureLevel));
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.CompactWrite"), Shader, Pass, FIntVector(NumGroups, 1, 1));
		}
		++GVotionStats.Compact;
		if (Cache)
		{
			Cache->DrawRecords = GraphBuilder.ConvertToExternalBuffer(DrawRecords);
			Cache->DrawArgs = GraphBuilder.ConvertToExternalBuffer(DrawArgs);
			Cache->LastFrame = Frame;
		}
	}

	const FIntPoint Size(int32(Params.ViewSize.X), int32(Params.ViewSize.Y));
	const float AlphaMax = bLite ? (255.f / 256.f) : 0.999f;
	const float AlphaMin = bLite ? (1.f / 256.f) : (1.f / 255.f);
	const uint32 DepthTest = Params.SceneDepth ? Params.bDepthTest : 0;
	const bool bTile = bFirstInAccum && CVarVotionTileRaster.GetValueOnRenderThread() != 0 && Params.bDebugQuads == 0
		&& EnumHasAnyFlags(Accum->Desc.Flags, TexCreate_UAV);
	FRDGBufferRef QuadArgs = DrawArgs;
	if (bTile)
	{
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(FeatureLevel);
		UpdatePairCapacity(Count);
		const uint32 Capacity = PairCapacity;
		const uint32 TilesX = FMath::DivideAndRoundUp(uint32(Size.X), 16u);
		const uint32 TilesY = FMath::DivideAndRoundUp(uint32(Size.Y), 16u);
		const uint32 NumTiles = TilesX * TilesY;
		const bool bSameTiles = bSameDraw && Cache->TileCapacity == Capacity && Cache->TileValues.IsValid() && Cache->TileStart.IsValid()
			&& Cache->TileEnd.IsValid() && Cache->TileTotals.IsValid() && Cache->TileHwArgs.IsValid();
		FRDGBufferRef TileValues = nullptr;
		FRDGBufferRef TileStart = nullptr;
		FRDGBufferRef TileEnd = nullptr;
		FRDGBufferRef Totals = nullptr;
		FRDGBufferRef HwArgs = nullptr;
		if (bSameTiles)
		{
			TileValues = GraphBuilder.RegisterExternalBuffer(Cache->TileValues, TEXT("VotionSplat.TileValues"));
			TileStart = GraphBuilder.RegisterExternalBuffer(Cache->TileStart, TEXT("VotionSplat.TileStart"));
			TileEnd = GraphBuilder.RegisterExternalBuffer(Cache->TileEnd, TEXT("VotionSplat.TileEnd"));
			Totals = GraphBuilder.RegisterExternalBuffer(Cache->TileTotals, TEXT("VotionSplat.TileTotals"));
			HwArgs = GraphBuilder.RegisterExternalBuffer(Cache->TileHwArgs, TEXT("VotionSplat.TileHwArgs"));
		}
		else
		{
			RDG_EVENT_SCOPE(GraphBuilder, "VotionSplat.TileBuild");
			++GVotionStats.Tile;
			FRDGBufferSRVRef DrawArgsSRV = GraphBuilder.CreateSRV(DrawArgs, PF_R32_UINT);
			FRDGBufferRef PairCounts = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), uint32(Count)), TEXT("VotionSplat.PairCounts"));
			FRDGBufferRef PairOffsets = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), uint32(Count)), TEXT("VotionSplat.PairOffsets"));
			Totals = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 4), TEXT("VotionSplat.TileTotals"), ERDGBufferFlags::MultiFrame);
			FRDGBufferRef Keys[2];
			FRDGBufferRef Vals[2];
			for (int32 Index = 0; Index < 2; ++Index)
			{
				Keys[Index] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Capacity), TEXT("VotionSplat.PairKeys"));
				Vals[Index] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Capacity), TEXT("VotionSplat.PairValues"),
					ERDGBufferFlags::MultiFrame);
			}
			const FIntVector SplatGroups(FMath::DivideAndRoundUp(Count, 256), 1, 1);
			{
				FVotionTileCountCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionTileCountCS::FParameters>();
				Pass->NumSplats = uint32(Count);
				Pass->ViewSizeI = Size;
				Pass->DrawArgsIn = DrawArgsSRV;
				Pass->DrawRecords = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(DrawRecords));
				Pass->PairCounts = GraphBuilder.CreateUAV(PairCounts, PF_R32_UINT);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.TileCount"), TShaderMapRef<FVotionTileCountCS>(ShaderMap), Pass, SplatGroups);
			}
			AddExclusiveScan(GraphBuilder, ShaderMap, PairCounts, PairOffsets, uint32(Count), Totals, Capacity);
			{
				FVotionTileEmitCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionTileEmitCS::FParameters>();
				Pass->NumSplats = uint32(Count);
				Pass->TilesX = TilesX;
				Pass->ViewSizeI = Size;
				Pass->PairCapacity = Capacity;
				Pass->DrawArgsIn = DrawArgsSRV;
				Pass->DrawRecords = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(DrawRecords));
				Pass->PairOffsetsIn = GraphBuilder.CreateSRV(PairOffsets, PF_R32_UINT);
				Pass->KeysOut = GraphBuilder.CreateUAV(Keys[0], PF_R32_UINT);
				Pass->ValsOut = GraphBuilder.CreateUAV(Vals[0], PF_R32_UINT);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.TileEmit"), TShaderMapRef<FVotionTileEmitCS>(ShaderMap), Pass, SplatGroups);
			}

			const uint32 NumBlocks = FMath::DivideAndRoundUp(Capacity, 4096u);
			const uint32 KeyBits = FMath::Max(1u, FMath::CeilLogTwo(NumTiles));
			const uint32 NumPasses = FMath::DivideAndRoundUp(KeyBits, 4u);
			FRDGBufferRef Hist = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 16 * NumBlocks), TEXT("VotionSplat.RadixHist"));
			FRDGBufferRef HistOffsets = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 16 * NumBlocks), TEXT("VotionSplat.RadixOffsets"));
			FRDGBufferRef HistTotals = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 4), TEXT("VotionSplat.RadixTotals"));
			FRDGBufferSRVRef TotalsSRV = GraphBuilder.CreateSRV(Totals, PF_R32_UINT);
			int32 Src = 0;
			for (uint32 PassIndex = 0; PassIndex < NumPasses; ++PassIndex)
			{
				const int32 Dst = 1 - Src;
				{
					FVotionRadixHistCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionRadixHistCS::FParameters>();
					Pass->Shift = PassIndex * 4;
					Pass->NumBlocks = NumBlocks;
					Pass->PairCapacity = Capacity;
					Pass->TotalsIn = TotalsSRV;
					Pass->KeysIn = GraphBuilder.CreateSRV(Keys[Src], PF_R32_UINT);
					Pass->Hist = GraphBuilder.CreateUAV(Hist, PF_R32_UINT);
					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.RadixHist"), TShaderMapRef<FVotionRadixHistCS>(ShaderMap), Pass,
						FIntVector(NumBlocks, 1, 1));
				}
				AddExclusiveScan(GraphBuilder, ShaderMap, Hist, HistOffsets, 16 * NumBlocks, HistTotals, ~0u);
				{
					FVotionRadixScatterCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionRadixScatterCS::FParameters>();
					Pass->Shift = PassIndex * 4;
					Pass->NumBlocks = NumBlocks;
					Pass->PairCapacity = Capacity;
					Pass->TotalsIn = TotalsSRV;
					Pass->KeysIn = GraphBuilder.CreateSRV(Keys[Src], PF_R32_UINT);
					Pass->ValsIn = GraphBuilder.CreateSRV(Vals[Src], PF_R32_UINT);
					Pass->HistOffsetsIn = GraphBuilder.CreateSRV(HistOffsets, PF_R32_UINT);
					Pass->KeysOut = GraphBuilder.CreateUAV(Keys[Dst], PF_R32_UINT);
					Pass->ValsOut = GraphBuilder.CreateUAV(Vals[Dst], PF_R32_UINT);
					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.RadixScatter"), TShaderMapRef<FVotionRadixScatterCS>(ShaderMap), Pass,
						FIntVector(NumBlocks, 1, 1));
				}
				Src = Dst;
			}
			TileValues = Vals[Src];

			TileStart = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumTiles), TEXT("VotionSplat.TileStart"), ERDGBufferFlags::MultiFrame);
			TileEnd = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumTiles), TEXT("VotionSplat.TileEnd"), ERDGBufferFlags::MultiFrame);
			FRDGBufferUAVRef TileStartUAV = GraphBuilder.CreateUAV(TileStart, PF_R32_UINT);
			FRDGBufferUAVRef TileEndUAV = GraphBuilder.CreateUAV(TileEnd, PF_R32_UINT);
			AddClearUAVPass(GraphBuilder, TileStartUAV, 0u);
			AddClearUAVPass(GraphBuilder, TileEndUAV, 0u);
			{
				FVotionTileRangesCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionTileRangesCS::FParameters>();
				Pass->PairCapacity = Capacity;
				Pass->TotalsIn = TotalsSRV;
				Pass->KeysIn = GraphBuilder.CreateSRV(Keys[Src], PF_R32_UINT);
				Pass->TileStart = TileStartUAV;
				Pass->TileEnd = TileEndUAV;
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.TileRanges"), TShaderMapRef<FVotionTileRangesCS>(ShaderMap), Pass,
					FIntVector(FMath::DivideAndRoundUp(Capacity, 2048u), 1, 1));
			}
			HwArgs = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(1), TEXT("VotionSplat.TileHwArgs"),
				ERDGBufferFlags::MultiFrame);
			{
				FVotionTileFinalizeCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionTileFinalizeCS::FParameters>();
				Pass->DrawArgsIn = DrawArgsSRV;
				Pass->TotalsIn = TotalsSRV;
				Pass->HwArgs = GraphBuilder.CreateUAV(HwArgs, PF_R32_UINT);
				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.TileFinalize"), TShaderMapRef<FVotionTileFinalizeCS>(ShaderMap), Pass,
					FIntVector(1, 1, 1));
			}
			if (!bPairReadbackPending)
			{
				if (!PairReadback.IsValid())
				{
					PairReadback = MakeUnique<FRHIGPUBufferReadback>(TEXT("VotionSplat.PairCount"));
				}
				AddEnqueueCopyPass(GraphBuilder, PairReadback.Get(), Totals, 2 * sizeof(uint32));
				bPairReadbackPending = true;
			}
			if (Cache)
			{
				Cache->TileValues = GraphBuilder.ConvertToExternalBuffer(TileValues);
				Cache->TileStart = GraphBuilder.ConvertToExternalBuffer(TileStart);
				Cache->TileEnd = GraphBuilder.ConvertToExternalBuffer(TileEnd);
				Cache->TileTotals = GraphBuilder.ConvertToExternalBuffer(Totals);
				Cache->TileHwArgs = GraphBuilder.ConvertToExternalBuffer(HwArgs);
				Cache->TileCapacity = Capacity;
			}
		}

		FVotionTileRasterCS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionTileRasterCS::FParameters>();
		Pass->TilesX = TilesX;
		Pass->ViewSizeI = Size;
		Pass->TotalsIn = GraphBuilder.CreateSRV(Totals, PF_R32_UINT);
		Pass->TileStartIn = GraphBuilder.CreateSRV(TileStart, PF_R32_UINT);
		Pass->TileEndIn = GraphBuilder.CreateSRV(TileEnd, PF_R32_UINT);
		Pass->PairValues = GraphBuilder.CreateSRV(TileValues, PF_R32_UINT);
		Pass->DrawRecords = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(DrawRecords));
		Pass->SceneDepth = Params.SceneDepth;
		Pass->DepthRectMin = Params.DepthRectMin;
		Pass->DepthScale = Params.DepthScale;
		Pass->bDepthTest = DepthTest;
		Pass->AlphaMax = AlphaMax;
		Pass->AlphaMin = AlphaMin;
		Pass->StopT = FMath::Max(CVarVotionTileStopT.GetValueOnRenderThread(), 0.f);
		Pass->AccumOut = GraphBuilder.CreateUAV(Accum);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VotionSplat.TileRaster"), TShaderMapRef<FVotionTileRasterCS>(ShaderMap), Pass,
			FIntVector(TilesX, TilesY, 1));
		QuadArgs = HwArgs;
	}

	FVotionRasterPassParams* Raster = GraphBuilder.AllocParameters<FVotionRasterPassParams>();
	Raster->VS.DrawRecords = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(DrawRecords));
	Raster->DrawArgs = QuadArgs;
	Raster->VS.ViewSize = Params.ViewSize;
	Raster->PS.SceneDepth = Params.SceneDepth;
	Raster->PS.DepthRectMin = Params.DepthRectMin;
	Raster->PS.DepthScale = Params.DepthScale;
	Raster->PS.bDepthTest = DepthTest;
	Raster->PS.AlphaMax = AlphaMax;
	Raster->PS.AlphaMin = AlphaMin;
	Raster->PS.bDebugQuads = Params.bDebugQuads;
	Raster->RenderTargets[0] = FRenderTargetBinding(Accum, ERenderTargetLoadAction::ELoad);

	GraphBuilder.AddPass(RDG_EVENT_NAME("VotionSplat.Raster"), Raster, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull,
		[Raster, FeatureLevel, Size](FRHICommandList& RHICmdList)
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
			RHICmdList.DrawPrimitiveIndirect(Raster->DrawArgs->GetIndirectRHICallBuffer(), 0);
		});

	if (CVarVotionStats.GetValueOnRenderThread() != 0 && Frame - GVotionStats.LastLog >= 120)
	{
		UE_LOG(LogVotionSplat, Log, TEXT("stats frames %u views %u project %u sort %u compact %u tile %u pairs %u capacity %u"),
			Frame - GVotionStats.LastLog, GVotionStats.Views, GVotionStats.Project, GVotionStats.Sort, GVotionStats.Compact,
			GVotionStats.Tile, LastPairs, PairCapacity);
		GVotionStats = FVotionPassStats();
		GVotionStats.LastLog = Frame;
	}
}
