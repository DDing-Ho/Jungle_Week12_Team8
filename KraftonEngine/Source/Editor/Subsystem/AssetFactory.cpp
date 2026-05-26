#include "Editor/Subsystem/AssetFactory.h"

#include "Object/ObjectFactory.h"
#include "Platform/Paths.h"
#include "SimpleJSON/json.hpp"
#include <fstream>
#include <filesystem>

#include "Animation/AnimInstanceAsset.h"
#include "Animation/AnimInstanceAssetManager.h"
#include "CameraShake/CameraShakeAsset.h"
#include "CameraShake/CameraShakeManager.h"
#include "FloatCurve/FloatCurveAsset.h"
#include "FloatCurve/FloatCurveManager.h"
#include "Particles/Assets/ParticleAsset.h"
#include "Particles/Assets/ParticleSystemAssetManager.h"

namespace
{
	FString SanitizeAssetStem(const FString& AssetName)
	{
		return AssetName.empty() ? FString("NewFloatCurve") : AssetName;
	}

	std::filesystem::path BuildUniqueAssetPath(const std::filesystem::path& Directory, const FString& AssetName, const wchar_t* Extension)
	{
		const FString BaseStem = SanitizeAssetStem(AssetName);

		int32 Suffix = 0;
		for (;;)
		{
			FString CandidateStem = BaseStem;
			if (Suffix > 0)
			{
				CandidateStem += "_";
				CandidateStem += std::to_string(Suffix);
			}

			std::filesystem::path CandidatePath = Directory / (FPaths::ToWide(CandidateStem) + Extension);
			if (!std::filesystem::exists(CandidatePath))
			{
				return CandidatePath;
			}

			++Suffix;
		}
	}

	void InitializeDefaultParticleSystem(UParticleSystem* NewAsset) 
	{
		if (!NewAsset)
		{
			return;
		}

		UParticleEmitter* Emitter = GUObjectArray.CreateObject<UParticleEmitter>(NewAsset);
		UParticleLODLevel* LOD = GUObjectArray.CreateObject<UParticleLODLevel>(Emitter);

		UParticleModuleRequired* Required = GUObjectArray.CreateObject<UParticleModuleRequired>(LOD);
		UParticleModuleTypeDataSprite* TypeData = GUObjectArray.CreateObject<UParticleModuleTypeDataSprite>(LOD);
		UParticleModuleSpawn* Spawn = GUObjectArray.CreateObject<UParticleModuleSpawn>(LOD);
		UParticleModuleLifetime* Lifetime = GUObjectArray.CreateObject<UParticleModuleLifetime>(LOD);
		UParticleModuleVelocity* Velocity = GUObjectArray.CreateObject<UParticleModuleVelocity>(LOD);

		LOD->SetRequiredModule(Required);
		LOD->SetTypeDataModule(TypeData);
		LOD->AddModule(Spawn);
		LOD->AddModule(Lifetime);
		LOD->AddModule(Velocity);
		LOD->CacheModules();

		Emitter->AddLODLevel(LOD);
		NewAsset->AddEmitter(Emitter);
		NewAsset->CacheSystemModuleInfo();
	}

	void InitializeMaterialJson(json::JSON& JsonData, const FString& MaterialPath, EMaterialCreatePreset Preset)
	{
		JsonData["PathFileName"] = MaterialPath;
		JsonData["Origin"] = "Editor";

		switch (Preset)
		{
		case EMaterialCreatePreset::ParticleSprite:
			JsonData["ShaderPath"] = "Shaders/Particles/ParticleSprite.hlsl";
			JsonData["BlendMode"] = "Translucent";
			JsonData["RenderPass"] = "AlphaBlend";
			JsonData["BlendState"] = "AlphaBlend";
			JsonData["DepthStencilState"] = "Default";
			JsonData["RasterizerState"] = "SolidNoCull";
			JsonData["Parameters"] = json::Object();
			JsonData["Textures"] = json::Object();
			break;

		case EMaterialCreatePreset::UberLit:
		default:
			JsonData["ShaderPath"] = "Shaders/Geometry/UberLit.hlsl";
			JsonData["BlendMode"] = "Opaque";
			JsonData["RenderPass"] = "Opaque";
			JsonData["BlendState"] = "Opaque";
			JsonData["DepthStencilState"] = "Default";
			JsonData["RasterizerState"] = "SolidBackCull";
			JsonData["Parameters"]["SectionColor"] = json::Array(1.0f, 1.0f, 1.0f, 1.0f);
			JsonData["Parameters"]["HasNormalMap"] = 0.0f;
			JsonData["Textures"] = json::Object();
			break;
		}
	}
}

bool FAssetFactory::CreateFloatCurve(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, AssetName, L".uasset");

	UFloatCurveAsset* NewAsset = GUObjectArray.CreateObject<UFloatCurveAsset>();
	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));

	FFloatCurve& Curve = NewAsset->GetCurve();
	Curve.Reset();
	Curve.AddKey(0.0f, 0.0f);
	Curve.AddKey(1.0f, 1.0f);
	Curve.SortKeys();

	bool bSaved = FFloatCurveManager::Get().Save(NewAsset);
	GUObjectArray.DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreateCameraShake(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, AssetName, L".uasset");

	UCameraShakeAsset* NewAsset = GUObjectArray.CreateObject<UCameraShakeAsset>();
	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));
	NewAsset->Version = 1;
	NewAsset->ShakeType = ECameraShakeType::Sequence;

	bool bSaved = FCameraShakeManager::Get().Save(NewAsset);
	GUObjectArray.DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreateAnimInstanceAsset(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const FString DefaultName = AssetName.empty() ? FString("NewAnimInstance") : AssetName;
	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, DefaultName, L".uasset");

	UAnimInstanceAsset* NewAsset = GUObjectArray.CreateObject<UAnimInstanceAsset>();
	NewAsset->SetAssetPathFileName(FPaths::ToUtf8(AssetPath.wstring()));

	FAnimGraphData& Graph = NewAsset->GetGraph();

	FAnimGraphNodeData OutputNode;
	OutputNode.NodeId = "Output";
	OutputNode.DisplayName = "Output";
	OutputNode.NodeType = EAnimGraphNodeType::Output;
	OutputNode.EditorPosX = 360.0f;
	OutputNode.EditorPosY = 80.0f;

	Graph.OutputNodeId = OutputNode.NodeId;
	Graph.Nodes.push_back(OutputNode);

	bool bSaved = FAnimInstanceAssetManager::Get().Save(NewAsset);
	GUObjectArray.DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreateParticleSystemAsset(const FString& DirectoryPath, const FString& AssetName, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, AssetName, L".uasset");

	UParticleSystem* NewAsset = GUObjectArray.CreateObject<UParticleSystem>();
	NewAsset->SetSourcePath(FPaths::ToUtf8(AssetPath.wstring()));

	InitializeDefaultParticleSystem(NewAsset);

	bool bSaved = FParticleSystemAssetManager::Get().Save(NewAsset);
	GUObjectArray.DestroyObject(NewAsset);

	if (!bSaved)
	{
		return false;
	}

	OutCreatedPath = FPaths::ToUtf8(AssetPath.wstring());
	return true;
}

bool FAssetFactory::CreateMaterial(const FString& DirectoryPath, const FString& AssetName, EMaterialCreatePreset Preset, FString& OutCreatedPath)
{
	const std::filesystem::path Directory(FPaths::ToWide(DirectoryPath));
	if (!std::filesystem::exists(Directory) || !std::filesystem::is_directory(Directory))
	{
		return false;
	}

	const FString DefaultName = AssetName.empty() ? FString("NewMaterial") : AssetName;
	const std::filesystem::path AssetPath = BuildUniqueAssetPath(Directory, DefaultName, L".mat");
	const FString AssetPathString = FPaths::ToUtf8(AssetPath.generic_wstring());
	const FString MaterialPath = FPaths::MakeProjectRelative(AssetPathString);

	json::JSON JsonData;
	InitializeMaterialJson(JsonData, MaterialPath, Preset);

	std::ofstream File(AssetPath);
	if (!File.is_open())
	{
		return false;
	}

	File << JsonData.dump();
	if (!File.good())
	{
		return false;
	}

	OutCreatedPath = MaterialPath;
	return true;
}
