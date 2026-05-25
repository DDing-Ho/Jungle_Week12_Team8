#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"

class UParticleSystem;


// TODO: 너무 많은 중복성들..

class FParticleSystemAssetManager : public TSingleton<FParticleSystemAssetManager>
{
	friend class TSingleton<FParticleSystemAssetManager>;

public:
	UParticleSystem* Load(const FString& Path);
	UParticleSystem* Find(const FString& Path) const;
	void RegisterAsset(const FString& Path, UParticleSystem* Asset);
	bool Save(UParticleSystem* Asset);
	bool IsSupportedAssetPackage(const FString& Path) const;

private:
	TMap<FString, UParticleSystem*>  LoadedAssets;
};