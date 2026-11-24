#include "VotionSplatPlyReader.h"

#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"

namespace
{
	constexpr int64 MaxSplats = 16000000;

	struct FHeader
	{
		int64 Count = 0;
		int64 DataOffset = 0;
		int32 Stride = 0;
		TMap<FString, int32> Index;
		TMap<FString, FString> Tags;
	};

	bool ReadHeader(const FString& Path, FHeader& Out, FString& Error)
	{
		TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
		if (!File)
		{
			Error = TEXT("Could not open the file.");
			return false;
		}
		const int64 Size = File->Size();
		const int64 Used = FMath::Min<int64>(Size, 1024 * 1024);
		if (Used <= 0)
		{
			Error = TEXT("PLY file is empty.");
			return false;
		}
		TArray<uint8> Bytes;
		Bytes.SetNumUninitialized(Used);
		if (!File->Read(Bytes.GetData(), Used))
		{
			Error = TEXT("Could not read the PLY header.");
			return false;
		}
		{
			int64 Marker = -1;
			for (int64 i = 0; i + 10 <= Used; ++i)
			{
				if (FMemory::Memcmp(Bytes.GetData() + i, "end_header", 10) == 0)
				{
					Marker = i;
					break;
				}
			}
			if (Marker >= 0)
			{
				int64 Cursor = Marker + 10;
				if (Cursor < Used && Bytes[Cursor] == '\r')
				{
					++Cursor;
				}
				if (Cursor >= Used || Bytes[Cursor] != '\n')
				{
					Error = TEXT("end_header is not followed by a newline.");
					return false;
				}
				Out.DataOffset = Cursor + 1;
				TArray<uint8> HeaderBytes;
				HeaderBytes.Append(Bytes.GetData(), int32(Marker));
				HeaderBytes.Add(0);
				const FString Text = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(HeaderBytes.GetData()))).Replace(TEXT("\r"), TEXT(""));
				TArray<FString> Lines;
				Text.ParseIntoArrayLines(Lines);
				if (Lines.Num() < 2 || Lines[0] != TEXT("ply") || Lines[1] != TEXT("format binary_little_endian 1.0"))
				{
					Error = TEXT("Only binary little-endian PLY is supported.");
					return false;
				}
				int32 Prop = 0;
				for (const FString& Line : Lines)
				{
					if (Line.StartsWith(TEXT("comment votion.")))
					{
						FString Body = Line.Mid(15);
						FString Key, Value;
						if (Body.Split(TEXT("="), &Key, &Value))
						{
							Out.Tags.Add(Key, Value);
						}
					}
					else if (Line.StartsWith(TEXT("element ")) && !Line.StartsWith(TEXT("element vertex ")))
					{
						Error = FString::Printf(TEXT("Unsupported %s."), *Line);
						return false;
					}
					else if (Line.StartsWith(TEXT("element vertex ")))
					{
						Out.Count = FCString::Atoi64(*Line.Mid(15));
					}
					else if (Line.StartsWith(TEXT("property ")))
					{
						if (!Line.StartsWith(TEXT("property float ")))
						{
							Error = FString::Printf(TEXT("Unsupported %s. Votion PLY properties are float."), *Line);
							return false;
						}
						Out.Index.Add(Line.Mid(15), Prop++);
					}
				}
				Out.Stride = Prop * 4;
				return true;
			}
		}
		Error = TEXT("PLY header is missing or larger than 1 MB.");
		return false;
	}

	bool Require(const FHeader& Header, const TCHAR* Name, FString& Error)
	{
		if (Header.Index.Contains(Name))
		{
			return true;
		}
		Error = FString::Printf(TEXT("PLY is missing property %s."), Name);
		return false;
	}
}

bool VotionIsVotionPly(const FString& Path)
{
	FHeader Header;
	FString Error;
	return ReadHeader(Path, Header, Error) && Header.Tags.Contains(TEXT("version"));
}

bool VotionLoadPly(const FString& Path, FVotionPlyBuild& Out, FString& Error)
{
	FHeader Header;
	if (!ReadHeader(Path, Header, Error))
	{
		return false;
	}
	const FString* Version = Header.Tags.Find(TEXT("version"));
	if (!Version)
	{
		Error = TEXT("This PLY has no Votion tags. Export it with the Votion plugin preset.");
		return false;
	}
	if (*Version != TEXT("1"))
	{
		Error = TEXT("This file was written by a newer Votion. Update the plugin.");
		return false;
	}
	const FString* Raster = Header.Tags.Find(TEXT("raster"));
	if (!Raster || (*Raster != TEXT("gsplat_antialiased") && *Raster != TEXT("gsplat_classic") && *Raster != TEXT("litegs_classic")))
	{
		Error = TEXT("votion.raster is missing or not a supported profile.");
		return false;
	}
	const FString* Near = Header.Tags.Find(TEXT("near_plane"));
	const double NearValue = Near ? FCString::Atod(**Near) : 0.0;
	if (!Near || !FMath::IsFinite(NearValue) || NearValue <= 0.0)
	{
		Error = TEXT("votion.near_plane must be a finite value greater than zero.");
		return false;
	}
	const FString* DegreeText = Header.Tags.Find(TEXT("sh_degree"));
	const int32 Degree = DegreeText ? FCString::Atoi(**DegreeText) : -1;
	if (Degree < 0 || Degree > 3)
	{
		Error = TEXT("votion.sh_degree must be 0, 1, 2 or 3.");
		return false;
	}
	static const TCHAR* Required[] = {
		TEXT("x"), TEXT("y"), TEXT("z"), TEXT("f_dc_0"), TEXT("f_dc_1"), TEXT("f_dc_2"),
		TEXT("opacity"), TEXT("scale_0"), TEXT("scale_1"), TEXT("scale_2"),
		TEXT("rot_0"), TEXT("rot_1"), TEXT("rot_2"), TEXT("rot_3")
	};
	for (const TCHAR* Name : Required)
	{
		if (!Require(Header, Name, Error))
		{
			return false;
		}
	}
	int32 Rest = 0;
	while (Header.Index.Contains(FString::Printf(TEXT("f_rest_%d"), Rest)))
	{
		++Rest;
	}
	if (Rest % 3 != 0)
	{
		Error = TEXT("f_rest column count is not divisible by 3.");
		return false;
	}
	const int32 Bands = Rest / 3;
	const int32 Available = FMath::RoundToInt(FMath::Sqrt(float(Bands + 1))) - 1;
	if (Available < 0 || (Available + 1) * (Available + 1) - 1 != Bands || Degree > Available)
	{
		Error = TEXT("votion.sh_degree is larger than the f_rest data.");
		return false;
	}
	if (Header.Count <= 0 || Header.Count > MaxSplats)
	{
		Error = FString::Printf(TEXT("Splat count %lld is outside 1..%lld."), Header.Count, MaxSplats);
		return false;
	}
	const int64 FileSize = IFileManager::Get().FileSize(*Path);
	if (Header.DataOffset + Header.Count * Header.Stride > FileSize)
	{
		Error = TEXT("PLY vertex data is shorter than the header says.");
		return false;
	}

	TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Path));
	if (!File || !File->Seek(Header.DataOffset))
	{
		Error = TEXT("Could not read the vertex data.");
		return false;
	}
	const int32 Coeffs = (Degree + 1) * (Degree + 1) - 1;
	Out.SplatData.Reserve(int32(FMath::Min<int64>(Header.Count, MAX_int32)) * 4);
	Out.ShRest.Reserve(int32(FMath::Min<int64>(Header.Count, MAX_int32)) * Coeffs * 3);
	Out.Bounds = FBox(ForceInit);
	int32 Dropped = 0;
	constexpr int32 Chunk = 262144;
	TArray<float> Raw;
	Raw.SetNumUninitialized(Chunk * (Header.Stride / 4));
	FScopedSlowTask Task(float(Header.Count), NSLOCTEXT("VotionSplat", "Import", "Importing Votion splat"));
	Task.MakeDialog(false);
	for (int64 Base = 0; Base < Header.Count; Base += Chunk)
	{
		const int32 Rows = int32(FMath::Min<int64>(Chunk, Header.Count - Base));
		if (!File->Read(reinterpret_cast<uint8*>(Raw.GetData()), int64(Rows) * Header.Stride))
		{
			Error = TEXT("Failed while reading vertex data.");
			return false;
		}
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			const float* V = Raw.GetData() + Row * (Header.Stride / 4);
			auto At = [&](const TCHAR* Name) { return double(V[Header.Index[Name]]); };
			const double Px = At(TEXT("x")), Py = At(TEXT("y")), Pz = At(TEXT("z"));
			const double Ox = At(TEXT("opacity"));
			const double S0 = FMath::Exp(At(TEXT("scale_0")));
			const double S1 = FMath::Exp(At(TEXT("scale_1")));
			const double S2 = FMath::Exp(At(TEXT("scale_2")));
			double Qw = At(TEXT("rot_0")), Qx = At(TEXT("rot_1")), Qy = At(TEXT("rot_2")), Qz = At(TEXT("rot_3"));
			const double Qn = FMath::Sqrt(Qw * Qw + Qx * Qx + Qy * Qy + Qz * Qz);
			if (!FMath::IsFinite(Px + Py + Pz + S0 + S1 + S2 + Ox) || S0 <= 0 || S1 <= 0 || S2 <= 0)
			{
				++Dropped;
				continue;
			}
			if (Qn < 1e-12)
			{
				Qw = 1; Qx = Qy = Qz = 0;
			}
			else
			{
				Qw /= Qn; Qx /= Qn; Qy /= Qn; Qz /= Qn;
			}
			const double XX = Qx * Qx, YY = Qy * Qy, ZZ = Qz * Qz;
			const double XY = Qx * Qy, XZ = Qx * Qz, YZ = Qy * Qz;
			const double WX = Qw * Qx, WY = Qw * Qy, WZ = Qw * Qz;
			const double R[3][3] = {
				{ 1 - 2 * (YY + ZZ), 2 * (XY - WZ), 2 * (XZ + WY) },
				{ 2 * (XY + WZ), 1 - 2 * (XX + ZZ), 2 * (YZ - WX) },
				{ 2 * (XZ - WY), 2 * (YZ + WX), 1 - 2 * (XX + YY) }
			};
			const double Scales[3] = { S0 * S0, S1 * S1, S2 * S2 };
			double Cov[3][3] = {};
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				for (int32 RowA = 0; RowA < 3; ++RowA)
				{
					for (int32 Col = 0; Col < 3; ++Col)
					{
						Cov[RowA][Col] += Scales[Axis] * R[RowA][Axis] * R[Col][Axis];
					}
				}
			}
			const double Uxx = Cov[0][0] * 10000.0;
			const double Uxy = -Cov[0][2] * 10000.0;
			const double Uxz = -Cov[0][1] * 10000.0;
			const double Uyy = Cov[2][2] * 10000.0;
			const double Uyz = Cov[1][2] * 10000.0;
			const double Uzz = Cov[1][1] * 10000.0;
			const double Opac = 1.0 / (1.0 + FMath::Exp(-Ox));
			if (!FMath::IsFinite(Uxx + Uyy + Uzz + Opac))
			{
				++Dropped;
				continue;
			}
			const FVector Pos(Px * 100.0, -Pz * 100.0, -Py * 100.0);
			Out.SplatData.Add(FVector4f(Pos.X, Pos.Y, Pos.Z, Opac));
			Out.SplatData.Add(FVector4f(Uxx, Uxy, Uxz, Uyy));
			Out.SplatData.Add(FVector4f(Uyz, Uzz, 0, 0));
			Out.SplatData.Add(FVector4f(At(TEXT("f_dc_0")), At(TEXT("f_dc_1")), At(TEXT("f_dc_2")), 0));
			for (int32 Coeff = 0; Coeff < Coeffs; ++Coeff)
			{
				for (int32 Channel = 0; Channel < 3; ++Channel)
				{
					const int32 Source = Header.Index[FString::Printf(TEXT("f_rest_%d"), Channel * Bands + Coeff)];
					Out.ShRest.Add(V[Source]);
				}
			}
			const double Radius = 3.0 * FMath::Sqrt(FMath::Max3(Uxx, Uyy, Uzz));
			Out.Bounds += Pos + FVector(Radius);
			Out.Bounds += Pos - FVector(Radius);
		}
		Task.EnterProgressFrame(Rows);
	}
	Out.NumSplats = Out.SplatData.Num() / 4;
	Out.ShDegree = Degree;
	Out.Raster = *Raster;
	Out.NearPlane = float(NearValue);
	if (Out.NumSplats <= 0)
	{
		Error = TEXT("Every splat was non-finite.");
		return false;
	}
	if (Dropped > 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Votion import skipped %d non-finite splats in %s"), Dropped, *Path);
	}
	return true;
}
