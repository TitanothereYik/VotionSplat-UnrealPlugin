#pragma once

#include "CoreMinimal.h"

struct FVotionPlyBuild
{
	TArray<FVector4f> SplatData;
	TArray<float> ShRest;
	int32 NumSplats = 0;
	int32 ShDegree = 0;
	FString Raster;
	float NearPlane = 0.01f;
	FBox Bounds = FBox(ForceInit);
};

bool VotionIsVotionPly(const FString& Path);
bool VotionLoadPly(const FString& Path, FVotionPlyBuild& Out, FString& OutError);
