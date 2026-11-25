#pragma once

#include "GameFramework/Actor.h"
#include "VotionSplatActor.generated.h"

class UVotionSplatComponent;

UCLASS()
class VOTIONSPLAT_API AVotionSplatActor : public AActor
{
	GENERATED_BODY()

public:
	AVotionSplatActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VotionSplat-UnrealPlugin")
	TObjectPtr<UVotionSplatComponent> SplatComponent;
};
