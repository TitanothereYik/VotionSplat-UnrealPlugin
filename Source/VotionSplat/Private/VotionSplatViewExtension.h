#pragma once

#include "SceneViewExtension.h"
#include "PostProcess/PostProcessMaterialInputs.h"

class FVotionSplatViewExtension : public FSceneViewExtensionBase
{
public:
	explicit FVotionSplatViewExtension(const FAutoRegister& AutoRegister)
		: FSceneViewExtensionBase(AutoRegister)
	{
	}

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual int32 GetPriority() const override { return 110; }
	virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View, FAfterPassCallbackDelegateArray& InOutPassCallbacks, bool bIsPassEnabled) override;

protected:
	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override { return true; }

private:
	FScreenPassTexture AfterTonemap_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessMaterialInputs& Inputs);
};
