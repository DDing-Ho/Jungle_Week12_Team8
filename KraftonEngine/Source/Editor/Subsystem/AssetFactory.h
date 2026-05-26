#pragma once

#include "Core/CoreTypes.h"

enum class EMaterialCreatePreset
{
	UberLit,
	ParticleSprite,
};

class FAssetFactory
{
public:
	static bool CreateFloatCurve(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
	static bool CreateCameraShake(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
	static bool CreateAnimInstanceAsset(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
	static bool CreateParticleSystemAsset(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath);
	static bool CreateMaterial(const FString& DirectoryPath, const FString& AssetName, EMaterialCreatePreset Preset, FString& OutCreatedPath);
};
