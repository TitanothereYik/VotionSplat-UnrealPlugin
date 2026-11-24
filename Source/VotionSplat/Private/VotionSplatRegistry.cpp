#include "VotionSplatRegistry.h"

#include "VotionSplatSceneProxy.h"
#include "SceneView.h"

std::atomic<int32> FVotionSplatRegistry::Count{ 0 };

FVotionSplatRegistry& FVotionSplatRegistry::Get()
{
	static FVotionSplatRegistry Instance;
	return Instance;
}

int32 FVotionSplatRegistry::GetNumRegisteredGameThreadSafe()
{
	return Count.load();
}

void FVotionSplatRegistry::Add(FVotionSplatSceneProxy* Proxy)
{
	Proxies.AddUnique(Proxy);
	Count.store(Proxies.Num());
}

void FVotionSplatRegistry::Remove(FVotionSplatSceneProxy* Proxy)
{
	Proxies.Remove(Proxy);
	Count.store(Proxies.Num());
}

void FVotionSplatRegistry::GatherVisible(const FSceneView& View, TArray<FVotionSplatSceneProxy*>& Out) const
{
	const FSceneInterface* Scene = View.Family ? View.Family->Scene : nullptr;
	const FVector Origin = View.ViewMatrices.GetViewOrigin();
	for (FVotionSplatSceneProxy* Proxy : Proxies)
	{
		if (!Proxy || &Proxy->GetScene() != Scene || !Proxy->IsShown(&View))
		{
			continue;
		}
		const FPrimitiveComponentId Id = Proxy->GetPrimitiveComponentId();
		if (View.HiddenPrimitives.Contains(Id))
		{
			continue;
		}
		if (View.ShowOnlyPrimitives.IsSet() && !View.ShowOnlyPrimitives->Contains(Id))
		{
			continue;
		}
		const FBoxSphereBounds Bounds = Proxy->GetBounds();
		if (!View.ViewFrustum.IntersectBox(Bounds.Origin, Bounds.BoxExtent))
		{
			continue;
		}
		Out.Add(Proxy);
	}
	Out.Sort([&Origin](const FVotionSplatSceneProxy& A, const FVotionSplatSceneProxy& B)
	{
		return FVector::DistSquared(A.GetBounds().Origin, Origin) > FVector::DistSquared(B.GetBounds().Origin, Origin);
	});
}
