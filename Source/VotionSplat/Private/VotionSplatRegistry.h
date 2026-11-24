#pragma once

#include "CoreMinimal.h"
#include <atomic>

class FSceneView;
class FVotionSplatSceneProxy;

class FVotionSplatRegistry
{
public:
	static FVotionSplatRegistry& Get();
	static int32 GetNumRegisteredGameThreadSafe();

	void Add(FVotionSplatSceneProxy* Proxy);
	void Remove(FVotionSplatSceneProxy* Proxy);
	void GatherVisible(const FSceneView& View, TArray<FVotionSplatSceneProxy*>& Out) const;

private:
	TArray<FVotionSplatSceneProxy*> Proxies;
	static std::atomic<int32> Count;
};
