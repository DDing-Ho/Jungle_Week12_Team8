#include "ParticleSystemAssetManager.h"

#include "Engine/Particles/Assets/ParticleAsset.h"
#include "Engine/Particles/Modules/ParticleEventModules.h"
#include "Engine/Platform/Paths.h"
#include "Engine/Asset/AssetPackage.h"
#include "Engine/Core/Log.h"
#include "Engine/Object/ObjectFactory.h"
#include "Engine/Serialization/WindowsArchive.h"

namespace
{
#if defined(_DEBUG)
	const char* ParticleEventTypeToString(EParticleEventType EventType)
	{
		switch (EventType)
		{
		case EParticleEventType::PEET_Spawn:     return "Spawn";
		case EParticleEventType::PEET_Death:     return "Death";
		case EParticleEventType::PEET_Collision: return "Collision";
		case EParticleEventType::PEET_Custom:    return "Custom";
		default:                                 return "Unknown";
		}
	}

	void LogParticleSystemEventGenerators(const char* Phase, const FString& Path, const UParticleSystem* Asset)
	{
		if (!Asset)
		{
			UE_LOG("[ParticleDiag][AssetManager] %s path=%s asset=null", Phase, Path.c_str());
			return;
		}

		const TArray<UParticleEmitter*>& Emitters = Asset->GetEmitters();
		UE_LOG("[ParticleDiag][AssetManager] %s path=%s emitterCount=%zu", Phase, Path.c_str(), Emitters.size());

		for (int32 EmitterIndex = 0; EmitterIndex < static_cast<int32>(Emitters.size()); ++EmitterIndex)
		{
			const UParticleEmitter* Emitter = Emitters[EmitterIndex];
			if (!Emitter)
			{
				continue;
			}

			for (int32 LODIndex = 0; LODIndex < Emitter->GetLODLevelCount(); ++LODIndex)
			{
				const UParticleLODLevel* LOD = Emitter->GetLODLevel(LODIndex);
				if (!LOD)
				{
					continue;
				}

				for (UParticleModule* Module : LOD->GetModules())
				{
					if (!Module || Module->GetModuleClass() != EParticleModuleClass::EventGenerator)
					{
						continue;
					}

					const UParticleModuleEventGenerator* Generator = static_cast<UParticleModuleEventGenerator*>(Module);
					const TArray<FParticleEventGeneratorEntry>& Events = Generator->GetGeneratedEvents();
					UE_LOG("[ParticleDiag][AssetManager] %s path=%s emitter=%d lod=%d generatedEventCount=%zu",
						Phase,
						Path.c_str(),
						EmitterIndex,
						LODIndex,
						Events.size());

					for (int32 EventIndex = 0; EventIndex < static_cast<int32>(Events.size()); ++EventIndex)
					{
						const FParticleEventGeneratorEntry& Entry = Events[EventIndex];
						const FString EventName = Entry.EventName.ToString();
						UE_LOG("[ParticleDiag][AssetManager] %s path=%s emitter=%d lod=%d event=%d type=%s name='%s' valid=%s",
							Phase,
							Path.c_str(),
							EmitterIndex,
							LODIndex,
							EventIndex,
							ParticleEventTypeToString(Entry.Type),
							EventName.c_str(),
							Entry.EventName.IsValid() ? "true" : "false");
					}
				}
			}
		}
	}
#endif
}

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
#if defined(_DEBUG)
	LogParticleSystemEventGenerators("Load", NormalizedPath, Asset);
#endif
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

#if defined(_DEBUG)
	LogParticleSystemEventGenerators("Save-BeforeWrite", Path, Asset);
#endif
	Asset->Serialize(Writer);
#if defined(_DEBUG)
	UE_LOG("[ParticleDiag][AssetManager] Save-AfterWrite path=%s writerValid=%s", Path.c_str(), Writer.IsValid() ? "true" : "false");
#endif
	return Writer.IsValid();
}

// TODO: 함수 없이 직접 비교 고민..
bool FParticleSystemAssetManager::IsSupportedAssetPackage(const FString& Path) const
{
	FAssetImportMetadata Metadata;
	return FAssetPackage::ReadMetadata(Path, EAssetPackageType::ParticleSystem, Metadata);
}
