#include "VotionSplatActor.h"

#include "VotionSplatComponent.h"

AVotionSplatActor::AVotionSplatActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SplatComponent = CreateDefaultSubobject<UVotionSplatComponent>(TEXT("Splat"));
	SetRootComponent(SplatComponent);
}
