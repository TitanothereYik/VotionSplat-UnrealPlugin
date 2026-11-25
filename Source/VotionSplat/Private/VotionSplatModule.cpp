#include "VotionSplatModule.h"

#include "VotionSplatLog.h"
#include "VotionSplatViewExtension.h"

#include "Engine/Engine.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "SceneViewExtension.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogVotionSplat);

static TSharedPtr<FVotionSplatViewExtension, ESPMode::ThreadSafe> GVotionViewExtension;

void FVotionSplatModule::StartupModule()
{
	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VotionSplat-UnrealPlugin")); Plugin.IsValid())
	{
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/VotionSplat"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
	}
	else
	{
		UE_LOG(LogVotionSplat, Error, TEXT("VotionSplat-UnrealPlugin descriptor was not found"));
	}
	if (GEngine)
	{
		OnPostEngineInit();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FVotionSplatModule::OnPostEngineInit);
	}
}

void FVotionSplatModule::OnPostEngineInit()
{
	if (!GVotionViewExtension.IsValid())
	{
		GVotionViewExtension = FSceneViewExtensions::NewExtension<FVotionSplatViewExtension>();
	}
}

void FVotionSplatModule::ShutdownModule()
{
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
	GVotionViewExtension.Reset();
}

IMPLEMENT_MODULE(FVotionSplatModule, VotionSplat)
