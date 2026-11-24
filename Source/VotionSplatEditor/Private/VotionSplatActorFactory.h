#pragma once

#include "ActorFactories/ActorFactory.h"
#include "VotionSplatActorFactory.generated.h"

UCLASS()
class VOTIONSPLATEDITOR_API UVotionSplatActorFactory : public UActorFactory
{
	GENERATED_BODY()

public:
	UVotionSplatActorFactory();

	virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
	virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
	virtual UObject* GetAssetFromActorInstance(AActor* ActorInstance) override;
};
