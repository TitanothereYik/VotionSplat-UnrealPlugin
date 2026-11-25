#include "VotionSplatViewExtension.h"

#include "VotionSplatLog.h"
#include "VotionSplatLut.h"
#include "VotionSplatRegistry.h"
#include "VotionSplatSceneProxy.h"
#include "VotionSplatShaders.h"

#include "ColorManagement/ColorSpace.h"
#include "HAL/IConsoleManager.h"
#include "PixelShaderUtils.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIStaticStates.h"
#include "SceneRendering.h"
#include "SceneTexturesConfig.h"
#include "ScreenPass.h"
#include "SystemTextures.h"
#include "ProfilingDebugging/RealtimeGPUProfiler.h"

DECLARE_GPU_STAT_NAMED(VotionSplat, TEXT("VotionSplat"));

static TAutoConsoleVariable<int32> CVarVotionEnable(TEXT("r.VotionSplat.Enable"), 1, TEXT("Draw Votion splats."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> CVarVotionDepth(TEXT("r.VotionSplat.DepthTest"), 1, TEXT("Occlude splats with scene depth."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> CVarVotionMode(TEXT("r.VotionSplat.CompositeMode"), -1, TEXT("0 display, 1 display-linear, 2 scene-linear. -1 automatic."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> CVarVotionQuads(TEXT("r.VotionSplat.DebugQuads"), 0, TEXT("Draw the splat quads as flat grey."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> CVarVotionLog(TEXT("r.VotionSplat.LogViews"), 0, TEXT("Log the composite mode once per second."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> CVarVotionAccum(TEXT("r.VotionSplat.AccumFormat"), 1, TEXT("Accumulation target. 0 RGBA16F, 1 RGBA32F. 16-bit is faster and fails the alpine_01 image gate, so 32-bit stays the default."), ECVF_RenderThreadSafe);

static void WarnOnce(const TCHAR* Text)
{
	static bool bWarned = false;
	if (!bWarned)
	{
		bWarned = true;
		UE_LOG(LogVotionSplat, Warning, TEXT("%s"), Text);
	}
}

static void WorkingRows(FVector4f& Row0, FVector4f& Row1, FVector4f& Row2)
{
	static FVector4f Cached[3];
	static bool bReady = false;
	if (!bReady)
	{
		const UE::Color::FColorSpaceTransform Transform(
			UE::Color::FColorSpace(UE::Color::EColorSpace::sRGB), UE::Color::FColorSpace::GetWorking());
		const FLinearColor CX = Transform.Apply(FLinearColor(1, 0, 0));
		const FLinearColor CY = Transform.Apply(FLinearColor(0, 1, 0));
		const FLinearColor CZ = Transform.Apply(FLinearColor(0, 0, 1));
		Cached[0] = FVector4f(CX.R, CY.R, CZ.R, 0);
		Cached[1] = FVector4f(CX.G, CY.G, CZ.G, 0);
		Cached[2] = FVector4f(CX.B, CY.B, CZ.B, 0);
		bReady = true;
	}
	Row0 = Cached[0];
	Row1 = Cached[1];
	Row2 = Cached[2];
}

static bool IsSupportedView(const FSceneView& View)
{
	return View.IsPerspectiveProjection()
		&& !View.bIsReflectionCapture
		&& !View.bIsPlanarReflection
		&& !View.bIsSceneCapture
		&& View.Family
		&& View.Family->EngineShowFlags.Rendering
		&& !View.Family->EngineShowFlags.Wireframe;
}

static uint32 DetermineMode(const FSceneView& View)
{
	const int32 Forced = CVarVotionMode.GetValueOnRenderThread();
	if (Forced >= 0)
	{
		return uint32(Forced);
	}
	const FSceneViewFamily& Family = *View.Family;
	if (Family.SceneCaptureSource == SCS_FinalColorHDR)
	{
		return GVotionInverseViewLut.bValid ? 2u : 1u;
	}
	if (Family.SceneCaptureSource == SCS_FinalToneCurveHDR)
	{
		return 1u;
	}
	if (Family.RenderTarget)
	{
		const EDisplayOutputFormat Format = Family.RenderTarget->GetDisplayOutputFormat();
		if (Format != EDisplayOutputFormat::SDR_sRGB && Format != EDisplayOutputFormat::SDR_Rec709
			&& Format != EDisplayOutputFormat::SDR_ExplicitGammaMapping)
		{
			WarnOnce(TEXT("HDR output is not supported by VotionSplat-UnrealPlugin; drawing as SDR"));
		}
	}
	return 0u;
}

static FVotionViewParams BuildViewParams(FRDGBuilder& GraphBuilder, const FSceneView& View, const FScreenPassTexture& SceneColor, const FPostProcessMaterialInputs& Inputs)
{
	FVotionViewParams Params;
	const FIntPoint Size = SceneColor.ViewRect.Size();
	Params.ViewSize = FVector2f(Size);
	const FMatrix& P = View.ViewMatrices.GetProjectionNoAAMatrix();
	const float W = float(Size.X);
	const float H = float(Size.Y);
	Params.Intrinsics = FVector4f(0.5f * W * P.M[0][0], 0.5f * H * P.M[1][1], 0.5f * W * (1.f + P.M[2][0]), 0.5f * H * (1.f - P.M[2][1]));
	Params.ViewToClip = FMatrix44f(P);
	Params.bDepthTest = CVarVotionDepth.GetValueOnRenderThread() != 0 ? 1u : 0u;
	Params.bDebugQuads = CVarVotionQuads.GetValueOnRenderThread() != 0 ? 1u : 0u;
	const FViewInfo& Info = static_cast<const FViewInfo&>(View);
	Params.DepthRectMin = Info.ViewRect.Min;
	const FVector2f DepthSize(Info.ViewRect.Size());
	Params.DepthScale = FVector2f(Size.X > 0 && Size.Y > 0 ? DepthSize.X / Size.X : 1.f, Size.X > 0 && Size.Y > 0 ? DepthSize.Y / Size.Y : 1.f);
	if (const TRDGUniformBuffer<FSceneTextureUniformParameters>* Textures = Inputs.SceneTextures.SceneTextures.GetUniformBuffer())
	{
		Params.SceneDepth = Textures->GetContents()->SceneDepthTexture;
	}
	if (!Params.SceneDepth)
	{
		Params.SceneDepth = GSystemTextures.GetBlackDummy(GraphBuilder);
		Params.bDepthTest = 0;
	}
	else if (Params.bDepthTest)
	{
		// A high-resolution screenshot can shade an upscaled color target while the scene-depth
		// texture still belongs to the editor view. Comparing those rejects splats that nothing occludes.
		const FIntPoint DepthExtent = Params.SceneDepth->Desc.Extent;
		const FIntPoint ViewExtent(
			FMath::RoundToInt(Params.DepthScale.X * float(Size.X)),
			FMath::RoundToInt(Params.DepthScale.Y * float(Size.Y)));
		if (FMath::Abs(DepthExtent.X - ViewExtent.X) > 8 || FMath::Abs(DepthExtent.Y - ViewExtent.Y) > 8)
		{
			Params.bDepthTest = 0;
		}
	}
	return Params;
}

void FVotionSplatViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled)
{
	if (Pass == EPostProcessingPass::Tonemap)
	{
		InOutPassCallbacks.Add(FAfterPassCallbackDelegate::CreateRaw(this, &FVotionSplatViewExtension::AfterTonemap_RenderThread));
	}
}

FScreenPassTexture FVotionSplatViewExtension::AfterTonemap_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs)
{
	FScreenPassTexture SceneColor = FScreenPassTexture::CopyFromSlice(GraphBuilder, Inputs.GetInput(EPostProcessMaterialInput::SceneColor));
	TArray<FVotionSplatSceneProxy*> Proxies;
	if (CVarVotionEnable.GetValueOnRenderThread() != 0 && IsSupportedView(View) && SceneColor.IsValid())
	{
		FVotionSplatRegistry::Get().GatherVisible(View, Proxies);
	}
	if (Proxies.Num() == 0 || !SceneColor.IsValid())
	{
		return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
	}

	RDG_EVENT_SCOPE(GraphBuilder, "VotionSplat");
	RDG_GPU_STAT_SCOPE(GraphBuilder, VotionSplat);
	const FIntPoint Size = SceneColor.ViewRect.Size();
	EPixelFormat AccumFormat = PF_FloatRGBA;
	if (CVarVotionAccum.GetValueOnRenderThread() == 1
		&& UE::PixelFormat::HasCapabilities(PF_A32B32G32R32F, EPixelFormatCapabilities::RenderTarget | EPixelFormatCapabilities::TextureBlendable))
	{
		AccumFormat = PF_A32B32G32R32F;
	}
	FRDGTextureRef Accum = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(Size, AccumFormat, FClearValueBinding::Transparent, TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV),
		TEXT("VotionSplat.Accum"));
	AddClearRenderTargetPass(GraphBuilder, Accum, FLinearColor::Transparent);

	const FVotionViewParams Params = BuildViewParams(GraphBuilder, View, SceneColor, Inputs);
	if (CVarVotionLog.GetValueOnRenderThread() != 0)
	{
		static uint32 LastLog = 0;
		const uint32 Frame = GFrameNumberRenderThread;
		static int32 LastW = 0;
		static int32 LastH = 0;
		if (Frame - LastLog > 60 || Size.X != LastW || Size.Y != LastH)
		{
			LastLog = Frame;
			LastW = Size.X;
			LastH = Size.Y;
			const FIntPoint DepthSize = Params.SceneDepth ? Params.SceneDepth->Desc.Extent : FIntPoint::ZeroValue;
			UE_LOG(LogVotionSplat, Log, TEXT("mode %u size %dx%d depth %dx%d rect %d,%d scale %.3f,%.3f proxies %d fx %.1f fy %.1f"),
				DetermineMode(View), Size.X, Size.Y, DepthSize.X, DepthSize.Y, Params.DepthRectMin.X, Params.DepthRectMin.Y,
				Params.DepthScale.X, Params.DepthScale.Y, Proxies.Num(), Params.Intrinsics.X, Params.Intrinsics.Y);
		}
	}
	for (int32 Index = 0; Index < Proxies.Num(); ++Index)
	{
		Proxies[Index]->RenderSplats(GraphBuilder, View, Params, Accum, Index == 0);
	}

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;
	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, SceneColor, View.GetOverwriteLoadAction(), TEXT("VotionSplat.Output"));
	}
	FVotionCompositePS::FParameters* Pass = GraphBuilder.AllocParameters<FVotionCompositePS::FParameters>();
	Pass->SceneColorTex = SceneColor.Texture;
	Pass->AccumTex = Accum;
	Pass->OutputRectMin = Output.ViewRect.Min;
	Pass->SceneRectMin = SceneColor.ViewRect.Min;
	Pass->Mode = DetermineMode(View);
	Pass->bOutputIsSrgbTexture = Output.Texture && EnumHasAnyFlags(Output.Texture->Desc.Flags, TexCreate_SRGB) ? 1u : 0u;
	Pass->InverseViewLut = GVotionInverseViewLut.Texture;
	Pass->LutSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Pass->LutSize = float(FMath::Max(GVotionInverseViewLut.Size, 1));
	WorkingRows(Pass->Rec709ToWorking0, Pass->Rec709ToWorking1, Pass->Rec709ToWorking2);
	Pass->RenderTargets[0] = Output.GetRenderTargetBinding();
	TShaderMapRef<FVotionCompositePS> Shader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FPixelShaderUtils::AddFullscreenPass(GraphBuilder, GetGlobalShaderMap(View.GetFeatureLevel()), RDG_EVENT_NAME("VotionSplat.Composite"), Shader, Pass, Output.ViewRect);
	return FScreenPassTexture(Output);
}
