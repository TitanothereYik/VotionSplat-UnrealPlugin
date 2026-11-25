#include "VotionSplatFactory.h"

#include "VotionSplatActor.h"
#include "VotionSplatAsset.h"
#include "VotionSplatComponent.h"
#include "VotionSplatPlyReader.h"

#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "VotionSplat"

namespace
{
	void NotifyError(const FString& Message)
	{
		FNotificationInfo Info(FText::FromString(Message));
		Info.ExpireDuration = 8.f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	void ApplyBuild(UVotionSplatAsset* Asset, FVotionPlyBuild& Build, const FString& Filename)
	{
		Asset->NumSplats = Build.NumSplats;
		Asset->ShDegree = Build.ShDegree;
		Asset->RasterProfile = Build.Raster;
		Asset->NearPlanePly = Build.NearPlane;
		Asset->LocalBounds = Build.Bounds;
		Asset->SplatData = MoveTemp(Build.SplatData);
		Asset->ShRest = MoveTemp(Build.ShRest);
		if (Asset->AssetImportData)
		{
			Asset->AssetImportData->Update(Filename);
		}
		Asset->RebuildGPU();
		for (TObjectIterator<UVotionSplatComponent> It; It; ++It)
		{
			if (It->SplatAsset == Asset)
			{
				It->MarkRenderStateDirty();
			}
		}
	}
}

UVotionSplatFactory::UVotionSplatFactory()
{
	SupportedClass = UVotionSplatAsset::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bText = false;
	Formats.Add(TEXT("ply;VotionSplat-UnrealPlugin"));
	ImportPriority = DefaultImportPriority + 100;
}

int32 UVotionSplatFactory::GetPriority() const
{
	return DefaultImportPriority + 100;
}

bool UVotionSplatFactory::FactoryCanImport(const FString& Filename)
{
	return Filename.EndsWith(TEXT(".ply"), ESearchCase::IgnoreCase) && VotionIsVotionPly(Filename);
}

UObject* UVotionSplatFactory::FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, const FString& Filename, const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, TEXT("ply"));
	FVotionPlyBuild Build;
	FString Error;
	if (!VotionLoadPly(Filename, Build, Error))
	{
		if (Warn)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("%s"), *Error);
		}
		NotifyError(Error);
		return nullptr;
	}
	UVotionSplatAsset* Asset = NewObject<UVotionSplatAsset>(InParent, InClass, InName, Flags | RF_Transactional);
	ApplyBuild(Asset, Build, Filename);
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Asset);
	return Asset;
}

bool UVotionSplatFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UVotionSplatAsset* Asset = Cast<UVotionSplatAsset>(Obj);
	if (!Asset || !Asset->AssetImportData)
	{
		return false;
	}
	Asset->AssetImportData->ExtractFilenames(OutFilenames);
	return OutFilenames.Num() > 0;
}

void UVotionSplatFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	if (UVotionSplatAsset* Asset = Cast<UVotionSplatAsset>(Obj); Asset && Asset->AssetImportData && NewReimportPaths.Num() > 0)
	{
		Asset->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UVotionSplatFactory::Reimport(UObject* Obj)
{
	UVotionSplatAsset* Asset = Cast<UVotionSplatAsset>(Obj);
	if (!Asset || !Asset->AssetImportData)
	{
		return EReimportResult::Failed;
	}
	const FString Filename = Asset->AssetImportData->GetFirstFilename();
	FVotionPlyBuild Build;
	FString Error;
	if (!VotionLoadPly(Filename, Build, Error))
	{
		NotifyError(Error);
		return EReimportResult::Failed;
	}
	ApplyBuild(Asset, Build, Filename);
	return EReimportResult::Succeeded;
}

#undef LOCTEXT_NAMESPACE
