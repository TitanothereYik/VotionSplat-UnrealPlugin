#include "VotionSplatLut.h"

#include "VotionSplatLog.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHITypes.h"

TGlobalResource<FVotionInverseViewLut> GVotionInverseViewLut;

void FVotionInverseViewLut::InitRHI(FRHICommandListBase& RHICmdList)
{
	TArray<uint8> Bytes;
	int32 N = 1;
	const uint8* Texels = nullptr;
	TArray<uint8> White;
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VotionSplat-UnrealPlugin"));
	const FString Path = Plugin.IsValid()
		? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/VotionSplat_InverseView.bin"))
		: FString();
	if (!Path.IsEmpty() && FFileHelper::LoadFileToArray(Bytes, *Path) && Bytes.Num() >= 12
		&& FMemory::Memcmp(Bytes.GetData(), "VSLUT001", 8) == 0)
	{
		FMemory::Memcpy(&N, Bytes.GetData() + 8, sizeof(int32));
		const int64 Payload = int64(N) * N * N * 16;
		if (N >= 2 && N <= 129 && Bytes.Num() >= 12 + Payload)
		{
			Texels = Bytes.GetData() + 12;
			bValid = true;
		}
	}
	if (!Texels)
	{
		N = 1;
		bValid = false;
		White.SetNumZeroed(16);
		float* Pixel = reinterpret_cast<float*>(White.GetData());
		Pixel[0] = Pixel[1] = Pixel[2] = Pixel[3] = 1.f;
		Texels = White.GetData();
		UE_LOG(LogVotionSplat, Warning, TEXT("Inverse view LUT missing or unreadable (%s). Scene-linear composite falls back."), *Path);
	}
	Size = N;
	FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create3D(TEXT("VotionSplat.InverseView"), N, N, N, PF_A32B32G32R32F);
	Desc.SetFlags(ETextureCreateFlags::ShaderResource);
	Desc.SetInitialState(ERHIAccess::SRVMask);
	Texture = RHICmdList.CreateTexture(Desc);
	const uint32 Row = uint32(N) * 16;
	RHICmdList.UpdateTexture3D(Texture, 0, FUpdateTextureRegion3D(0, 0, 0, 0, 0, 0, N, N, N), Row, Row * uint32(N), Texels);
}

void FVotionInverseViewLut::ReleaseRHI()
{
	Texture.SafeRelease();
}
