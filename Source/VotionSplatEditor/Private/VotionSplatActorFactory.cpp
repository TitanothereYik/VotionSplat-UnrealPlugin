#include "VotionSplatActorFactory.h"

#include "VotionSplatActor.h"
#include "VotionSplatAsset.h"
#include "VotionSplatComponent.h"

#define LOCTEXT_NAMESPACE "VotionSplat"

UVotionSplatActorFactory::UVotionSplatActorFactory()
{
	DisplayName = LOCTEXT("ActorFactory", "Votion Splat");
	NewActorClass = AVotionSplatActor::StaticClass();
	bUseSurfaceOrientation = false;
}

bool UVotionSplatActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
	return AssetData.IsInstanceOf(UVotionSplatAsset::StaticClass());
}

void UVotionSplatActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
	Super::PostSpawnActor(Asset, NewActor);
	if (AVotionSplatActor* Actor = Cast<AVotionSplatActor>(NewActor))
	{
		Actor->SplatComponent->Modify();
		Actor->SplatComponent->SplatAsset = Cast<UVotionSplatAsset>(Asset);
		Actor->SplatComponent->MarkRenderStateDirty();
	}
}

UObject* UVotionSplatActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
	if (const AVotionSplatActor* Actor = Cast<AVotionSplatActor>(ActorInstance))
	{
		return Actor->SplatComponent ? Actor->SplatComponent->SplatAsset : nullptr;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
