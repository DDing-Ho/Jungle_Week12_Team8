#include "ParticleSystemAssetManager.h"

#include "Engine/Particles/Assets/ParticleAsset.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Asset/AssetPackage.h"
#include "Engine/Core/Log.h"
#include "Engine/Object/ObjectFactory.h"
#include "Engine/Serialization/WindowsArchive.h"

UParticleSystem* FParticleSystemAssetManager::Load(const FString& Path)
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);

	auto It = LoadedAssets.find(NormalizedPath);
	if (It != LoadedAssets.end())
	{
		return It->second;
	}

	if (!FAssetPackage::IsAssetPackagePath(NormalizedPath))
	{
		return nullptr;
	}

	FWindowsBinReader Reader(NormalizedPath);
	if (!Reader.IsValid())
	{
		// TODO: 클래스 이름 문자열은 Reflection으로 뜯어 올 수 있을 듯
		UE_LOG("ParticleSystem load failed: package open failed. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	FAssetPackageHeader Header;
	Reader << Header;
	if (!Header.IsValid(EAssetPackageType::ParticleSystem))
	{
		UE_LOG("ParticleSystem load failed: invalid package header. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	FAssetImportMetadata Metadata;
	Reader << Metadata;

	UParticleSystem* Asset = GUObjectArray.CreateObject<UParticleSystem>();
	Asset->Serialize(Reader);

	if (!Reader.IsValid())
	{
		GUObjectArray.DestroyObject(Asset);
		UE_LOG("ParticleSystem load failed: package data is incomplete. Path=%s", NormalizedPath.c_str());
		return nullptr;
	}

	Asset->SetSourcePath(NormalizedPath);
	LoadedAssets.emplace(NormalizedPath, Asset);
	return Asset;
}

UParticleSystem* FParticleSystemAssetManager::Find(const FString& Path) const
{
	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	auto It = LoadedAssets.find(NormalizedPath);
	return It != LoadedAssets.end() ? It->second : nullptr;
}

void FParticleSystemAssetManager::RegisterAsset(const FString& Path, UParticleSystem* Asset)
{
	if (!Asset)
	{
		return;
	}

	const FString NormalizedPath = FPaths::MakeProjectRelative(Path);
	Asset->SetSourcePath(NormalizedPath);
	LoadedAssets[NormalizedPath] = Asset;
}

bool FParticleSystemAssetManager::Save(UParticleSystem* Asset)
{
	if (!Asset)
	{
		return false;
	}

	const FString& Path = Asset->GetAssetPathFileName();
	if (Path.empty() || Path == "None")
	{
		return false;
	}

	FWindowsBinWriter Writer(FPaths::MakeProjectRelative(Path));
	if (!Writer.IsValid())
	{
		return false;
	}

	FAssetPackageHeader Header;
	Header.Type = static_cast<uint32>(EAssetPackageType::ParticleSystem);

	FAssetImportMetadata Metadata;
	Writer << Header;
	Writer << Metadata;

	Asset->Serialize(Writer);
	return Writer.IsValid();
}

// TODO: 함수 없이 직접 비교 고민..
bool FParticleSystemAssetManager::IsSupportedAssetPackage(const FString& Path) const
{
	FAssetImportMetadata Metadata;
	return FAssetPackage::ReadMetadata(Path, EAssetPackageType::ParticleSystem, Metadata);
}
