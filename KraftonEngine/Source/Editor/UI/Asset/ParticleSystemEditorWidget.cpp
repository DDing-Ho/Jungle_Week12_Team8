#include "ParticleSystemEditorWidget.h"

#include "Engine/Particles/Assets/ParticleAsset.h"
#include "Engine/Particles/Assets/ParticleSystemAssetManager.h"
#include "Engine/Particles/Modules/ParticleCoreModules.h"
#include "Engine/Particles/Modules/ParticleEventModules.h"
#include "Engine/Particles/Modules/ParticleMotionModules.h"
#include "Engine/Particles/Modules/ParticleRenderExpressionModules.h"
#include "Engine/Particles/Runtime/ParticleEmitterInstance.h"
#include "Engine/Core/Log.h"
#include "Engine/Core/Property/FDistributionProperty.h"
#include "Core/Property/FArrayProperty.h"
#include "Core/Property/FEnumProperty.h"
#include "Core/Property/FObjectPropertyBase/FSoftObjectProperty.h"
#include "Core/Property/FStructProperty.h"
#include "Core/Property/PropertyTypes.h"
#include "Core/UObject/TSoftObjectPtr.h"
#include "Engine/GameFramework/WorldContext.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/Component/ParticleSystemComponent.h"
#include "Engine/Viewport/Viewport.h"
#include "Editor/Slate/SlateApplication.h"
#include "Editor/UI/ContentBrowser/ContentItem.h"
#include "Editor/Settings/EditorSettings.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Object/FUObjectArray.h"
#include "Platform/Paths.h"
#include "Profiling/Stats.h"
#include "Serialization/MemoryArchive.h"
#include "Texture/Texture2D.h"
#include "UI/Toolbar/ViewportToolbar.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <cstdio>

// 바닥 시각화
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "Mesh/MeshManager.h"
#include "GameFramework/StaticMeshActor.h"
#include "Engine/Component/StaticMeshComponent.h"
#include "Engine/Component/BoxComponent.h"

static uint32 NextParticleSystemEditorInstanceId = 0;

// ── Curve canvas helpers ──────────────────────────────────────────────────────

static ImVec2 PCurveToScreen(float T, float V,
    float MinT, float MaxT, float MinV, float MaxV,
    ImVec2 CMin, ImVec2 CMax)
{
    const float W  = CMax.x - CMin.x;
    const float H  = CMax.y - CMin.y;
    const float TS = (MaxT > MinT) ? (MaxT - MinT) : 1.0f;
    const float VS = (MaxV > MinV) ? (MaxV - MinV) : 1.0f;
    return ImVec2(CMin.x + (T - MinT) / TS * W,
                  CMax.y - (V - MinV) / VS * H);
}

static void PScreenToCurve(ImVec2 Pos,
    float MinT, float MaxT, float MinV, float MaxV,
    ImVec2 CMin, ImVec2 CMax,
    float& OutT, float& OutV)
{
    const float W  = CMax.x - CMin.x;
    const float H  = CMax.y - CMin.y;
    const float TS = (MaxT > MinT) ? (MaxT - MinT) : 1.0f;
    const float VS = (MaxV > MinV) ? (MaxV - MinV) : 1.0f;
    OutT = MinT + ((W > 0.0f) ? (Pos.x - CMin.x) / W : 0.0f) * TS;
    OutV = MinV + ((H > 0.0f) ? (CMax.y - Pos.y) / H : 0.0f) * VS;
}

static bool PCurvePointNear(ImVec2 A, ImVec2 B, float R)
{
    const float DX = A.x - B.x, DY = A.y - B.y;
    return (DX * DX + DY * DY) <= R * R;
}

static bool PCurvePointFinite(ImVec2 P)
{
	return std::isfinite(P.x) && std::isfinite(P.y);
}

static ImVec2 PGetCurveTangentHandlePosition(
	const FFloatCurveKey& Key,
	bool bArrive,
	float MinT,
	float MaxT,
	float MinV,
	float MaxV,
	ImVec2 CMin,
	ImVec2 CMax)
{
	const float Tangent = bArrive ? Key.ArriveTangent : Key.LeaveTangent;
	const float Direction = bArrive ? -1.0f : 1.0f;
	const float Width = CMax.x - CMin.x;
	const float Height = CMax.y - CMin.y;
	const float TimeSpan = (MaxT > MinT) ? (MaxT - MinT) : 1.0f;
	const float ValueSpan = (MaxV > MinV) ? (MaxV - MinV) : 1.0f;

	ImVec2 DirectionVector(
		Direction * Width / TimeSpan,
		-Direction * Tangent * Height / ValueSpan);
	const float Length = std::sqrt(DirectionVector.x * DirectionVector.x + DirectionVector.y * DirectionVector.y);
	if (Length <= 1e-6f)
	{
		DirectionVector = ImVec2(Direction, 0.0f);
	}
	else
	{
		DirectionVector.x /= Length;
		DirectionVector.y /= Length;
	}

	const ImVec2 KeyPos = PCurveToScreen(Key.Time, Key.Value, MinT, MaxT, MinV, MaxV, CMin, CMax);
	constexpr float TangentHandleLength = 48.0f;
	return ImVec2(
		KeyPos.x + DirectionVector.x * TangentHandleLength,
		KeyPos.y + DirectionVector.y * TangentHandleLength);
}

static void PFitCurveViewToKeys(
	FParticleFloatCurve* MinCurve,
	FParticleFloatCurve* MaxCurve,
	float& OutMinT,
	float& OutMaxT,
	float& OutMinV,
	float& OutMaxV)
{
	bool bGotKey = false;
	auto FitFrom = [&](FParticleFloatCurve* Curve)
	{
		if (!Curve)
		{
			return;
		}

		for (const FFloatCurveKey& Key : Curve->Keys)
		{
			if (!bGotKey)
			{
				OutMinT = OutMaxT = Key.Time;
				OutMinV = OutMaxV = Key.Value;
				bGotKey = true;
			}
			else
			{
				OutMinT = (std::min)(OutMinT, Key.Time);
				OutMaxT = (std::max)(OutMaxT, Key.Time);
				OutMinV = (std::min)(OutMinV, Key.Value);
				OutMaxV = (std::max)(OutMaxV, Key.Value);
			}
		}
	};

	FitFrom(MinCurve);
	FitFrom(MaxCurve);

	if (!bGotKey)
	{
		OutMinT = 0.0f;
		OutMaxT = 1.0f;
		OutMinV = -10.0f;
		OutMaxV = 10.0f;
		return;
	}

	const float TimeMargin = (std::max)(0.05f, (OutMaxT - OutMinT) * 0.1f);
	const float ValueMargin = (std::max)(1.0f, (OutMaxV - OutMinV) * 0.15f);
	OutMinT = (std::max)(0.0f, OutMinT - TimeMargin);
	OutMaxT += TimeMargin;
	OutMinV -= ValueMargin;
	OutMaxV += ValueMargin;

	if (OutMaxT <= OutMinT + 0.001f)
	{
		OutMaxT = OutMinT + 1.0f;
	}
	if (OutMaxV <= OutMinV + 0.001f)
	{
		OutMaxV = OutMinV + 1.0f;
	}
}

struct FParticleModuleStyleColors
{
	ImVec4 Default;
	ImVec4 Selected;
};

struct FParticleModuleAddOption
{
	EParticleModuleClass Class;
	const char* Name;
};

struct FParticleModuleDragDropPayload
{
	int32 EmitterIndex = -1;
	UParticleModule* Module = nullptr;
};

struct FParticlePreviewStats
{
	int32 TotalEmitters = 0;
	int32 ActiveEmitters = 0;
	int32 ActiveParticles = 0;
	int32 MaxParticles = 0;
	int32 RenderedEmitters = 0;
	int32 QueuedEvents = 0;
};

static const FStatEntry* FindParticleStatEntry(const char* Name)
{
	if (!Name)
	{
		return nullptr;
	}

	const TArray<FStatEntry>& Snapshot = FStatManager::Get().GetSnapshot();
	for (const FStatEntry& Entry : Snapshot)
	{
		if (!Entry.Name || !Entry.Category)
		{
			continue;
		}

		if (strcmp(Entry.Category, "Particles") == 0 && strcmp(Entry.Name, Name) == 0)
		{
			return &Entry;
		}
	}

	return nullptr;
}

static FParticlePreviewStats GatherParticlePreviewStats(const UParticleSystemComponent* Component)
{
	FParticlePreviewStats Stats;
	if (!Component)
	{
		return Stats;
	}

	const TArray<FParticleEmitterInstance*>& Instances = Component->GetEmitterInstances();
	Stats.TotalEmitters = static_cast<int32>(Instances.size());
	Stats.RenderedEmitters = static_cast<int32>(Component->GetEmitterRenderData().size());
	Stats.QueuedEvents = static_cast<int32>(Component->GetFrameEventQueue().size());
	Stats.ActiveParticles = Component->GetTotalActiveParticles();

	for (const FParticleEmitterInstance* Instance : Instances)
	{
		if (!Instance)
		{
			continue;
		}

		Stats.MaxParticles += Instance->MaxActiveParticles;
		if (Instance->GetActiveParticleCount() > 0)
		{
			++Stats.ActiveEmitters;
		}
	}

	return Stats;
}

static const char* GetParticleEmitterTypeLabel(EParticleEmitterType Type)
{
	switch (Type)
	{
	case EParticleEmitterType::PET_Sprite: return "SPRITE";
	case EParticleEmitterType::PET_Mesh: return "MESH";
	case EParticleEmitterType::PET_Beam: return "BEAM";
	case EParticleEmitterType::PET_Ribbon: return "RIBBON";
	default: return "EMITTER";
	}
}

static ImU32 GetParticleEmitterTypeColor(EParticleEmitterType Type)
{
	switch (Type)
	{
	case EParticleEmitterType::PET_Sprite: return IM_COL32(255, 184, 77, 255);
	case EParticleEmitterType::PET_Mesh: return IM_COL32(104, 196, 255, 255);
	case EParticleEmitterType::PET_Beam: return IM_COL32(168, 128, 255, 255);
	case EParticleEmitterType::PET_Ribbon: return IM_COL32(110, 222, 168, 255);
	default: return IM_COL32(190, 190, 190, 255);
	}
}

static int32 GetParticleSystemLODCount(const UParticleSystem* Asset)
{
	if (!Asset)
	{
		return 0;
	}

	int32 LODCount = static_cast<int32>(Asset->LODDistances.size());
	for (UParticleEmitter* Emitter : Asset->GetEmitters())
	{
		if (Emitter)
		{
			LODCount = (std::max)(LODCount, Emitter->GetLODLevelCount());
		}
	}

	return (std::max)(LODCount, 0);
}

static float GetDefaultParticleLODDistance(int32 LODIndex)
{
	return static_cast<float>(LODIndex) * 2500.0f;
}

static void EnsureParticleSystemLODDistanceCount(UParticleSystem* Asset, int32 DesiredCount)
{
	if (!Asset || DesiredCount <= 0)
	{
		return;
	}

	if (Asset->LODDistances.empty())
	{
		Asset->LODDistances.push_back(0.0f);
	}

	while (static_cast<int32>(Asset->LODDistances.size()) < DesiredCount)
	{
		const int32 NextIndex = static_cast<int32>(Asset->LODDistances.size());
		const float PrevDistance = Asset->LODDistances.back();
		Asset->LODDistances.push_back((std::max)(PrevDistance, GetDefaultParticleLODDistance(NextIndex)));
	}
}

static UParticleLODLevel* CreateDefaultParticleLODLevel(UParticleEmitter* Emitter, int32 LODIndex)
{
	if (!Emitter)
	{
		return nullptr;
	}

	UParticleLODLevel* LOD = GUObjectArray.CreateObject<UParticleLODLevel>(Emitter);
	UParticleModuleRequired* Required = GUObjectArray.CreateObject<UParticleModuleRequired>(LOD);
	UParticleModuleTypeDataSprite* TypeData = GUObjectArray.CreateObject<UParticleModuleTypeDataSprite>(LOD);
	UParticleModuleSpawn* Spawn = GUObjectArray.CreateObject<UParticleModuleSpawn>(LOD);
	UParticleModuleLifetime* Lifetime = GUObjectArray.CreateObject<UParticleModuleLifetime>(LOD);
	UParticleModuleVelocity* Velocity = GUObjectArray.CreateObject<UParticleModuleVelocity>(LOD);

	LOD->SetLevel(LODIndex);
	LOD->SetRequiredModule(Required);
	LOD->SetTypeDataModule(TypeData);
	LOD->AddModule(Spawn);
	LOD->AddModule(Lifetime);
	LOD->AddModule(Velocity);
	LOD->CacheModules();
	return LOD;
}

static UParticleModule* FindModuleByClass(const UParticleLODLevel* LODLevel, EParticleModuleClass ModuleClass)
{
	if (!LODLevel)
	{
		return nullptr;
	}

	if (ModuleClass == EParticleModuleClass::Required)
	{
		return LODLevel->GetRequiredModule();
	}

	const bool bIsTypeDataClass =
		(ModuleClass == EParticleModuleClass::TypeDataSprite) ||
		(ModuleClass == EParticleModuleClass::TypeDataMesh) ||
		(ModuleClass == EParticleModuleClass::TypeDataBeam) ||
		(ModuleClass == EParticleModuleClass::TypeDataRibbon);
	if (bIsTypeDataClass)
	{
		return LODLevel->GetTypeDataModule();
	}

	for (UParticleModule* Module : LODLevel->GetModules())
	{
		if (Module && Module->GetModuleClass() == ModuleClass)
		{
			return Module;
		}
	}

	return nullptr;
}

static bool AreParticleModulesEffectivelyShared(UParticleModule* CurrentModule, UParticleModule* PreviousModule)
{
	if (!CurrentModule || !PreviousModule)
	{
		return false;
	}

	if (CurrentModule == PreviousModule)
	{
		return true;
	}

	if (CurrentModule->GetClass() != PreviousModule->GetClass())
	{
		return false;
	}

	FMemoryArchive CurrentWriter(/*bIsSaving=*/true);
	CurrentWriter.SetIsDuplicating(true);
	CurrentModule->Serialize(CurrentWriter);

	FMemoryArchive PreviousWriter(/*bIsSaving=*/true);
	PreviousWriter.SetIsDuplicating(true);
	PreviousModule->Serialize(PreviousWriter);

	return CurrentWriter.GetBuffer() == PreviousWriter.GetBuffer();
}

static bool ShouldDisplayParticleEditableProperty(const UObject* Object, const FProperty* Prop)
{
	if (!Object || !Prop)
	{
		return false;
	}

	const UParticleModule* ParticleModule = Cast<UParticleModule>(Object);
	if (ParticleModule
		&& (Prop->Name == "RandomSeedInfo" || Prop->Name == "Random Seed Info")
		&& !ParticleModule->SupportsRandomSeed())
	{
		return false;
	}

	return true;
}

static bool IsParticleEventModuleClass(EParticleModuleClass ModuleClass)
{
	return ModuleClass == EParticleModuleClass::EventGenerator
		|| ModuleClass == EParticleModuleClass::EventReceiverSpawn
		|| ModuleClass == EParticleModuleClass::EventReceiverKillAll;
}

static bool IsParticleSubUVModuleClass(EParticleModuleClass ModuleClass)
{
	return ModuleClass == EParticleModuleClass::SubImageIndex
		|| ModuleClass == EParticleModuleClass::SubUVMovie;
}

static bool EventGeneratorRequiresEnabledCollisionModule(const UParticleModule* Module)
{
	const UParticleModuleEventGenerator* Generator = Cast<UParticleModuleEventGenerator>(Module);
	if (!Generator)
	{
		return false;
	}

	for (const FParticleEventGeneratorEntry& Entry : Generator->GetGeneratedEvents())
	{
		if (Entry.Type == EParticleEventType::PEET_Collision)
		{
			return true;
		}
	}

	return false;
}

static bool HasEnabledCollisionModule(const UParticleLODLevel* LOD)
{
	if (!LOD)
	{
		return false;
	}

	for (UParticleModule* Candidate : LOD->GetModules())
	{
		if (Candidate
			&& Candidate->IsEnabled()
			&& Candidate->GetModuleClass() == EParticleModuleClass::Collision)
		{
			return true;
		}
	}

	return false;
}

#if defined(_DEBUG)
static const char* ParticleEventTypeToString(EParticleEventType EventType)
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

static void LogEventGeneratorState(const char* Phase, const UObject* Object, const FProperty* Prop)
{
	const UParticleModuleEventGenerator* Generator = Cast<UParticleModuleEventGenerator>(Object);
	if (!Generator)
	{
		return;
	}

	const char* PropertyName = Prop ? Prop->Name.c_str() : "<null>";
	const TArray<FParticleEventGeneratorEntry>& Events = Generator->GetGeneratedEvents();
	UE_LOG("[ParticleDiag][Editor] %s module=%p prop=%s eventCount=%zu",
		Phase,
		Generator,
		PropertyName,
		Events.size());

	for (int32 EventIndex = 0; EventIndex < static_cast<int32>(Events.size()); ++EventIndex)
	{
		const FParticleEventGeneratorEntry& Entry = Events[EventIndex];
		const FString EventName = Entry.EventName.ToString();
		UE_LOG("[ParticleDiag][Editor] %s module=%p event=%d type=%s name='%s' valid=%s",
			Phase,
			Generator,
			EventIndex,
			ParticleEventTypeToString(Entry.Type),
			EventName.c_str(),
			Entry.EventName.IsValid() ? "true" : "false");
	}
}
#endif

static bool IsParticleModuleFixedInStack(EParticleModuleClass ModuleClass)
{
	return ModuleClass == EParticleModuleClass::Required
		|| ModuleClass == EParticleModuleClass::Spawn
		|| ModuleClass == EParticleModuleClass::TypeDataSprite
		|| ModuleClass == EParticleModuleClass::TypeDataMesh
		|| ModuleClass == EParticleModuleClass::TypeDataBeam
		|| ModuleClass == EParticleModuleClass::TypeDataRibbon;
}

static int32 FindParticleModuleIndex(const UParticleLODLevel* LODLevel, const UParticleModule* Module)
{
	if (!LODLevel || !Module)
	{
		return -1;
	}

	const TArray<UParticleModule*>& Modules = LODLevel->GetModules();
	for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
	{
		if (Modules[ModuleIndex] == Module)
		{
			return ModuleIndex;
		}
	}

	return -1;
}

static void DrawSharedModuleStripeOverlay(ImDrawList* DrawList, const ImRect& Rect)
{
	if (!DrawList)
	{
		return;
	}

	const float StripeSpacing = 10.0f;
	const float StripeRun = Rect.GetHeight();
	DrawList->PushClipRect(Rect.Min, Rect.Max, true);
	for (float X = Rect.Min.x - StripeRun; X < Rect.Max.x + StripeRun; X += StripeSpacing)
	{
		DrawList->AddLine(
			ImVec2(X, Rect.Max.y),
			ImVec2(X + StripeRun, Rect.Min.y),
			IM_COL32(0, 0, 0, 88),
			1.0f);
	}
	DrawList->PopClipRect();
}

static ID3D11ShaderResourceView* GetParticleEmitterPreviewSRV(UParticleEmitter* Emitter, int32 LODIndex)
{
	if (!Emitter)
	{
		return nullptr;
	}

	UParticleLODLevel* LODLevel = Emitter->GetLODLevel(LODIndex);
	if (!LODLevel)
	{
		LODLevel = Emitter->GetLODLevel(0);
	}
	if (!LODLevel)
	{
		return nullptr;
	}

	UParticleModuleRequired* RequiredModule = LODLevel->GetRequiredModule();
	if (!RequiredModule)
	{
		return nullptr;
	}

	UMaterial* Material = RequiredModule->GetMaterial();
	if (!Material)
	{
		return nullptr;
	}

	TMap<FString, UTexture2D*>* Textures = Material->GetTexture();
	if (!Textures)
	{
		return nullptr;
	}

	for (const auto& Pair : *Textures)
	{
		UTexture2D* Texture = Pair.second;
		if (Texture && Texture->GetSRV())
		{
			return Texture->GetSRV();
		}
	}

	return nullptr;
}

constexpr FParticleModuleStyleColors ParticleTypeDataModuleColors = { ImVec4(0.078f, 0.078f, 0.098f, 1.0f), ImVec4(1.0f, 0.39f, 0.0f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleRequiredModuleColors = { ImVec4(0.784f, 0.784f, 0.392f, 1.0f), ImVec4(1.0f, 0.882f, 0.196f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleSpawnModuleColors = { ImVec4(0.784f, 0.392f, 0.392f, 1.0f), ImVec4(1.0f, 0.196f, 0.196f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleEventModuleColors = { ImVec4(0.212f, 0.282f, 0.824f, 1.0f), ImVec4(0.290f, 0.376f, 0.922f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleSubUVModuleColors = { ImVec4(0.192f, 0.345f, 0.192f, 1.0f), ImVec4(0.239f, 0.820f, 0.357f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleNormalModuleColors = { ImVec4(0.157f, 0.157f, 0.192f, 1.0f), ImVec4(1.0f, 0.392f, 0.0f, 1.0f) };

static bool IsParticleMaterialPath(const FString& MaterialPath)
{
	const TArray<FMaterialAssetListItem>& MatFiles = FMaterialManager::Get().GetAvailableParticleMaterialFiles();
	return std::any_of(MatFiles.begin(), MatFiles.end(),
		[&MaterialPath](const FMaterialAssetListItem& Item)
		{
			return Item.FullPath == MaterialPath;
		});
}

constexpr FParticleModuleAddOption ParticleModuleAddOptions[] =
{
	{ EParticleModuleClass::Spawn, "Spawn" },
	{ EParticleModuleClass::Lifetime, "Lifetime" },
	{ EParticleModuleClass::Location, "Location" },
	{ EParticleModuleClass::Velocity, "Velocity" },
	{ EParticleModuleClass::Color, "Color" },
	{ EParticleModuleClass::Size, "Size" },
	{ EParticleModuleClass::Rotation, "Rotation" },
	{ EParticleModuleClass::RotationRate, "Rotation Rate" },
	{ EParticleModuleClass::Acceleration, "Acceleration" },
	{ EParticleModuleClass::Attractor, "Attractor" },
	{ EParticleModuleClass::Orbit, "Orbit" },
	{ EParticleModuleClass::Collision, "Collision" },
	{ EParticleModuleClass::Kill, "Kill" },
	{ EParticleModuleClass::EventGenerator, "Event Generator" },
	{ EParticleModuleClass::EventReceiverSpawn, "Event Receiver Spawn" },
	{ EParticleModuleClass::EventReceiverKillAll, "Event Receiver Kill All" },
	{ EParticleModuleClass::SubImageIndex, "SubImage Index" },
	{ EParticleModuleClass::SubUVMovie, "SubUV Movie" },
	{ EParticleModuleClass::Light, "Light" },
	{ EParticleModuleClass::VectorField, "Vector Field" },
	{ EParticleModuleClass::Camera, "Camera" },
	{ EParticleModuleClass::Parameter, "Parameter" },
};

constexpr ImVec4 ParticlePanelAccentColor = ImVec4(0.0f, 0.71f, 0.86f, 1.0f);
constexpr ImVec4 ParticlePanelHeaderColor = ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
constexpr ImVec4 ParticlePanelBodyColor = ImVec4(0.23f, 0.23f, 0.23f, 1.0f);
constexpr ImVec4 ParticleEmitterBlockColor = ImVec4(0.26f, 0.26f, 0.26f, 1.0f);

constexpr ImVec2 ParticleEditorInitialWindowSize = ImVec2(1280.0f, 780.0f);
constexpr ImVec2 ParticleEditorZeroSpacing = ImVec2(0.0f, 0.0f);
constexpr ImVec2 ParticleEditorZeroVerticalSpacing = ImVec2(0.0f, 0.0f);
constexpr ImVec2 ParticlePanelContentPadding = ImVec2(12.0f, 10.0f);
constexpr ImVec2 ParticlePanelItemSpacing = ImVec2(8.0f, 8.0f);
constexpr ImVec2 ParticleEmitterBlockPadding = ImVec2(0.0f, 8.0f);

constexpr float ParticleEditorSplitterThickness = 6.0f;
constexpr float ParticleEditorMinPanelWidth = 180.0f;
constexpr float ParticleEditorMinViewportHeight = 160.0f;
constexpr float ParticleEditorMinLowerPanelHeight = 100.0f;

constexpr float ParticlePanelHeaderHeight = 28.0f;
constexpr float ParticlePanelAccentWidth = 4.0f;
constexpr float ParticlePanelTitleOffsetX = 12.0f;
constexpr float ParticlePanelTitleOffsetY = 7.0f;
constexpr float ParticleViewportTitleOffsetY = 6.0f;
constexpr float ParticleDetailsHeaderSpacing = 34.0f;
constexpr float ParticleCurveEditorHeaderSpacing = 32.0f;
constexpr float ParticleDetailsColumnWidth = 150.0f;

constexpr float ParticleEmitterSpacing = 14.0f;
constexpr float ParticleEmitterWidth = 180.0f;
constexpr float ParticleEmitterHeaderHeight = 74.0f;
constexpr float ParticleEmitterHeaderBottomSpacing = 6.0f;
constexpr float ParticleEmitterHeaderPaddingX = 10.0f;
constexpr float ParticleEmitterHeaderPaddingY = 8.0f;
constexpr float ParticleEmitterHeaderPreviewSize = 46.0f;
constexpr float ParticleEmitterHeaderPreviewInset = 8.0f;
constexpr float ParticleEmitterHeaderBadgeHeight = 18.0f;
constexpr float ParticleEmitterHeaderNameOffsetY = 6.0f;

constexpr float ParticleModuleItemSpacing = 0.0f;
constexpr float ParticleModuleHeight = 32.0f;
constexpr ImVec2 ParticleModuleFramePadding = ImVec2(10.0f, 6.0f);
constexpr ImVec2 ParticleModuleTextAlign = ImVec2(0.06f, 0.5f);
constexpr float ParticleModuleCheckboxFramePadding = 1.0f;
constexpr float ParticleModuleCheckboxRightPadding = 6.0f;

static void DrawParticleModuleLabel(ImDrawList* DrawList, const ImRect& Rect, const char* ModuleName, bool bCanToggleModule)
{
	if (!DrawList || !ModuleName || ModuleName[0] == '\0')
	{
		return;
	}

	const float CheckboxSize = ImGui::GetFontSize() + ParticleModuleCheckboxFramePadding * 2.0f;
	const float TextRightPadding = ParticleModuleFramePadding.x +
		(bCanToggleModule ? CheckboxSize + ParticleModuleCheckboxRightPadding : 0.0f);
	const ImVec2 TextSize = ImGui::CalcTextSize(ModuleName);
	const ImVec2 TextPos(
		Rect.Min.x + ParticleModuleFramePadding.x,
		Rect.Min.y + (Rect.GetHeight() - TextSize.y) * 0.5f);
	const ImVec2 ClipMax(Rect.Max.x - TextRightPadding, Rect.Max.y);

	if (ClipMax.x <= TextPos.x)
	{
		return;
	}

	DrawList->PushClipRect(Rect.Min, ClipMax, true);
	DrawList->AddText(ImVec2(TextPos.x + 1.0f, TextPos.y), IM_COL32(0, 0, 0, 200), ModuleName);
	DrawList->AddText(ImVec2(TextPos.x, TextPos.y + 1.0f), IM_COL32(0, 0, 0, 200), ModuleName);
	DrawList->AddText(TextPos, IM_COL32(255, 255, 255, 255), ModuleName);
	DrawList->PopClipRect();
}

const FVector ParticlePreviewFloorLocation = FVector(0.0f, 0.0f, -1.0f);
const FVector ParticlePreviewFloorScale = FVector(10.0f, 10.0f, 1.0f);

FParticleSystemEditorWidget::FParticleSystemEditorWidget()
	: InstanceId(NextParticleSystemEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("ParticleSystemEditorPreview_" + Id);
	WindowIdSuffix = "###ParticleSystemEditor_" + Id;
}

bool FParticleSystemEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UParticleSystem>();
}

void FParticleSystemEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);
	SelectedEmitterIndex = -1;
	EditedLODIndex = 0;
	SelectedModule = nullptr;

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	Actor->bTickInEditor = true;
	if (UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject))
	{
		UParticleSystemComponent* Comp = Actor->AddComponent<UParticleSystemComponent>();
		Comp->SetTemplate(Asset);
		Actor->SetRootComponent(Comp);
	}
	Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
	FloorActor->InitDefaultComponents("Asset/Mesh/CubeGrid/SM_CubeGrid_StaticMesh.uasset");
	UBoxComponent* FloorBoxComponent = FloorActor->AddComponent<UBoxComponent>();
	FloorBoxComponent->AttachToComponent(FloorActor->GetRootComponent());
	FloorActor->SetActorLocation(ParticlePreviewFloorLocation);
	FloorActor->SetActorScale(ParticlePreviewFloorScale);

	ImVec2 ViewportSize = ImGui::GetContentRegionAvail();

	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewComponent(Actor->GetComponentByClass<UParticleSystemComponent>());

	ViewportClient.ResetCameraToPreviewBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);

	FSlateApplication::Get().RegisterViewport(&ViewportWindow, &ViewportClient);
	SyncEditedLODSelection();
}

void FParticleSystemEditorWidget::Close()
{
	FAssetEditorWidget::Close();

	if (UWorld* PreviewWorld = ViewportClient.GetPreviewWorld())
	{
		FScene& PreviewScene = PreviewWorld->GetScene();
		GEngine->GetRenderer().GetResources().ReleaseShadowResourcesForScene(&PreviewScene);

		if (PreviewWorldHandle.IsValid())
		{
			GEngine->DestroyWorldContext(PreviewWorldHandle);
		}
	}


	FSlateApplication::Get().UnregisterViewport(&ViewportClient);

	ViewportClient.Release();
}

void FParticleSystemEditorWidget::Tick(float DeltaTime)
{
	if (ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}

	if (UParticleSystemComponent* Comp = GetPreviewParticleComponent())
	{
		// Apply speed scale; pause by zeroing time dilation
		Comp->SetCustomTimeDilation(bAnimPaused ? 0.0f : AnimSpeedScale);

		// Loop: restart when the system finishes playing
		if (bAnimLoop && !bAnimPaused)
		{
			const bool bNowActive = Comp->IsActive();
			if (bWasActive && !bNowActive)
				ReplayPreviewParticleSystem();
			bWasActive = bNowActive;
		}
		else
		{
			bWasActive = Comp->IsActive();
		}
	}
}

void FParticleSystemEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen())
	{
		OutClients.push_back(const_cast<FParticleSystemEditorViewportClient*>(&ViewportClient));
	}
}

void FParticleSystemEditorWidget::Render(float DeltaTime)
{
	if (bPendingClose)
	{
		Close();
		bPendingClose = false;
		return;
	}

	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject);
	EditedLODIndex = GetEditedLODIndex(Asset);

	bool bWindowOpen = true;
	FString VisibleTitle = "Particle System Editor";
	const FString AssetPath = Asset->GetAssetPathFileName();

	VisibleTitle += " - ";
	VisibleTitle += AssetPath;

	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGui::SetNextWindowSize(ParticleEditorInitialWindowSize, ImGuiCond_Once);
	const ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoSavedSettings;
	const FString WindowTitle = VisibleTitle + WindowIdSuffix;
	if (ConsumeFocusRequest())
	{
		ImGui::SetNextWindowFocus();
	}

	if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen, WindowFlags))
	{
		ImGui::End();
		if (!bWindowOpen)
		{
			bPendingClose = true;
		}
		return;
	}

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
	{
		FSlateApplication::Get().BringViewportToFront(&ViewportClient);
	}

	bool bChanged = false;

	// ── 공용 상태 ────────────────────────────────────────────────────
	UParticleEmitter* SelectedEmitter = nullptr;
	if (SelectedEmitterIndex >= 0 && SelectedEmitterIndex < static_cast<int32>(Asset->GetEmitters().size()))
		SelectedEmitter = Asset->GetEmitter(SelectedEmitterIndex);

	UParticleLODLevel* SelectedLOD = GetEditedLODLevel(SelectedEmitter);
	const int32 TotalLODCount      = GetParticleSystemLODCount(Asset);
	const bool  bCanEditLODs       = TotalLODCount > 0 && !Asset->GetEmitters().empty();
	const bool  bCanRemoveEmitter  = SelectedEmitterIndex >= 0 && SelectedEmitterIndex < static_cast<int32>(Asset->GetEmitters().size());
	const bool  bCanDeleteLOD      = bCanEditLODs && TotalLODCount > 1;

	bool bCanRemoveModule   = false;
	int32 SelectedModuleIndex = -1;
	if (SelectedModule && SelectedLOD)
	{
		const TArray<UParticleModule*>& Modules = SelectedLOD->GetModules();
		for (int32 i = 0; i < static_cast<int32>(Modules.size()); ++i)
		{
			if (Modules[i] == SelectedModule)
			{
				SelectedModuleIndex = i;
				bCanRemoveModule = !IsParticleModuleFixedInStack(SelectedModule->GetModuleClass());
				break;
			}
		}
	}

	// ── 1. Save ──────────────────────────────────────────────────────
	if (ImGui::Button("Save"))
	{
#if defined(_DEBUG)
		UE_LOG("[ParticleDiag][Editor] SaveButton path=%s dirty=%s",
			Asset->GetAssetPathFileName().c_str(),
			IsDirty() ? "true" : "false");
		LogEventGeneratorState("SaveButton-BeforeSave", SelectedModule, nullptr);
#endif
		if (FParticleSystemAssetManager::Get().Save(Asset))
			ClearDirty();
	}
	ImGui::SameLine();

	// ── 2. Restart Sim ───────────────────────────────────────────────
	if (ImGui::Button("Restart Sim"))
		ReplayPreviewParticleSystem();
	ImGui::SameLine();

	// ── 3. Emitter 드롭다운 ──────────────────────────────────────────
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("Emitter").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight());
	if (ImGui::BeginCombo("##EmitterCombo", "Emitter"))
	{
		if (ImGui::Selectable("Add Emitter"))
		{
			const int32 NewEmitterIndex = static_cast<int32>(Asset->GetEmitters().size());
			UParticleEmitter* Emitter = GUObjectArray.CreateObject<UParticleEmitter>(Asset);
			Emitter->SetEmitterName(FName("Emitter_" + std::to_string(NewEmitterIndex + 1)));
			const int32 DesiredLODCount = (std::max)(1, GetParticleSystemLODCount(Asset));
			UParticleLODLevel* BaseLOD = CreateDefaultParticleLODLevel(Emitter, 0);
			if (BaseLOD)
				Emitter->AddLODLevel(BaseLOD);
			for (int32 LODIndex = 1; LODIndex < DesiredLODCount; ++LODIndex)
			{
				UParticleLODLevel* NewLOD = BaseLOD
					? Cast<UParticleLODLevel>(BaseLOD->Duplicate(Emitter))
					: CreateDefaultParticleLODLevel(Emitter, LODIndex);
				if (NewLOD)
					Emitter->AddLODLevel(NewLOD);
			}
			EnsureParticleSystemLODDistanceCount(Asset, DesiredLODCount);
			Asset->AddEmitter(Emitter);
			Asset->CacheSystemModuleInfo();
			SelectedEmitterIndex = NewEmitterIndex;
			SelectedModule = nullptr;
			SyncEditedLODSelection();
			MarkDirty();
		}
		ImGui::BeginDisabled(!bCanRemoveEmitter);
		if (ImGui::Selectable("Remove Emitter"))
		{
			Asset->RemoveEmitter(SelectedEmitterIndex);
			Asset->CacheSystemModuleInfo();
			SelectedEmitterIndex = -1;
			SelectedModule = nullptr;
			SyncEditedLODSelection();
			MarkDirty();
		}
		ImGui::EndDisabled();
		ImGui::EndCombo();
	}
	ImGui::SameLine();

	// ── 4. Module 드롭다운 ───────────────────────────────────────────
	ImGui::BeginDisabled(!SelectedEmitter || !SelectedLOD);
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("Module").x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetFrameHeight());
	if (ImGui::BeginCombo("##ModuleCombo", "Module"))
	{
		if (ImGui::BeginMenu("Add Module"))
		{
			for (const FParticleModuleAddOption& Option : ParticleModuleAddOptions)
			{
				bool bAlreadyExists = false;
				for (UParticleModule* Module : SelectedLOD->GetModules())
				{
					if (Module && Module->GetModuleClass() == Option.Class)
					{
						bAlreadyExists = true;
						break;
					}
				}
				ImGui::BeginDisabled(bAlreadyExists);
				if (ImGui::MenuItem(Option.Name, nullptr, false, !bAlreadyExists))
				{
					UParticleModule* NewModule = nullptr;
					switch (Option.Class)
					{
					case EParticleModuleClass::Spawn:                NewModule = GUObjectArray.CreateObject<UParticleModuleSpawn>(SelectedLOD); break;
					case EParticleModuleClass::Lifetime:             NewModule = GUObjectArray.CreateObject<UParticleModuleLifetime>(SelectedLOD); break;
					case EParticleModuleClass::Location:             NewModule = GUObjectArray.CreateObject<UParticleModuleLocation>(SelectedLOD); break;
					case EParticleModuleClass::Velocity:             NewModule = GUObjectArray.CreateObject<UParticleModuleVelocity>(SelectedLOD); break;
					case EParticleModuleClass::Color:                NewModule = GUObjectArray.CreateObject<UParticleModuleColor>(SelectedLOD); break;
					case EParticleModuleClass::Size:                 NewModule = GUObjectArray.CreateObject<UParticleModuleSize>(SelectedLOD); break;
					case EParticleModuleClass::Rotation:             NewModule = GUObjectArray.CreateObject<UParticleModuleRotation>(SelectedLOD); break;
					case EParticleModuleClass::RotationRate:         NewModule = GUObjectArray.CreateObject<UParticleModuleRotationRate>(SelectedLOD); break;
					case EParticleModuleClass::Acceleration:         NewModule = GUObjectArray.CreateObject<UParticleModuleAcceleration>(SelectedLOD); break;
					case EParticleModuleClass::Attractor:            NewModule = GUObjectArray.CreateObject<UParticleModuleAttractor>(SelectedLOD); break;
					case EParticleModuleClass::Orbit:                NewModule = GUObjectArray.CreateObject<UParticleModuleOrbit>(SelectedLOD); break;
					case EParticleModuleClass::Collision:            NewModule = GUObjectArray.CreateObject<UParticleModuleCollision>(SelectedLOD); break;
					case EParticleModuleClass::Kill:                 NewModule = GUObjectArray.CreateObject<UParticleModuleKill>(SelectedLOD); break;
					case EParticleModuleClass::EventGenerator:       NewModule = GUObjectArray.CreateObject<UParticleModuleEventGenerator>(SelectedLOD); break;
					case EParticleModuleClass::EventReceiverSpawn:   NewModule = GUObjectArray.CreateObject<UParticleModuleEventReceiverSpawn>(SelectedLOD); break;
					case EParticleModuleClass::EventReceiverKillAll: NewModule = GUObjectArray.CreateObject<UParticleModuleEventReceiverKillAll>(SelectedLOD); break;
					case EParticleModuleClass::SubImageIndex:        NewModule = GUObjectArray.CreateObject<UParticleModuleSubImageIndex>(SelectedLOD); break;
					case EParticleModuleClass::SubUVMovie:           NewModule = GUObjectArray.CreateObject<UParticleModuleSubUVMovie>(SelectedLOD); break;
					case EParticleModuleClass::Light:                NewModule = GUObjectArray.CreateObject<UParticleModuleLight>(SelectedLOD); break;
					case EParticleModuleClass::VectorField:          NewModule = GUObjectArray.CreateObject<UParticleModuleVectorField>(SelectedLOD); break;
					case EParticleModuleClass::Camera:               NewModule = GUObjectArray.CreateObject<UParticleModuleCamera>(SelectedLOD); break;
					case EParticleModuleClass::Parameter:            NewModule = GUObjectArray.CreateObject<UParticleModuleParameter>(SelectedLOD); break;
					default: break;
					}
					if (NewModule)
					{
						SelectedLOD->AddModule(NewModule);
						SelectedLOD->CacheModules();
						Asset->CacheSystemModuleInfo();
						SelectedModule = NewModule;
						SyncEditedLODSelection();
						MarkDirty();
					}
				}
				ImGui::EndDisabled();
			}
			ImGui::EndMenu();
		}
		ImGui::BeginDisabled(!bCanRemoveModule);
		if (ImGui::Selectable("Remove Module"))
		{
			RemoveSelectedModule();
		}
		ImGui::EndDisabled();
		ImGui::EndCombo();
	}
	ImGui::EndDisabled(); // Module BeginDisabled 종료
	ImGui::SameLine();

	// ── Stat 토글 버튼 ───────────────────────────────────────────────
	{
		const bool bStatActive = bShowPreviewStats;
		if (bStatActive)
		{
			const ImVec4 ActiveCol = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
			ImGui::PushStyleColor(ImGuiCol_Button, ActiveCol);
		}
		if (ImGui::Button("Stat##PSStatToggle"))
			bShowPreviewStats = !bShowPreviewStats;
		if (bStatActive)
			ImGui::PopStyleColor();
	}

	// ── 우측 정렬 LOD 섹션 ───────────────────────────────────────────
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float ArrowW = ImGui::GetFrameHeight();
	const float RightSectionWidth =
		ImGui::CalcTextSize("Manage LOD").x + Style.FramePadding.x * 2.0f + ArrowW +
		ImGui::CalcTextSize("Select LOD").x + Style.FramePadding.x * 2.0f + ArrowW +
		ImGui::CalcTextSize("LOD :").x +
		54.0f +                          // InputInt 너비
		40.0f +                          // 화살표 두 개
		Style.ItemSpacing.x * 7.0f +
		24.0f;                           // 여유

	ImGui::SameLine();
	const float RightStartX = ImGui::GetCursorPosX() + (std::max)(0.0f, ImGui::GetContentRegionAvail().x - RightSectionWidth);
	ImGui::SetCursorPosX(RightStartX);

	// ── 5. Manage LOD 드롭다운 ───────────────────────────────────────
	ImGui::BeginDisabled(!bCanEditLODs);
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("Manage LOD").x + Style.FramePadding.x * 2.0f + ArrowW);
	if (ImGui::BeginCombo("##ManageLODCombo", "Manage LOD"))
	{
		if (ImGui::Selectable("Add Higher-detail LOD"))
			InsertLODRelativeToCurrent(true);
		if (ImGui::Selectable("Add Lower-detail LOD"))
			InsertLODRelativeToCurrent(false);
		ImGui::Separator();
		ImGui::BeginDisabled(!bCanDeleteLOD);
		if (ImGui::Selectable("Delete Current LOD"))
		{
			const int32 DeleteIndex = GetEditedLODIndex(Asset);
			for (UParticleEmitter* Emitter : Asset->GetEmitters())
			{
				if (Emitter && DeleteIndex < Emitter->GetLODLevelCount())
				{
					UParticleLODLevel* LODToDelete = Emitter->GetLODLevel(DeleteIndex);
					Emitter->RemoveLODLevel(DeleteIndex);
					if (LODToDelete)
					{
						GUObjectArray.RemoveObject(LODToDelete);
						GUObjectArray.DestroyObject(LODToDelete);
					}
					Emitter->RefreshLODLevelIndices();
				}
			}
			if (DeleteIndex < static_cast<int32>(Asset->LODDistances.size()))
				Asset->LODDistances.erase(Asset->LODDistances.begin() + DeleteIndex);
			EditedLODIndex = (std::max)(0, DeleteIndex - 1);
			SelectedModule = nullptr;
			Asset->CacheSystemModuleInfo();
			SyncEditedLODSelection();
			MarkDirty();
		}
		ImGui::EndDisabled();
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();

	// ── 6. Select LOD 드롭다운 ───────────────────────────────────────
	ImGui::BeginDisabled(!bCanEditLODs);
	ImGui::SetNextItemWidth(ImGui::CalcTextSize("Select LOD").x + Style.FramePadding.x * 2.0f + ArrowW);
	if (ImGui::BeginCombo("##SelectLODCombo", "Select LOD"))
	{
		ImGui::BeginDisabled(EditedLODIndex <= 0);
		if (ImGui::Selectable("Higher LOD"))
		{
			EditedLODIndex = (std::max)(0, EditedLODIndex - 1);
			SyncEditedLODSelection();
		}
		if (ImGui::Selectable("Highest LOD"))
		{
			EditedLODIndex = 0;
			SyncEditedLODSelection();
		}
		ImGui::EndDisabled();
		ImGui::Separator();
		ImGui::BeginDisabled(EditedLODIndex >= TotalLODCount - 1);
		if (ImGui::Selectable("Lower LOD"))
		{
			EditedLODIndex = (std::min)(TotalLODCount - 1, EditedLODIndex + 1);
			SyncEditedLODSelection();
		}
		if (ImGui::Selectable("Lowest LOD"))
		{
			EditedLODIndex = TotalLODCount - 1;
			SyncEditedLODSelection();
		}
		ImGui::EndDisabled();
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine(0.0f, 12.0f);

	// ── 7. LOD 입력칸 + 화살표 ───────────────────────────────────────
	ImGui::BeginDisabled(!bCanEditLODs);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("LOD :");
	ImGui::SameLine();
	int32 RequestedLODIndex = EditedLODIndex;
	ImGui::SetNextItemWidth(54.0f);
	if (ImGui::InputInt("##ParticleEditorLODIndex", &RequestedLODIndex, 0, 0) && TotalLODCount > 0)
	{
		EditedLODIndex = (std::clamp)(RequestedLODIndex, 0, TotalLODCount - 1);
		SyncEditedLODSelection();
	}
	ImGui::SameLine();
	ImGui::BeginGroup();
	if (ImGui::ArrowButton("##ParticleEditorLODUp", ImGuiDir_Up) && TotalLODCount > 0)
	{
		EditedLODIndex = (std::min)(TotalLODCount - 1, EditedLODIndex + 1);
		SyncEditedLODSelection();
	}
	ImGui::SameLine();
	if (ImGui::ArrowButton("##ParticleEditorLODDown", ImGuiDir_Down) && TotalLODCount > 0)
	{
		EditedLODIndex = (std::max)(0, EditedLODIndex - 1);
		SyncEditedLODSelection();
	}
	ImGui::EndGroup();
	ImGui::EndDisabled();

	struct FPanelLayout
	{
		ImVec2 ContentSize;
		float SplitWidth = 0.0f;
		float SplitHeight = 0.0f;
		float MinPanelRatio = 0.0f;
		float MinViewportRatio = 0.0f;
		float MinLowerPanelRatio = 0.0f;
		float LeftPanelWidth = 0.0f;
		float RightPanelWidth = 0.0f;
		float LeftTopPanelHeight = 0.0f;
		float LeftBottomPanelHeight = 0.0f;
		float RightTopPanelHeight = 0.0f;
		float RightBottomPanelHeight = 0.0f;
	} Layout;

	Layout.ContentSize = ImGui::GetContentRegionAvail();
	Layout.SplitWidth = (std::max)(0.0f, Layout.ContentSize.x - ParticleEditorSplitterThickness);
	Layout.SplitHeight = (std::max)(0.0f, Layout.ContentSize.y - ParticleEditorSplitterThickness);
	Layout.MinPanelRatio = Layout.SplitWidth > 0.0f ? (std::min)(ParticleEditorMinPanelWidth / Layout.SplitWidth, 0.5f) : 0.0f;
	Layout.MinViewportRatio = Layout.SplitHeight > 0.0f ? (std::min)(ParticleEditorMinViewportHeight / Layout.SplitHeight, 0.5f) : 0.0f;
	Layout.MinLowerPanelRatio = Layout.SplitHeight > 0.0f ? (std::min)(ParticleEditorMinLowerPanelHeight / Layout.SplitHeight, 0.5f) : 0.0f;

	LeftPanelRatio = (std::clamp)(LeftPanelRatio, Layout.MinPanelRatio, 1.0f - Layout.MinPanelRatio);
	LeftTopPanelRatio = (std::clamp)(LeftTopPanelRatio, Layout.MinViewportRatio, 1.0f - Layout.MinLowerPanelRatio);
	RightTopPanelRatio = (std::clamp)(RightTopPanelRatio, Layout.MinLowerPanelRatio, 1.0f - Layout.MinLowerPanelRatio);

	Layout.LeftPanelWidth = Layout.SplitWidth * LeftPanelRatio;
	Layout.RightPanelWidth = (std::max)(0.0f, Layout.SplitWidth - Layout.LeftPanelWidth);
	Layout.LeftTopPanelHeight = Layout.SplitHeight * LeftTopPanelRatio;
	Layout.LeftBottomPanelHeight = (std::max)(0.0f, Layout.SplitHeight - Layout.LeftTopPanelHeight);
	Layout.RightTopPanelHeight = Layout.SplitHeight * RightTopPanelRatio;
	Layout.RightBottomPanelHeight = (std::max)(0.0f, Layout.SplitHeight - Layout.RightTopPanelHeight);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ParticleEditorZeroSpacing);

	ImGui::BeginChild(
		"##ParticleEditorLeftColumn",
		ImVec2(Layout.LeftPanelWidth, Layout.ContentSize.y),
		false,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	{
		RenderPreviewViewport(ImVec2(Layout.LeftPanelWidth, Layout.LeftTopPanelHeight));

		const ImVec2 LeftHorizontalSplitterPos = ImGui::GetCursorScreenPos();
		ImRect LeftHorizontalSplitterRect(
			LeftHorizontalSplitterPos,
			ImVec2(LeftHorizontalSplitterPos.x + Layout.LeftPanelWidth, LeftHorizontalSplitterPos.y + ParticleEditorSplitterThickness));
		float LeftTopHeight = Layout.LeftTopPanelHeight;
		float LeftBottomHeight = Layout.LeftBottomPanelHeight;
		if (ImGui::SplitterBehavior(
				LeftHorizontalSplitterRect,
				ImGui::GetID("##ParticleEditorLeftHorizontalSplitter"),
				ImGuiAxis_Y,
				&LeftTopHeight,
				&LeftBottomHeight,
				ParticleEditorMinViewportHeight,
				ParticleEditorMinLowerPanelHeight) &&
			Layout.SplitHeight > 0.0f)
		{
			LeftTopPanelRatio = (std::clamp)(LeftTopHeight / Layout.SplitHeight, Layout.MinViewportRatio, 1.0f - Layout.MinLowerPanelRatio);
		}
		ImGui::Dummy(ImVec2(Layout.LeftPanelWidth, ParticleEditorSplitterThickness));

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ParticlePanelBodyColor);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticlePanelContentPadding);
		ImGui::BeginChild("Details", ImVec2(Layout.LeftPanelWidth, Layout.LeftBottomPanelHeight), true);
		bChanged |= RenderDetailsPanel();
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();

	ImGui::SameLine();
	const ImVec2 VerticalSplitterPos = ImGui::GetCursorScreenPos();
	ImRect VerticalSplitterRect(
		VerticalSplitterPos,
		ImVec2(VerticalSplitterPos.x + ParticleEditorSplitterThickness, VerticalSplitterPos.y + Layout.ContentSize.y));
	float LeftWidth = Layout.LeftPanelWidth;
	float RightWidth = Layout.RightPanelWidth;
	if (ImGui::SplitterBehavior(
			VerticalSplitterRect,
			ImGui::GetID("##ParticleEditorVerticalSplitter"),
			ImGuiAxis_X,
			&LeftWidth,
			&RightWidth,
			ParticleEditorMinPanelWidth,
			ParticleEditorMinPanelWidth) &&
		Layout.SplitWidth > 0.0f)
	{
		LeftPanelRatio = (std::clamp)(LeftWidth / Layout.SplitWidth, Layout.MinPanelRatio, 1.0f - Layout.MinPanelRatio);
	}
	ImGui::Dummy(ImVec2(ParticleEditorSplitterThickness, Layout.ContentSize.y));

	ImGui::SameLine();
	ImGui::BeginChild(
		"##ParticleEditorRightColumn",
		ImVec2(Layout.RightPanelWidth, Layout.ContentSize.y),
		false,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ParticlePanelBodyColor);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticlePanelContentPadding);
		ImGui::BeginChild("Emitters", ImVec2(Layout.RightPanelWidth, Layout.RightTopPanelHeight), true);
		bChanged |= RenderEmittersPanel();
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		const ImVec2 RightHorizontalSplitterPos = ImGui::GetCursorScreenPos();
		ImRect RightHorizontalSplitterRect(
			RightHorizontalSplitterPos,
			ImVec2(RightHorizontalSplitterPos.x + Layout.RightPanelWidth, RightHorizontalSplitterPos.y + ParticleEditorSplitterThickness));
		float RightTopHeight = Layout.RightTopPanelHeight;
		float RightBottomHeight = Layout.RightBottomPanelHeight;
		if (ImGui::SplitterBehavior(
				RightHorizontalSplitterRect,
				ImGui::GetID("##ParticleEditorRightHorizontalSplitter"),
				ImGuiAxis_Y,
				&RightTopHeight,
				&RightBottomHeight,
				ParticleEditorMinLowerPanelHeight,
				ParticleEditorMinLowerPanelHeight) &&
			Layout.SplitHeight > 0.0f)
		{
			RightTopPanelRatio = (std::clamp)(RightTopHeight / Layout.SplitHeight, Layout.MinLowerPanelRatio, 1.0f - Layout.MinLowerPanelRatio);
		}
		ImGui::Dummy(ImVec2(Layout.RightPanelWidth, ParticleEditorSplitterThickness));

		ImGui::PushStyleColor(ImGuiCol_ChildBg, ParticlePanelBodyColor);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticlePanelContentPadding);
		ImGui::BeginChild("Curve Editor", ImVec2(Layout.RightPanelWidth, Layout.RightBottomPanelHeight), true);
		bChanged |= RenderCurveEditorPanel();
		ImGui::EndChild();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();

	ImGui::End();

	if (bChanged)
	{
		MarkDirty();
	}

	if (!bWindowOpen)
	{
		bPendingClose = true;
	}
}

void FParticleSystemEditorWidget::RenderPreviewViewport(const ImVec2& Size)
{
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ParticlePanelBodyColor);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticlePanelContentPadding);
	ImGui::BeginChild("Viewport", Size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 HeaderMin = ImGui::GetCursorScreenPos();
	const float Width = ImGui::GetContentRegionAvail().x;
	DrawList->AddRectFilled(HeaderMin, ImVec2(HeaderMin.x + Width, HeaderMin.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ParticlePanelHeaderColor));
	DrawList->AddRectFilled(HeaderMin, ImVec2(HeaderMin.x + ParticlePanelAccentWidth, HeaderMin.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ParticlePanelAccentColor));
	DrawList->AddText(ImVec2(HeaderMin.x + ParticlePanelTitleOffsetX, HeaderMin.y + ParticlePanelTitleOffsetY), ImGui::GetColorU32(ImGuiCol_Text), "Viewport");
	ImGui::Dummy(ImVec2(Width, ParticlePanelHeaderHeight + ParticlePanelItemSpacing.y));

	const ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
	const ImVec2 ViewportSize = ImGui::GetContentRegionAvail();
	ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, ViewportSize.x, ViewportSize.y);

	FViewport* VP = ViewportClient.GetViewport();
	if (VP && ViewportSize.x > 0.0f && ViewportSize.y > 0.0f)
	{
		VP->RequestResize(static_cast<uint32>(ViewportSize.x), static_cast<uint32>(ViewportSize.y));
		ViewportWindow.SetRect({ViewportPos.x, ViewportPos.y, ViewportSize.x, ViewportSize.y});

		if (VP->GetSRV())
		{
			ImGui::Image((ImTextureID)VP->GetSRV(), ViewportSize);
		}

		// --- View / Time overlay buttons (top-left of viewport) ---
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f, 1.f, 1.f, 0.15f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.f, 1.f, 1.f, 0.30f));

		constexpr float OverlayBtnY = 6.0f;
		constexpr float OverlayBtnSpacing = 4.0f;

		ImGui::SetCursorScreenPos(ImVec2(ViewportPos.x + 8.0f, ViewportPos.y + OverlayBtnY));
		if (ImGui::Button("View##PSView"))
			ImGui::OpenPopup("##PSViewPopup");
		const float TimeBtnX = ViewportPos.x + 8.0f + ImGui::GetItemRectSize().x + OverlayBtnSpacing;

		ImGui::SetCursorScreenPos(ImVec2(TimeBtnX, ViewportPos.y + OverlayBtnY));
		if (ImGui::Button("Time##PSTime"))
			ImGui::OpenPopup("##PSTimePopup");

		ImGui::PopStyleColor(3);

		// Shared popup style constants
		constexpr ImVec4 PopupBgColor      = ImVec4(0.14f, 0.14f, 0.14f, 0.98f);
		constexpr ImVec4 AccentCyan        = ImVec4(0.0f,  0.78f, 0.95f, 1.0f);
		constexpr ImVec4 FrameBg           = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);
		constexpr ImVec4 FrameBgHov        = ImVec4(0.32f, 0.32f, 0.32f, 1.0f);
		constexpr ImVec4 FrameBgAct        = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
		constexpr ImVec4 MenuHoverBg       = ImVec4(0.0f,  0.71f, 0.86f, 0.22f);
		constexpr ImVec2 PopupPad          = ImVec2(14.0f, 10.0f);
		constexpr ImVec2 PopupItemSpc      = ImVec2(8.0f,  9.0f);
		constexpr ImVec2 PopupFramePad     = ImVec2(4.0f,  4.0f);

		// View popup (ShowFlags)
		ImGui::PushStyleColor(ImGuiCol_PopupBg, PopupBgColor);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, PopupPad);
		if (ImGui::BeginPopup("##PSViewPopup"))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  PopupItemSpc);
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, PopupFramePad);
			ImGui::PushStyleColor(ImGuiCol_FrameBg,        FrameBg);
			ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, FrameBgHov);
			ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  FrameBgAct);
			ImGui::PushStyleColor(ImGuiCol_CheckMark,      AccentCyan);

			FViewportRenderOptions& RO = ViewportClient.GetRenderOptions();

			ImGui::TextDisabled("Mesh");
			ImGui::Indent(8.0f);
			ImGui::Checkbox("StaticMesh",       &RO.ShowFlags.bStaticMesh);
			ImGui::Unindent(8.0f);

			ImGui::Spacing();
			ImGui::TextDisabled("Display");
			ImGui::Indent(8.0f);
			ImGui::Checkbox("Grid",             &RO.ShowFlags.bGrid);
			ImGui::Checkbox("World Axis",       &RO.ShowFlags.bWorldAxis);
			ImGui::Checkbox("Bounding Volume",  &RO.ShowFlags.bBoundingVolume);
			ImGui::Checkbox("Collision Shape",  &RO.ShowFlags.bShowCollisionShape);
			ImGui::Checkbox("Debug Draw",       &RO.ShowFlags.bDebugDraw);
			ImGui::Unindent(8.0f);

			ImGui::Spacing();
			ImGui::TextDisabled("Post Process");
			ImGui::Indent(8.0f);
			ImGui::Checkbox("FXAA",             &RO.ShowFlags.bFXAA);
			ImGui::Checkbox("Gamma Correction", &RO.ShowFlags.bGammaCorrection);
			ImGui::Unindent(8.0f);

			ImGui::PopStyleColor(4);
			ImGui::PopStyleVar(2);
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		// Time popup
		ImGui::PushStyleColor(ImGuiCol_PopupBg,      PopupBgColor);
		ImGui::PushStyleColor(ImGuiCol_Header,        MenuHoverBg);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, MenuHoverBg);
		ImGui::PushStyleColor(ImGuiCol_CheckMark,     AccentCyan);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, PopupPad);
		if (ImGui::BeginPopup("##PSTimePopup"))
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, PopupItemSpc);

			if (ImGui::MenuItem("Play/Pause", nullptr, !bAnimPaused))
				bAnimPaused = !bAnimPaused;
			if (ImGui::MenuItem("Realtime",   nullptr, bAnimRealtime))
				bAnimRealtime = !bAnimRealtime;
			if (ImGui::MenuItem("Loop",       nullptr, bAnimLoop))
				bAnimLoop = !bAnimLoop;

			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();

			if (ImGui::BeginMenu("AnimSpeed"))
			{
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, PopupItemSpc);
				ImGui::TextColored(AccentCyan, "AnimSpeed");
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();

				struct FSpeedEntry { const char* Label; float Speed; };
				constexpr FSpeedEntry Speeds[] = {
					{ "100%", 1.0f }, { "50%", 0.5f }, { "25%", 0.25f }, { "10%", 0.1f }, { "1%", 0.01f }
				};
				for (const FSpeedEntry& S : Speeds)
				{
					if (ImGui::MenuItem(S.Label, nullptr, AnimSpeedScale == S.Speed))
						AnimSpeedScale = S.Speed;
				}
				ImGui::PopStyleVar();
				ImGui::EndMenu();
			}

			ImGui::PopStyleVar();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(4);

		// Right-side toolbar (ViewMode only; ShowFlags moved to View button above)
		FViewportToolbarContext Context;
		Context.Renderer = &GEngine->GetRenderer();
		Context.Settings = &FEditorSettings::Get().MeshEditorViewportSettings;
		Context.RenderOptions = &ViewportClient.GetRenderOptions();
		Context.ToolbarLeft = ViewportPos.x;
		Context.ToolbarTop = ViewportPos.y;
		Context.ToolbarWidth = ViewportSize.x;
		Context.bReservePlayStopSpace = false;
		Context.bShowAddActor = false;
		Context.bShowGizmoControls = false;
		Context.bShowCameraControls = false;
		Context.bShowShowFlags = false;
		Context.bShowViewMode = false;

		FViewportToolbar::Render(Context);

		if (bShowPreviewStats)
			RenderPreviewStatsOverlay(ViewportPos, ViewportSize);
	}

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void FParticleSystemEditorWidget::RenderPreviewStatsOverlay(const ImVec2& ViewportPos, const ImVec2& ViewportSize) const
{
	UParticleSystemComponent* PreviewComponent = GetPreviewParticleComponent();
	if (!PreviewComponent || ViewportSize.x <= 0.0f || ViewportSize.y <= 0.0f)
		return;

	const FParticlePreviewStats Stats = GatherParticlePreviewStats(PreviewComponent);
	const FStatEntry* TickEntry = FindParticleStatEntry("ParticleSystemComponent::Tick");

	// ── 행 구성 ───────────────────────────────────────────────────────
	struct FRow { char Label[24]; char Value[96]; };
	FRow Rows[6] = {};
	int32 RowCount = 0;

	auto AddRow = [&](const char* Label, const char* Fmt, ...)
	{
		FRow& R = Rows[RowCount++];
		strncpy_s(R.Label, Label, sizeof(R.Label) - 1);
		va_list Args; va_start(Args, Fmt);
		vsnprintf(R.Value, sizeof(R.Value), Fmt, Args);
		va_end(Args);
	};

	// Playback : Playing 0.50x | 0.42 s | Loop
	{
		const char* Pb = bAnimPaused ? "Paused" : "Playing";
		const float  T = PreviewComponent->GetSystemElapsedTime();
		if (bAnimLoop)
			AddRow("Playback", "%s  %.2fx  |  %.2f s  |  Loop", Pb, AnimSpeedScale, T);
		else
			AddRow("Playback", "%s  %.2fx  |  %.2f s", Pb, AnimSpeedScale, T);
	}

	AddRow("LOD",       "%d", EditedLODIndex);
	AddRow("Emitters",  "%d / %d active", Stats.ActiveEmitters, Stats.TotalEmitters);
	AddRow("Particles", "%d alive / %d max", Stats.ActiveParticles, Stats.MaxParticles);
	AddRow("Frame",     "+%d spawn  /  -%d kill  /  %d events",
		PreviewComponent->GetTotalSpawnedThisFrame(),
		PreviewComponent->GetTotalKilledThisFrame(),
		Stats.QueuedEvents);
	AddRow("CPU",       "%.3f ms", TickEntry ? TickEntry->LastTime * 1000.0 : 0.0);

	// ── 레이아웃 ──────────────────────────────────────────────────────
	constexpr float PadX         = 12.0f;
	constexpr float PadY         = 10.0f;
	constexpr float LineSpacing  = 4.0f;
	constexpr float TopOffset    = 6.0f;
	constexpr float RightOffset  = 10.0f;
	constexpr float MinWidth     = 220.0f;
	constexpr float ColGap       = 14.0f;

	const float LineH = ImGui::GetTextLineHeight();

	float MaxLabelW = 0.0f, MaxValueW = 0.0f;
	for (int32 i = 0; i < RowCount; ++i)
	{
		MaxLabelW = (std::max)(MaxLabelW, ImGui::CalcTextSize(Rows[i].Label).x);
		MaxValueW = (std::max)(MaxValueW, ImGui::CalcTextSize(Rows[i].Value).x);
	}

	const float ContentW  = MaxLabelW + ColGap + MaxValueW;
	const float PanelW    = (std::max)(MinWidth, ContentW + PadX * 2.0f);
	const float TitleH    = LineH + 4.0f;
	const float PanelH    = PadY * 2.0f + TitleH
		+ LineH * static_cast<float>(RowCount)
		+ LineSpacing * static_cast<float>(RowCount - 1);

	const ImVec2 PMin(ViewportPos.x + ViewportSize.x - PanelW - RightOffset, ViewportPos.y + TopOffset);
	const ImVec2 PMax(PMin.x + PanelW, PMin.y + PanelH);

	// ── 렌더링 ────────────────────────────────────────────────────────
	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->AddRectFilled(PMin, PMax, IM_COL32(10, 12, 16, 215), 6.0f);
	DL->AddRect(PMin, PMax, IM_COL32(0, 181, 219, 180), 6.0f, 0, 1.0f);
	DL->AddText(ImVec2(PMin.x + PadX, PMin.y + PadY), IM_COL32(170, 220, 235, 255), "Preview Stats");

	const float LabelX   = PMin.x + PadX;
	const float ValueX   = LabelX + MaxLabelW + ColGap;
	const ImU32 ColLabel = IM_COL32(175, 175, 175, 255);
	const ImU32 ColValue = IM_COL32(235, 235, 235, 255);
	const ImU32 ColCPU   = IM_COL32(132, 214, 255, 255);

	float CurY = PMin.y + PadY + TitleH;
	for (int32 i = 0; i < RowCount; ++i)
	{
		DL->AddText(ImVec2(LabelX, CurY), ColLabel, Rows[i].Label);
		DL->AddText(ImVec2(ValueX, CurY), (i == RowCount - 1) ? ColCPU : ColValue, Rows[i].Value);
		CurY += LineH + LineSpacing;
	}
}

UParticleSystemComponent* FParticleSystemEditorWidget::GetPreviewParticleComponent() const
{
	return ViewportClient.GetPreviewComponent();
}

void FParticleSystemEditorWidget::ReplayPreviewParticleSystem()
{
	if (UParticleSystemComponent* PreviewComponent = GetPreviewParticleComponent())
	{
		PreviewComponent->SetLODLevel(EditedLODIndex);
		if (UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject))
		{
			PreviewComponent->SetTemplate(Asset);
		}
		PreviewComponent->DeactivateSystem();
		PreviewComponent->ActivateSystem();
	}
}

int32 FParticleSystemEditorWidget::GetEditedLODIndex(const UParticleSystem* Asset) const
{
	const int32 LODCount = GetParticleSystemLODCount(Asset);
	if (LODCount <= 0)
	{
		return 0;
	}

	return (std::clamp)(EditedLODIndex, 0, LODCount - 1);
}

UParticleLODLevel* FParticleSystemEditorWidget::GetEditedLODLevel(UParticleEmitter* Emitter) const
{
	return Emitter ? Emitter->GetLODLevel(GetEditedLODIndex(Cast<UParticleSystem>(EditedObject))) : nullptr;
}

const UParticleLODLevel* FParticleSystemEditorWidget::GetEditedLODLevel(const UParticleEmitter* Emitter) const
{
	return Emitter ? Emitter->GetLODLevel(GetEditedLODIndex(Cast<UParticleSystem>(EditedObject))) : nullptr;
}

void FParticleSystemEditorWidget::SyncEditedLODSelection(bool bRefreshPreview)
{
	UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject);
	EditedLODIndex = GetEditedLODIndex(Asset);

	if (!Asset)
	{
		SelectedModule = nullptr;
		return;
	}

	if (SelectedEmitterIndex < 0 || SelectedEmitterIndex >= static_cast<int32>(Asset->GetEmitters().size()))
	{
		SelectedEmitterIndex = -1;
		SelectedModule = nullptr;
	}
	else if (SelectedModule)
	{
		UParticleEmitter* SelectedEmitter = Asset->GetEmitter(SelectedEmitterIndex);
		UParticleLODLevel* CurrentLOD = GetEditedLODLevel(SelectedEmitter);
		bool bSelectionStillValid = false;
		if (CurrentLOD)
		{
			if (SelectedModule == CurrentLOD->GetRequiredModule() || SelectedModule == CurrentLOD->GetTypeDataModule())
			{
				bSelectionStillValid = true;
			}
			else
			{
				for (UParticleModule* Module : CurrentLOD->GetModules())
				{
					if (Module == SelectedModule)
					{
						bSelectionStillValid = true;
						break;
					}
				}
			}
		}

		if (!bSelectionStillValid)
		{
			SelectedModule = nullptr;
		}
	}

	if (bRefreshPreview)
	{
		if (UParticleSystemComponent* PreviewComponent = GetPreviewParticleComponent())
		{
			PreviewComponent->SetLODLevel(EditedLODIndex);
			PreviewComponent->SetTemplate(Asset);
		}
	}
}

bool FParticleSystemEditorWidget::InsertLODRelativeToCurrent(bool bInsertAfter)
{
	UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject);
	if (!Asset)
	{
		return false;
	}

	const int32 CurrentLODCount = GetParticleSystemLODCount(Asset);
	if (CurrentLODCount <= 0)
	{
		return false;
	}

	const int32 SourceLODIndex = GetEditedLODIndex(Asset);
	const int32 InsertLODIndex = bInsertAfter ? (SourceLODIndex + 1) : SourceLODIndex;

	for (UParticleEmitter* Emitter : Asset->GetEmitters())
	{
		if (!Emitter)
		{
			continue;
		}

		UParticleLODLevel* SourceLOD = Emitter->GetLODLevel(SourceLODIndex);
		if (!SourceLOD && Emitter->GetLODLevelCount() > 0)
		{
			SourceLOD = Emitter->GetLODLevel(Emitter->GetLODLevelCount() - 1);
		}

		UParticleLODLevel* NewLOD = SourceLOD
			? Cast<UParticleLODLevel>(SourceLOD->Duplicate(Emitter))
			: CreateDefaultParticleLODLevel(Emitter, InsertLODIndex);
		if (!NewLOD)
		{
			continue;
		}

		Emitter->InsertLODLevel(InsertLODIndex, NewLOD);
		NewLOD->CacheModules();
	}

	EnsureParticleSystemLODDistanceCount(Asset, CurrentLODCount);
	const float InsertDistance =
		(InsertLODIndex < static_cast<int32>(Asset->LODDistances.size()))
			? Asset->LODDistances[InsertLODIndex]
			: GetDefaultParticleLODDistance(InsertLODIndex);
	Asset->LODDistances.insert(Asset->LODDistances.begin() + InsertLODIndex, InsertDistance);
	EditedLODIndex = InsertLODIndex;
	SelectedModule = nullptr;

	Asset->CacheSystemModuleInfo();
	SyncEditedLODSelection();
	MarkDirty();
	return true;
}

bool FParticleSystemEditorWidget::RemoveSelectedModule()
{
	UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject);
	if (!Asset || !SelectedModule || SelectedEmitterIndex < 0)
	{
		return false;
	}

	if (IsParticleModuleFixedInStack(SelectedModule->GetModuleClass()))
	{
		return false;
	}

	UParticleEmitter* SelectedEmitter = Asset->GetEmitter(SelectedEmitterIndex);
	UParticleLODLevel* SelectedLOD = GetEditedLODLevel(SelectedEmitter);
	if (!SelectedLOD)
	{
		return false;
	}

	const int32 SelectedModuleIndex = FindParticleModuleIndex(SelectedLOD, SelectedModule);
	if (SelectedModuleIndex < 0)
	{
		return false;
	}

	UParticleModule* RemovedModule = SelectedModule;
	SelectedModule = nullptr;
	SelectedLOD->RemoveModule(SelectedModuleIndex);
	GUObjectArray.RemoveObject(RemovedModule);
	GUObjectArray.DestroyObject(RemovedModule);
	SelectedLOD->CacheModules();
	Asset->CacheSystemModuleInfo();
	SyncEditedLODSelection();
	MarkDirty();
	return true;
}

bool FParticleSystemEditorWidget::RenderDetailsPanel()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ParticlePanelItemSpacing);

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Start = ImGui::GetCursorScreenPos();
	const float Width = ImGui::GetContentRegionAvail().x;
	DrawList->AddRectFilled(Start, ImVec2(Start.x + Width, Start.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ParticlePanelHeaderColor));
	DrawList->AddRectFilled(Start, ImVec2(Start.x + ParticlePanelAccentWidth, Start.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ParticlePanelAccentColor));
	DrawList->AddText(ImVec2(Start.x + ParticlePanelTitleOffsetX, Start.y + ParticlePanelTitleOffsetY), ImGui::GetColorU32(ImGuiCol_Text), "Details");
	ImGui::Dummy(ImVec2(Width, ParticleDetailsHeaderSpacing));

	UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject);

	UObject* DetailsObject = Asset;
	if (SelectedModule)
	{
		DetailsObject = SelectedModule;
	}
	else if (SelectedEmitterIndex >= 0 && SelectedEmitterIndex < static_cast<int32>(Asset->GetEmitters().size()))
	{
		DetailsObject = Asset->GetEmitter(SelectedEmitterIndex);
	}

	if (SelectedModule && EventGeneratorRequiresEnabledCollisionModule(SelectedModule))
	{
		UParticleEmitter* SelectedEmitter = (SelectedEmitterIndex >= 0 && SelectedEmitterIndex < static_cast<int32>(Asset->GetEmitters().size()))
			? Asset->GetEmitter(SelectedEmitterIndex)
			: nullptr;
		const UParticleLODLevel* SelectedLOD = GetEditedLODLevel(SelectedEmitter);
		if (!HasEnabledCollisionModule(SelectedLOD))
		{
			ImGui::TextColored(
				ImVec4(1.0f, 0.72f, 0.22f, 1.0f),
				"Warning: Collision event entries require an enabled Collision module in this LOD.");
			ImGui::Separator();
		}
	}

	bool bChanged = RenderEditableProperties(DetailsObject);

	if (SelectedModule)
	{
		bChanged |= RenderParticleDistribution(SelectedModule);
	}

	ImGui::PopStyleVar();
	return bChanged;
}

bool FParticleSystemEditorWidget::RenderParticleDistribution(UParticleModule* Module)
{
	if (!Module)
	{
		return false;
	}

	bool bChanged = false;
	int32 CurvePropertyChannelIdx = 0;
	auto DrawType = [&bChanged](EDistributionType& Type)
	{
		ImGui::PushID("DistributionType");
		const char* Names[] = { "Constant", "Uniform", "ConstantCurve", "UniformCurve" };
		int32 Current = static_cast<int32>(Type);
		if (Current < 0 || Current >= 4)
		{
			Current = 0;
		}
		ImGui::TextUnformatted("Type");
		ImGui::NextColumn();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::Combo("##DistributionType", &Current, Names, 4))
		{
			Type = static_cast<EDistributionType>(Current);
			bChanged = true;
		}
		ImGui::NextColumn();
		ImGui::PopID();
	};
	auto DrawFloat = [&bChanged](const char* Name, float& Value)
	{
		ImGui::PushID(Name);
		ImGui::TextUnformatted(Name);
		ImGui::NextColumn();
		ImGui::SetNextItemWidth(-1.0f);
		bChanged |= ImGui::DragFloat("##Value", &Value, 0.01f, 0.0f, 0.0f, "%.4f");
		ImGui::NextColumn();
		ImGui::PopID();
	};
	auto DrawVector = [&bChanged](const char* Name, FVector& Value)
	{
		ImGui::PushID(Name);
		ImGui::TextUnformatted(Name);
		ImGui::NextColumn();
		ImGui::SetNextItemWidth(-1.0f);
		bChanged |= ImGui::DragFloat3("##Value", &Value.X, 0.01f, 0.0f, 0.0f, "%.4f");
		ImGui::NextColumn();
		ImGui::PopID();
	};
	auto DrawColor = [&bChanged](const char* Name, FLinearColor& Value)
	{
		ImGui::PushID(Name);
		float Color[4] = { Value.R, Value.G, Value.B, Value.A };
		ImGui::TextUnformatted(Name);
		ImGui::NextColumn();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::DragFloat4("##Value", Color, 0.01f, 0.0f, 0.0f, "%.4f"))
		{
			Value = FLinearColor(Color[0], Color[1], Color[2], Color[3]);
			bChanged = true;
		}
		ImGui::NextColumn();
		ImGui::PopID();
	};
	auto DrawBool = [&bChanged](const char* Name, bool& Value)
	{
		ImGui::PushID(Name);
		ImGui::TextUnformatted(Name);
		ImGui::NextColumn();
		bChanged |= ImGui::Checkbox("##Value", &Value);
		ImGui::NextColumn();
		ImGui::PopID();
	};
	auto DrawLockedAxesMode = [&bChanged](const char* Name, EParticleLockedAxesMode& Value)
	{
		ImGui::PushID(Name);
		ImGui::TextUnformatted(Name);
		ImGui::NextColumn();
		const char* Labels[] = { "None", "XY", "XZ", "YZ", "XYZ" };
		int32 Current = static_cast<int32>(Value);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::Combo("##Value", &Current, Labels, IM_ARRAYSIZE(Labels)))
		{
			Value = static_cast<EParticleLockedAxesMode>(Current);
			bChanged = true;
		}
		ImGui::NextColumn();
		ImGui::PopID();
	};
	auto DrawMirrorFlags = [&bChanged](const char* Name, uint8& Flags)
	{
		ImGui::PushID(Name);
		ImGui::TextUnformatted(Name);
		ImGui::NextColumn();

		bool bMirrorX = (Flags & PMF_X) != 0;
		bool bMirrorY = (Flags & PMF_Y) != 0;
		bool bMirrorZ = (Flags & PMF_Z) != 0;

		if (ImGui::Checkbox("X##Mirror", &bMirrorX))
		{
			Flags = bMirrorX ? (Flags | PMF_X) : (Flags & ~PMF_X);
			bChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Y##Mirror", &bMirrorY))
		{
			Flags = bMirrorY ? (Flags | PMF_Y) : (Flags & ~PMF_Y);
			bChanged = true;
		}
		ImGui::SameLine();
		if (ImGui::Checkbox("Z##Mirror", &bMirrorZ))
		{
			Flags = bMirrorZ ? (Flags | PMF_Z) : (Flags & ~PMF_Z);
			bChanged = true;
		}

		ImGui::NextColumn();
		ImGui::PopID();
	};
	auto DrawFloatCurve = [this, &bChanged, &CurvePropertyChannelIdx](const char* Name, FParticleFloatCurve& Curve)
	{
		const int32 ThisChannelIdx = CurvePropertyChannelIdx++;
		const bool bSelectMaxCurve = std::strstr(Name, "(Max)") != nullptr;
		ImGui::PushID(Name);
		ImGui::TextUnformatted(Name);
		ImGui::NextColumn();
		const float CurveButtonWidth = 96.0f;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0f, ImGui::GetContentRegionAvail().x - CurveButtonWidth));
		if (ImGui::Button("Curve Editor", ImVec2(CurveButtonWidth, 0.0f)))
		{
			CurveChannelIdx = ThisChannelIdx;
			CurveSelectedKey = -1;
			CurveSelCurve = bSelectMaxCurve ? 1 : 0;
			PFitCurveViewToKeys(&Curve, nullptr, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV);
		}
		ImGui::NextColumn();

		ImGui::TextUnformatted("Key Count");
		ImGui::NextColumn();
		ImGui::Text("%d", static_cast<int32>(Curve.Keys.size()));
		ImGui::SameLine();
		if (ImGui::Button("Add Key"))
		{
			const float Time = Curve.Keys.empty() ? 0.0f : Curve.Keys.back().Time + 0.1f;
			const float Value = Curve.Keys.empty() ? 0.0f : Curve.Keys.back().Value;
			Curve.AddKey(Time, Value);
			bChanged = true;
		}
		ImGui::NextColumn();

		if (!Curve.Keys.empty())
		{
			ImGui::TextDisabled("Fields");
			ImGui::NextColumn();
			ImGui::TextDisabled("T / V / AT / LT");
			ImGui::NextColumn();
		}

		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve.Keys.size()); ++KeyIndex)
		{
			FFloatCurveKey& Key = Curve.Keys[KeyIndex];
			ImGui::PushID(KeyIndex);
			char Label[64];
			snprintf(Label, sizeof(Label), "Key %d", KeyIndex);
			ImGui::TextUnformatted(Label);
			ImGui::NextColumn();
			float Values[4] = { Key.Time, Key.Value, Key.ArriveTangent, Key.LeaveTangent };
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::DragFloat4("##Key", Values, 0.01f, 0.0f, 0.0f, "%.3f"))
			{
				Key.Time = Values[0];
				Key.Value = Values[1];
				Key.ArriveTangent = Values[2];
				Key.LeaveTangent = Values[3];
				if (Key.TangentMode == EParticleCurveTangentMode::User)
				{
					Key.LeaveTangent = Key.ArriveTangent;
				}
				Curve.SortKeys();
				Curve.AutoSetTangents();
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove"))
			{
				Curve.Keys.erase(Curve.Keys.begin() + KeyIndex);
				Curve.AutoSetTangents();
				bChanged = true;
				ImGui::PopID();
				ImGui::NextColumn();
				break;
			}
			ImGui::NextColumn();
			ImGui::PopID();
		}
		ImGui::PopID();
	};

	// Helper to determine what fields to show based on distribution type
	auto FloatDistFields = [&](UDistributionFloat* Dist)
	{
		if (!Dist) return;
		const bool bCurve   = (Dist->Type == EDistributionType::ConstantCurve || Dist->Type == EDistributionType::UniformCurve);
		const bool bUniform = (Dist->Type == EDistributionType::Uniform        || Dist->Type == EDistributionType::UniformCurve);
		const bool bConstant = Dist->Type == EDistributionType::Constant;
		const bool bUniformRangeOnly = Dist->Type == EDistributionType::Uniform;
		DrawType(Dist->Type);
		DrawBool("Can be Baked", Dist->bCanBeBaked);
		if (bConstant) DrawFloat("Constant", Dist->Min);
		if (bUniformRangeOnly) DrawFloat("Min", Dist->Min);
		if (bUniformRangeOnly) DrawFloat("Max", Dist->Max);
		if (bCurve)
		{
			DrawBool("Is Looped", Dist->bIsLooped);
			DrawFloat("Loop Key Offset", Dist->LoopKeyOffset);
			DrawFloatCurve("Points (Min)", Dist->MinCurve);
		}
		if (bCurve && bUniform) DrawFloatCurve("Points (Max)", Dist->MaxCurve);
		if (bUniform) DrawBool("Use Extremes", Dist->bUseExtremes);
	};
	auto VectorDistFields = [&](UDistributionVector* Dist)
	{
		if (!Dist) return;
		const bool bCurve   = (Dist->Type == EDistributionType::ConstantCurve || Dist->Type == EDistributionType::UniformCurve);
		const bool bUniform = (Dist->Type == EDistributionType::Uniform        || Dist->Type == EDistributionType::UniformCurve);
		const bool bConstant = Dist->Type == EDistributionType::Constant;
		const bool bUniformRangeOnly = Dist->Type == EDistributionType::Uniform;
		DrawType(Dist->Type);
		DrawBool("Can be Baked", Dist->bCanBeBaked);
		DrawLockedAxesMode("Locked Axes", Dist->LockedAxesMode);
		if (bConstant) DrawVector("Constant", Dist->Min);
		if (bUniformRangeOnly) DrawVector("Min", Dist->Min);
		if (bUniformRangeOnly) DrawVector("Max", Dist->Max);
		if (bCurve)
		{
			DrawBool("Is Looped", Dist->bIsLooped);
			DrawFloat("Loop Key Offset", Dist->LoopKeyOffset);
			DrawFloatCurve("Points.X (Min)", Dist->MinCurve.X);
			DrawFloatCurve("Points.Y (Min)", Dist->MinCurve.Y);
			DrawFloatCurve("Points.Z (Min)", Dist->MinCurve.Z);
		}
		if (bCurve && bUniform)
		{
			DrawFloatCurve("Points.X (Max)", Dist->MaxCurve.X);
			DrawFloatCurve("Points.Y (Max)", Dist->MaxCurve.Y);
			DrawFloatCurve("Points.Z (Max)", Dist->MaxCurve.Z);
		}
		if (bUniform) DrawMirrorFlags("Mirror Flags", Dist->MirrorFlags);
		if (bUniform) DrawBool("Use Extremes", Dist->bUseExtremes);
	};
	auto ColorDistFields = [&](UDistributionLinearColor* Dist)
	{
		if (!Dist) return;
		const bool bCurve   = (Dist->Type == EDistributionType::ConstantCurve || Dist->Type == EDistributionType::UniformCurve);
		const bool bUniform = (Dist->Type == EDistributionType::Uniform        || Dist->Type == EDistributionType::UniformCurve);
		const bool bConstant = Dist->Type == EDistributionType::Constant;
		const bool bUniformRangeOnly = Dist->Type == EDistributionType::Uniform;
		DrawType(Dist->Type);
		DrawBool("Can be Baked", Dist->bCanBeBaked);
		if (bConstant) DrawColor("Constant", Dist->Min);
		if (bUniformRangeOnly) DrawColor("Min", Dist->Min);
		if (bUniformRangeOnly) DrawColor("Max", Dist->Max);
		if (bCurve)
		{
			DrawBool("Is Looped", Dist->bIsLooped);
			DrawFloat("Loop Key Offset", Dist->LoopKeyOffset);
			DrawFloatCurve("Points.R (Min)", Dist->MinCurve.R);
			DrawFloatCurve("Points.G (Min)", Dist->MinCurve.G);
			DrawFloatCurve("Points.B (Min)", Dist->MinCurve.B);
			DrawFloatCurve("Points.A (Min)", Dist->MinCurve.A);
		}
		if (bCurve && bUniform)
		{
			DrawFloatCurve("Points.R (Max)", Dist->MaxCurve.R);
			DrawFloatCurve("Points.G (Max)", Dist->MaxCurve.G);
			DrawFloatCurve("Points.B (Max)", Dist->MaxCurve.B);
			DrawFloatCurve("Points.A (Max)", Dist->MaxCurve.A);
		}
		if (bUniform) DrawBool("Use Extremes", Dist->bUseExtremes);
	};

	TArray<const FProperty*> Props;
	Module->GetEditableProperties(Props);

	const ImVec4 SectionBg      = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
	const ImVec4 SectionHovered = ImVec4(0.26f, 0.26f, 0.26f, 1.0f);
	const ImVec4 SectionActive  = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);

	bool bFoundDistribution = false;
	int32 DistIdx = 0;
	for (const FProperty* Prop : Props)
	{
		if (!ShouldDisplayParticleEditableProperty(Module, Prop))
		{
			continue;
		}
		if (!Prop || Prop->GetType() != EPropertyType::Distribution)
		{
			continue;
		}
		bFoundDistribution = true;

		const FDistributionProperty& DistProp = static_cast<const FDistributionProperty&>(*Prop);
		UObject* DistObject = DistProp.GetDistributionPropertyValue(const_cast<void*>(DistProp.ContainerPtrToValuePtr(Module)));

		ImGui::PushID(DistIdx++);
		ImGui::PushStyleColor(ImGuiCol_Header,        SectionBg);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, SectionHovered);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive,  SectionActive);
		ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
		const bool bOpen = ImGui::CollapsingHeader("Distribution", ImGuiTreeNodeFlags_DefaultOpen);
		ImGui::PopStyleColor(3);

		if (bOpen)
		{
			ImGui::Indent(6.0f);

			if (!DistObject)
			{
				ImGui::TextDisabled("Distribution is null");
			}
			else
			{
				const FString ColId = FString("##DistCols_") + Prop->Name;
				ImGui::Columns(2, ColId.c_str(), false);
				ImGui::SetColumnWidth(0, ParticleDetailsColumnWidth);

				if (UDistributionFloat* FloatDist = Cast<UDistributionFloat>(DistObject))
				{
					FloatDistFields(FloatDist);
				}
				else if (UDistributionVector* VectorDist = Cast<UDistributionVector>(DistObject))
				{
					VectorDistFields(VectorDist);
				}
				else if (UDistributionLinearColor* ColorDist = Cast<UDistributionLinearColor>(DistObject))
				{
					ColorDistFields(ColorDist);
				}

				ImGui::Columns(1);
			}

			ImGui::Unindent(6.0f);
		}

		ImGui::PopID();
	}

	if (!bFoundDistribution)
	{
		ImGui::TextDisabled("No distribution variables");
	}

	if (bChanged)
	{
		HandleEditedParticleProperty(Module, nullptr);
	}
	return bChanged;
}

bool FParticleSystemEditorWidget::RenderEditableProperties(UObject* Object)
{
	if (!Object)
	{
		return false;
	}

	TArray<const FProperty*> Props;
	Object->GetEditableProperties(Props);
	TArray<const FProperty*> VisibleProps;
	VisibleProps.reserve(Props.size());
	for (const FProperty* Prop : Props)
	{
		if (ShouldDisplayParticleEditableProperty(Object, Prop))
		{
			VisibleProps.push_back(Prop);
		}
	}
	if (VisibleProps.empty())
	{
		ImGui::TextDisabled("No editable properties.");
		return false;
	}

	// 카테고리별 그룹 수집 (최초 등장 순서 유지)
	std::vector<FString> CategoryOrder;
	for (const FProperty* Prop : VisibleProps)
	{
		if (!Prop || Prop->GetType() == EPropertyType::Distribution) continue;
		bool bFound = false;
		for (const FString& Cat : CategoryOrder)
		{
			if (Cat == Prop->Category) { bFound = true; break; }
		}
		if (!bFound) CategoryOrder.push_back(Prop->Category);
	}

	const bool bUseSections = !CategoryOrder.empty();

	bool bChanged = false;
	int32 GlobalID = 0;

	const ImVec4 SectionBg      = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
	const ImVec4 SectionHovered = ImVec4(0.26f, 0.26f, 0.26f, 1.0f);
	const ImVec4 SectionActive  = ImVec4(0.22f, 0.22f, 0.22f, 1.0f);

	auto RenderCategoryProps = [&](const FString& Category)
	{
		const FString ColID = FString("##Col_") + Category;
		ImGui::Columns(2, ColID.c_str(), false);
		ImGui::SetColumnWidth(0, ParticleDetailsColumnWidth);

		for (const FProperty* Prop : VisibleProps)
		{
			if (!Prop || Prop->Category != Category) continue;
			if (Prop->GetType() == EPropertyType::Distribution) continue;

			ImGui::PushID(GlobalID++);
			ImGui::TextUnformatted(Prop->Name.c_str());
			ImGui::NextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			if (RenderParticleProperty(*Prop, Object))
			{
				HandleEditedParticleProperty(Object, Prop);
				bChanged = true;
			}
			ImGui::NextColumn();
			ImGui::PopID();
		}

		ImGui::Columns(1);
	};

	for (const FString& Category : CategoryOrder)
	{
		if (bUseSections)
		{
			ImGui::PushStyleColor(ImGuiCol_Header,        SectionBg);
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, SectionHovered);
			ImGui::PushStyleColor(ImGuiCol_HeaderActive,  SectionActive);
			const bool bOpen = ImGui::CollapsingHeader(Category.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
			ImGui::PopStyleColor(3);

			if (bOpen)
			{
				ImGui::Indent(6.0f);
				RenderCategoryProps(Category);
				ImGui::Unindent(6.0f);
			}
		}
		else
		{
			RenderCategoryProps(Category);
		}
	}

	return bChanged;
}

bool FParticleSystemEditorWidget::RenderParticleProperty(const FProperty& Prop, void* Container)
{
	void* ValuePtr = Prop.ContainerPtrToValuePtr(Container);
	switch (Prop.GetType())
	{
	case EPropertyType::Bool:
		return ImGui::Checkbox("##Value", static_cast<bool*>(ValuePtr));
	case EPropertyType::ByteBool:
	{
		uint8* Value = static_cast<uint8*>(ValuePtr);
		bool bValue = (*Value != 0);
		if (ImGui::Checkbox("##Value", &bValue))
		{
			*Value = bValue ? 1 : 0;
			return true;
		}
		return false;
	}
	case EPropertyType::Int:
	{
		const FNumericProperty& NumericProp = static_cast<const FNumericProperty&>(Prop);
		int32* Value = static_cast<int32*>(ValuePtr);
		if (NumericProp.Min != 0.0f || NumericProp.Max != 0.0f)
		{
			return ImGui::DragInt("##Value", Value, NumericProp.Speed, static_cast<int32>(NumericProp.Min), static_cast<int32>(NumericProp.Max));
		}
		return ImGui::DragInt("##Value", Value, NumericProp.Speed);
	}
	case EPropertyType::Float:
	{
		const FNumericProperty& NumericProp = static_cast<const FNumericProperty&>(Prop);
		float* Value = static_cast<float*>(ValuePtr);
		if (NumericProp.Min != 0.0f || NumericProp.Max != 0.0f)
		{
			return ImGui::DragFloat("##Value", Value, NumericProp.Speed, NumericProp.Min, NumericProp.Max, "%.4f");
		}
		return ImGui::DragFloat("##Value", Value, NumericProp.Speed, 0.0f, 0.0f, "%.4f");
	}
	case EPropertyType::Vec3:
		return ImGui::DragFloat3("##Value", static_cast<float*>(ValuePtr), 0.1f);
	case EPropertyType::Vec4:
		return ImGui::DragFloat4("##Value", static_cast<float*>(ValuePtr), 0.1f);
	case EPropertyType::Color4:
	{
		FLinearColor* Value = static_cast<FLinearColor*>(ValuePtr);
		float Color[4] = { Value->R, Value->G, Value->B, Value->A };
		if (ImGui::DragFloat4("##Value", Color, 0.01f, 0.0f, 0.0f, "%.4f"))
		{
			*Value = FLinearColor(Color[0], Color[1], Color[2], Color[3]);
			return true;
		}
		return false;
	}
	case EPropertyType::SoftObject:
	{
		const FSoftObjectProperty& SoftObjectProp = static_cast<const FSoftObjectProperty&>(Prop);
		if (SoftObjectProp.PropertyClass != UStaticMesh::StaticClass())
		{
			ImGui::TextDisabled("Unsupported");
			return false;
		}

		auto* Value = static_cast<TSoftObjectPtr<UStaticMesh>*>(ValuePtr);
		const FString& CurrentPath = Value->GetPath().ToString();
		const FString Preview = (CurrentPath.empty() || CurrentPath == "None") ? FString("None") : CurrentPath;
		bool bChanged = false;

		if (ImGui::BeginCombo("##Value", Preview.c_str()))
		{
			const bool bSelectedNone = CurrentPath.empty() || CurrentPath == "None";
			if (ImGui::Selectable("None", bSelectedNone))
			{
				Value->Reset();
				bChanged = true;
			}
			if (bSelectedNone)
			{
				ImGui::SetItemDefaultFocus();
			}

			const TArray<FMeshAssetListItem>& MeshFiles = FMeshManager::GetAvailableStaticMeshFiles();
			for (const FMeshAssetListItem& Item : MeshFiles)
			{
				const bool bSelected = CurrentPath == Item.FullPath;
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
				{
					ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
					if (UStaticMesh* LoadedMesh = FMeshManager::LoadStaticMesh(Item.FullPath, Device))
					{
						*Value = LoadedMesh;
					}
					else
					{
						Value->SetPath(Item.FullPath);
					}
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		return bChanged;
	}
	case EPropertyType::Distribution:
		ImGui::TextDisabled("Edited below");
		return false;
	case EPropertyType::MaterialSlot:
	{
		FMaterialSlot* Slot = static_cast<FMaterialSlot*>(ValuePtr);
		FString Preview = (Slot->Path.empty() || Slot->Path == "None") ? "None" : Slot->Path;
		bool bChanged = false;

		if (ImGui::BeginCombo("##Mat", Preview.c_str()))
		{
			bool bSelectedNone = (Slot->Path == "None" || Slot->Path.empty());
			if (ImGui::Selectable("None", bSelectedNone))
			{
				Slot->Path = "None";
				bChanged = true;
			}
			if (bSelectedNone)
			{
				ImGui::SetItemDefaultFocus();
			}

			const TArray<FMaterialAssetListItem>& MatFiles = FMaterialManager::Get().GetAvailableParticleMaterialFiles();
			for (const FMaterialAssetListItem& Item : MatFiles)
			{
				bool bSelected = (Slot->Path == Item.FullPath);
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
				{
					Slot->Path = Item.FullPath;
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("MaterialContentItem"))
			{
				FContentItem ContentItem = *reinterpret_cast<const FContentItem*>(Payload->Data);
				const FString DroppedPath = FPaths::ToUtf8(ContentItem.Path.lexically_relative(FPaths::RootDir()).generic_wstring());
				if (IsParticleMaterialPath(DroppedPath))
				{
					Slot->Path = DroppedPath;
					bChanged = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		return bChanged;
	}
	case EPropertyType::Rotator:
	{
		FRotator* Rot = static_cast<FRotator*>(ValuePtr);
		float RotXYZ[3] = { Rot->Roll, Rot->Pitch, Rot->Yaw };
		if (ImGui::DragFloat3("##Value", RotXYZ, 0.1f))
		{
			Rot->Roll = RotXYZ[0];
			Rot->Pitch = RotXYZ[1];
			Rot->Yaw = RotXYZ[2];
			return true;
		}
		return false;
	}
	case EPropertyType::String:
	{
		FString* Value = static_cast<FString*>(ValuePtr);
		char Buffer[256];
		strncpy_s(Buffer, sizeof(Buffer), Value->c_str(), _TRUNCATE);
		if (ImGui::InputText("##Value", Buffer, sizeof(Buffer)))
		{
			*Value = Buffer;
			return true;
		}
		return false;
	}
	case EPropertyType::Name:
	{
		FName* Value = static_cast<FName*>(ValuePtr);
		const FString Current = Value->ToString();
		char Buffer[256];
		strncpy_s(Buffer, sizeof(Buffer), Current.c_str(), _TRUNCATE);
		if (ImGui::InputText("##Value", Buffer, sizeof(Buffer)))
		{
			*Value = FName(Buffer);
			return true;
		}
		return false;
	}
	case EPropertyType::Enum:
	{
		const FEnumProperty& EnumProp = static_cast<const FEnumProperty&>(Prop);
		UEnum* Enum = EnumProp.GetEnum();
		int32 CurrentValue = 0;
		if (Prop.ElementSize == sizeof(uint8))
		{
			CurrentValue = *static_cast<uint8*>(ValuePtr);
		}
		else
		{
			CurrentValue = *static_cast<int32*>(ValuePtr);
		}

		const int32 CurrentIndex = Enum ? Enum->GetIndexByValue(CurrentValue) : -1;
		const char* Preview = (Enum && CurrentIndex >= 0) ? Enum->GetNameByIndex(static_cast<uint32>(CurrentIndex)) : "Unknown";
		bool bChanged = false;
		if (ImGui::BeginCombo("##Value", Preview))
		{
			const uint32 Count = Enum ? Enum->NumEnums() : 0;
			for (uint32 EnumIndex = 0; EnumIndex < Count; ++EnumIndex)
			{
				const int64 EnumValue = Enum->GetValueByIndex(EnumIndex);
				const bool bSelected = static_cast<int64>(CurrentValue) == EnumValue;
				if (ImGui::Selectable(Enum->GetNameByIndex(EnumIndex), bSelected))
				{
					if (Prop.ElementSize == sizeof(uint8))
					{
						*static_cast<uint8*>(ValuePtr) = static_cast<uint8>(EnumValue);
					}
					else
					{
						*static_cast<int32*>(ValuePtr) = static_cast<int32>(EnumValue);
					}
					bChanged = true;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return bChanged;
	}
	case EPropertyType::Array:
	{
		const FArrayProperty& ArrayProp = static_cast<const FArrayProperty&>(Prop);
		if (!ArrayProp.Accessor || !ArrayProp.Inner)
		{
			ImGui::TextDisabled("Unsupported");
			return false;
		}

		FArrayAccessor* Accessor = ArrayProp.Accessor;
		const bool bFixedSize = (Prop.PropertyFlag & CPF_FixedSize) != 0;
		bool bChanged = false;
		int32 RemoveIndex = INDEX_NONE;

		ImGui::BeginGroup();
		ImGui::Text("%u element%s", Accessor->Num(ValuePtr), Accessor->Num(ValuePtr) == 1 ? "" : "s");
		if (!bFixedSize)
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("+"))
			{
				Accessor->AddDefault(ValuePtr);
				bChanged = true;
			}
		}

		const uint32 ElementCount = Accessor->Num(ValuePtr);
		for (uint32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
		{
			void* ElementPtr = Accessor->GetAt(ValuePtr, ElementIndex);
			if (!ElementPtr)
			{
				continue;
			}

			ImGui::PushID(static_cast<int32>(ElementIndex));
			ImGui::Separator();
			ImGui::Text("Element %u", ElementIndex);
			if (!bFixedSize)
			{
				ImGui::SameLine();
				if (ImGui::SmallButton("x"))
				{
					RemoveIndex = static_cast<int32>(ElementIndex);
				}
			}

			if (ArrayProp.Inner->GetType() == EPropertyType::Struct)
			{
				bChanged |= RenderParticleProperty(*ArrayProp.Inner, ElementPtr);
			}
			else
			{
				ImGui::SetNextItemWidth(-1.0f);
				bChanged |= RenderParticleProperty(*ArrayProp.Inner, ElementPtr);
			}
			ImGui::PopID();
		}

		if (RemoveIndex != INDEX_NONE)
		{
			Accessor->RemoveAt(ValuePtr, static_cast<uint32>(RemoveIndex));
			bChanged = true;
		}

		ImGui::EndGroup();
		return bChanged;
	}
	case EPropertyType::Struct:
	{
		const FStructProperty& StructProp = static_cast<const FStructProperty&>(Prop);
		bool bChanged = false;
		const TArray<FProperty*>& StructProps = StructProp.GetStructProperties();
		for (int32 ChildIndex = 0; ChildIndex < static_cast<int32>(StructProps.size()); ++ChildIndex)
		{
			FProperty* ChildProp = StructProps[ChildIndex];
			if (!ChildProp)
			{
				continue;
			}
			ImGui::PushID(ChildIndex);
			ImGui::TextUnformatted(ChildProp->Name.c_str());
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			bChanged |= RenderParticleProperty(*ChildProp, ValuePtr);
			ImGui::PopID();
		}
		return bChanged;
	}
	default:
		ImGui::TextDisabled("Unsupported");
		return false;
	}
}

void FParticleSystemEditorWidget::HandleEditedParticleProperty(UObject* Object, const FProperty* Prop)
{
	if (!Object)
	{
		return;
	}

	if (Prop)
	{
		Object->PostEditProperty(Prop->Name.c_str());
	}
#if defined(_DEBUG)
	LogEventGeneratorState("HandleEditedParticleProperty-AfterPostEdit", Object, Prop);
#endif
	if (UParticleModule* Module = Cast<UParticleModule>(Object))
	{
		Module->CacheModuleValues();
	}

	UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject);
	UParticleEmitter* Emitter = (SelectedEmitterIndex >= 0 && Asset) ? Asset->GetEmitter(SelectedEmitterIndex) : nullptr;
	UParticleLODLevel* LOD = GetEditedLODLevel(Emitter);
	if (LOD)
	{
		LOD->CacheModules();
	}
	if (Asset)
	{
		Asset->CacheSystemModuleInfo();
	}
#if defined(_DEBUG)
	LogEventGeneratorState("HandleEditedParticleProperty-AfterCache", Object, Prop);
#endif
	SyncEditedLODSelection();
	MarkDirty();
}

bool FParticleSystemEditorWidget::RenderEmittersPanel()
{
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 HeaderStart = ImGui::GetCursorScreenPos();
	const float HeaderWidth = ImGui::GetContentRegionAvail().x;
	DrawList->AddRectFilled(HeaderStart, ImVec2(HeaderStart.x + HeaderWidth, HeaderStart.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ParticlePanelHeaderColor));
	DrawList->AddRectFilled(HeaderStart, ImVec2(HeaderStart.x + ParticlePanelAccentWidth, HeaderStart.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ParticlePanelAccentColor));
	DrawList->AddText(ImVec2(HeaderStart.x + ParticlePanelTitleOffsetX, HeaderStart.y + ParticlePanelTitleOffsetY), ImGui::GetColorU32(ImGuiCol_Text), "Emitters");
	ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, ParticleCurveEditorHeaderSpacing));

	UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject);
	if (!Asset)
	{
		return false;
	}

	const TArray<UParticleEmitter*>& Emitters = Asset->GetEmitters();
	if (Emitters.empty())
	{
		return false;
	}

	bool bChanged = false;
	bool bClearSelection = false;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticleEditorZeroSpacing);
	if (ImGui::BeginChild("##ParticleEmitterStrip", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		for (int32 EmitterIndex = 0; EmitterIndex < static_cast<int32>(Emitters.size()); ++EmitterIndex)
		{
			UParticleEmitter* Emitter = Emitters[EmitterIndex];
			ImGui::PushID(EmitterIndex);

			bChanged |= RenderEmitterBlock(Emitter, EmitterIndex);

			if (EmitterIndex + 1 < static_cast<int32>(Emitters.size()))
			{
				ImGui::SameLine(0.0f, ParticleEmitterSpacing);
			}

			ImGui::PopID();
		}

		ImGui::SameLine(0.0f, 0.0f);
		if (ImGui::InvisibleButton("##EmitterStripBackground", ImGui::GetContentRegionAvail()))
		{
			bClearSelection = true;
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();

	if (bClearSelection)
	{
		SelectedEmitterIndex = -1;
		SelectedModule = nullptr;
	}

	return bChanged;
}

bool FParticleSystemEditorWidget::RenderEmitterBlock(UParticleEmitter* Emitter, int32 EmitterIndex)
{
	bool bChanged = false;
	bool bSelectEmitter = false;

	ImGui::BeginGroup();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ParticleEmitterBlockColor);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticleEmitterBlockPadding);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ParticleEditorZeroVerticalSpacing);
	ImGui::BeginChild("##EmitterBlock", ImVec2(ParticleEmitterWidth, 0.0f), true, ImGuiWindowFlags_NoScrollbar);
	bChanged |= RenderEmitterHeader(Emitter, EmitterIndex);
	bChanged |= RenderEmitterModules(Emitter, EmitterIndex);
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		!ImGui::IsAnyItemHovered())
	{
		bSelectEmitter = true;
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
	ImGui::EndGroup();

	if (bSelectEmitter)
	{
		SelectedEmitterIndex = EmitterIndex;
		SelectedModule = nullptr;
	}

	return bChanged;
}

bool FParticleSystemEditorWidget::RenderEmitterHeader(UParticleEmitter* Emitter, int32 EmitterIndex)
{
	const FString EmitterName = Emitter->GetEmitterName().ToString();
	const bool bEmitterSelected = SelectedEmitterIndex == EmitterIndex;
	UParticleLODLevel* LODLevel = GetEditedLODLevel(Emitter);
	UParticleModuleTypeDataBase* TypeData = LODLevel ? LODLevel->GetTypeDataModule() : nullptr;
	UParticleModuleRequired* RequiredModule = LODLevel ? LODLevel->GetRequiredModule() : nullptr;
	const EParticleEmitterType EmitterType = TypeData
		? TypeData->GetEmitterType()
		: (RequiredModule ? RequiredModule->GetEmitterType() : EParticleEmitterType::PET_Sprite);
	const char* TypeLabel = GetParticleEmitterTypeLabel(EmitterType);
	const ImU32 TypeColor = GetParticleEmitterTypeColor(EmitterType);
	const int32 ModuleCount = LODLevel ? static_cast<int32>(LODLevel->GetModules().size()) + 2 : 0;

	const ImVec2 HeaderPos = ImGui::GetCursorScreenPos();
	const float HeaderWidth = ImGui::GetContentRegionAvail().x;
	const ImVec2 HeaderSize(HeaderWidth, ParticleEmitterHeaderHeight);
	const ImVec2 HeaderMax(HeaderPos.x + HeaderSize.x, HeaderPos.y + HeaderSize.y);
	const ImRect HeaderRect(HeaderPos, HeaderMax);

	const ImU32 HeaderColor = bEmitterSelected
		? IM_COL32(255, 106, 12, 255)
		: IM_COL32(66, 70, 82, 255);
	const ImU32 HeaderBorderColor = bEmitterSelected
		? IM_COL32(255, 162, 64, 255)
		: IM_COL32(92, 96, 108, 255);

	if (ImGui::InvisibleButton("##EmitterHeader", HeaderSize))
	{
		SelectedEmitterIndex = EmitterIndex;
		SelectedModule = nullptr;
	}

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(HeaderRect.Min, HeaderRect.Max, HeaderColor, 0.0f);
	DrawList->AddRect(HeaderRect.Min, HeaderRect.Max, HeaderBorderColor, 0.0f, 0, bEmitterSelected ? 2.0f : 1.0f);
	DrawList->AddRectFilled(
		HeaderRect.Min,
		ImVec2(HeaderRect.Min.x + ParticlePanelAccentWidth, HeaderRect.Max.y),
		TypeColor);

	const ImVec2 PreviewMin(
		HeaderRect.Max.x - ParticleEmitterHeaderPreviewInset - ParticleEmitterHeaderPreviewSize,
		HeaderRect.Min.y + ParticleEmitterHeaderPreviewInset);
	const ImVec2 PreviewMax(
		HeaderRect.Max.x - ParticleEmitterHeaderPreviewInset,
		PreviewMin.y + ParticleEmitterHeaderPreviewSize);
	const ImRect PreviewRect(PreviewMin, PreviewMax);

	DrawList->AddRectFilled(PreviewRect.Min, PreviewRect.Max, IM_COL32(18, 18, 22, 255), 0.0f);
	DrawList->AddRect(PreviewRect.Min, PreviewRect.Max, IM_COL32(110, 110, 118, 255), 0.0f);

	if (ID3D11ShaderResourceView* PreviewSRV = GetParticleEmitterPreviewSRV(Emitter, EditedLODIndex))
	{
		DrawList->AddImage((ImTextureID)PreviewSRV, PreviewRect.Min, PreviewRect.Max);
	}
	else
	{
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				const bool bDark = ((Row + Col) % 2) == 0;
				const float CellW = (PreviewRect.Max.x - PreviewRect.Min.x) * 0.25f;
				const float CellH = (PreviewRect.Max.y - PreviewRect.Min.y) * 0.25f;
				const ImVec2 CellMin(PreviewRect.Min.x + Col * CellW, PreviewRect.Min.y + Row * CellH);
				const ImVec2 CellMax(CellMin.x + CellW, CellMin.y + CellH);
				DrawList->AddRectFilled(CellMin, CellMax, bDark ? IM_COL32(28, 28, 32, 255) : IM_COL32(40, 40, 46, 255));
			}
		}

		const ImVec2 TypeTextSize = ImGui::CalcTextSize(TypeLabel);
		DrawList->AddText(
			ImVec2(
				PreviewRect.Min.x + ((PreviewRect.Max.x - PreviewRect.Min.x) - TypeTextSize.x) * 0.5f,
				PreviewRect.Min.y + ((PreviewRect.Max.y - PreviewRect.Min.y) - TypeTextSize.y) * 0.5f),
			TypeColor,
			TypeLabel);
	}

	const ImVec2 TextMin(
		HeaderRect.Min.x + ParticleEmitterHeaderPaddingX + ParticlePanelAccentWidth + 2.0f,
		HeaderRect.Min.y + ParticleEmitterHeaderPaddingY);
	const float TextMaxX = PreviewRect.Min.x - 8.0f;
	DrawList->AddText(
		ImVec2(TextMin.x, TextMin.y + ParticleEmitterHeaderNameOffsetY),
		IM_COL32(255, 255, 255, 255),
		EmitterName.empty() ? "Particle Emitter" : EmitterName.c_str());

	const char* CountLabel = "%d";
	char CountText[16];
	snprintf(CountText, sizeof(CountText), CountLabel, ModuleCount);
	const ImVec2 BadgeMin(TextMin.x, HeaderRect.Max.y - ParticleEmitterHeaderPaddingY - ParticleEmitterHeaderBadgeHeight);
	const ImVec2 TypeBadgeMax((std::min)(TextMaxX, BadgeMin.x + 62.0f), BadgeMin.y + ParticleEmitterHeaderBadgeHeight);
	DrawList->AddRectFilled(BadgeMin, TypeBadgeMax, IM_COL32(28, 28, 32, 180), 3.0f);
	const ImVec2 TypeBadgeTextSize = ImGui::CalcTextSize(TypeLabel);
	DrawList->AddText(
		ImVec2(BadgeMin.x + 6.0f, BadgeMin.y + (ParticleEmitterHeaderBadgeHeight - TypeBadgeTextSize.y) * 0.5f),
		TypeColor,
		TypeLabel);

	const float CountBadgeWidth = 28.0f;
	const ImVec2 CountBadgeMin(TypeBadgeMax.x + 6.0f, BadgeMin.y);
	if (CountBadgeMin.x + CountBadgeWidth <= TextMaxX)
	{
		const ImVec2 CountBadgeMax(CountBadgeMin.x + CountBadgeWidth, BadgeMin.y + ParticleEmitterHeaderBadgeHeight);
		DrawList->AddRectFilled(CountBadgeMin, CountBadgeMax, IM_COL32(28, 28, 32, 180), 3.0f);
		const ImVec2 CountSize = ImGui::CalcTextSize(CountText);
		DrawList->AddText(
			ImVec2(
				CountBadgeMin.x + (CountBadgeWidth - CountSize.x) * 0.5f,
				CountBadgeMin.y + (ParticleEmitterHeaderBadgeHeight - CountSize.y) * 0.5f),
			IM_COL32(245, 245, 245, 255),
			CountText);
	}

	ImGui::Dummy(ImVec2(1.0f, ParticleEmitterHeaderBottomSpacing));

	return false;
}

bool FParticleSystemEditorWidget::RenderEmitterModules(UParticleEmitter* Emitter, int32 EmitterIndex)
{
	bool bChanged = false;

	UParticleLODLevel* LODLevel = GetEditedLODLevel(Emitter);
	if (!LODLevel)
	{
		return false;
	}

	if (UParticleModuleTypeDataBase* TypeDataModule = LODLevel->GetTypeDataModule())
	{
		bChanged |= RenderParticleModuleItem(TypeDataModule, EmitterIndex);
	}

	UParticleModuleRequired* RequiredModule = LODLevel->GetRequiredModule();
	bChanged |= RenderParticleModuleItem(RequiredModule, EmitterIndex);

	const TArray<UParticleModule*> Modules = LODLevel->GetModules();
	for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
	{
		UParticleModule* Module = Modules[ModuleIndex];
		bChanged |= RenderParticleModuleItem(Module, EmitterIndex);
	}

	return bChanged;
}

bool FParticleSystemEditorWidget::RenderParticleModuleItem(UParticleModule* Module, int32 EmitterIndex)
{
	if (!Module)
	{
		return false;
	}

	bool bChanged = false;

	const EParticleModuleClass ModuleClass = Module->GetModuleClass();
	const char* ModuleName = "Unknown";
	switch (ModuleClass)
	{
	case EParticleModuleClass::Required: ModuleName = "Required"; break;
	case EParticleModuleClass::TypeDataSprite: ModuleName = "Sprite Data"; break;
	case EParticleModuleClass::TypeDataMesh: ModuleName = "Mesh Data"; break;
	case EParticleModuleClass::TypeDataBeam: ModuleName = "Beam Data"; break;
	case EParticleModuleClass::TypeDataRibbon: ModuleName = "Ribbon Data"; break;
	case EParticleModuleClass::Spawn: ModuleName = "Spawn"; break;
	case EParticleModuleClass::Lifetime: ModuleName = "Lifetime"; break;
	case EParticleModuleClass::Location: ModuleName = "Location"; break;
	case EParticleModuleClass::Velocity: ModuleName = "Velocity"; break;
	case EParticleModuleClass::Color: ModuleName = "Color"; break;
	case EParticleModuleClass::Size: ModuleName = "Size"; break;
	case EParticleModuleClass::Rotation: ModuleName = "Rotation"; break;
	case EParticleModuleClass::RotationRate: ModuleName = "Rotation Rate"; break;
	case EParticleModuleClass::Acceleration: ModuleName = "Acceleration"; break;
	case EParticleModuleClass::Attractor: ModuleName = "Attractor"; break;
	case EParticleModuleClass::Orbit: ModuleName = "Orbit"; break;
	case EParticleModuleClass::Collision: ModuleName = "Collision"; break;
	case EParticleModuleClass::Kill: ModuleName = "Kill"; break;
	case EParticleModuleClass::EventGenerator: ModuleName = "Event Generator"; break;
	case EParticleModuleClass::EventReceiverSpawn: ModuleName = "Event Receiver Spawn"; break;
	case EParticleModuleClass::EventReceiverKillAll: ModuleName = "Event Receiver Kill All"; break;
	case EParticleModuleClass::SubImageIndex: ModuleName = "SubImage Index"; break;
	case EParticleModuleClass::SubUVMovie: ModuleName = "SubUV Movie"; break;
	case EParticleModuleClass::Light: ModuleName = "Light"; break;
	case EParticleModuleClass::VectorField: ModuleName = "Vector Field"; break;
	case EParticleModuleClass::Camera: ModuleName = "Camera"; break;
	case EParticleModuleClass::Parameter: ModuleName = "Parameter"; break;
	default: break;
	}

	const bool bCanToggleModule =
		(ModuleClass != EParticleModuleClass::Required) &&
		(ModuleClass != EParticleModuleClass::TypeDataSprite) &&
		(ModuleClass != EParticleModuleClass::TypeDataMesh) &&
		(ModuleClass != EParticleModuleClass::TypeDataBeam) &&
		(ModuleClass != EParticleModuleClass::TypeDataRibbon);
	UParticleLODLevel* CurrentLODLevel = nullptr;
	int32 ModuleIndexInStack = -1;
	if (UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject))
	{
		if (UParticleEmitter* Emitter = Asset->GetEmitter(EmitterIndex))
		{
			CurrentLODLevel = GetEditedLODLevel(Emitter);
			ModuleIndexInStack = FindParticleModuleIndex(CurrentLODLevel, Module);
		}
	}
	const bool bCanReorderModule = ModuleIndexInStack >= 0 && !IsParticleModuleFixedInStack(ModuleClass);
	const bool bModuleSelected = SelectedModule == Module;
	const FParticleModuleStyleColors& ModuleColors =
		(ModuleClass == EParticleModuleClass::Spawn) ? ParticleSpawnModuleColors :
		((ModuleClass == EParticleModuleClass::Required) ? ParticleRequiredModuleColors :
		(IsParticleEventModuleClass(ModuleClass)) ? ParticleEventModuleColors :
		(IsParticleSubUVModuleClass(ModuleClass)) ? ParticleSubUVModuleColors :
		(((ModuleClass == EParticleModuleClass::TypeDataSprite)
		|| (ModuleClass == EParticleModuleClass::TypeDataMesh)
		|| (ModuleClass == EParticleModuleClass::TypeDataBeam)
		|| (ModuleClass == EParticleModuleClass::TypeDataRibbon)) ? ParticleTypeDataModuleColors :
		ParticleNormalModuleColors));
	const ImVec4 ModuleColor = bModuleSelected ? ModuleColors.Selected : ModuleColors.Default;
	bool bSharedFromPreviousLOD = false;
	if (EditedLODIndex > 0)
	{
		if (UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject))
		{
			if (UParticleEmitter* Emitter = Asset->GetEmitter(EmitterIndex))
			{
				if (const UParticleLODLevel* PreviousLOD = Emitter->GetLODLevel(EditedLODIndex - 1))
				{
					bSharedFromPreviousLOD = AreParticleModulesEffectivelyShared(Module, FindModuleByClass(PreviousLOD, ModuleClass));
				}
			}
		}
	}
	ImVec2 RowStart;
	float RowWidth = 0.0f;
	ImGuiSelectableFlags SelectableFlags = ImGuiSelectableFlags_SpanAvailWidth;
	if (bCanToggleModule)
	{
		RowStart = ImGui::GetCursorScreenPos();
		RowWidth = ImGui::GetContentRegionAvail().x;
		SelectableFlags |= ImGuiSelectableFlags_AllowOverlap;
		ImGui::SetNextItemAllowOverlap();
	}
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ParticleModuleFramePadding);
	ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ParticleModuleTextAlign);
	ImGui::PushStyleColor(ImGuiCol_Header, ModuleColor);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ModuleColor);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ModuleColor);
	ImGui::PushID(Module);
	if (ImGui::Selectable("##ParticleModuleRow", true, SelectableFlags, ImVec2(0.0f, ParticleModuleHeight)))
	{
		SelectedEmitterIndex = EmitterIndex;
		SelectedModule = Module;
	}
	if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
	{
		SelectedEmitterIndex = EmitterIndex;
		SelectedModule = Module;
	}
	ImGui::PopID();
	const ImRect ModuleRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	const bool bCanRemoveModule = ModuleIndexInStack >= 0 && !IsParticleModuleFixedInStack(ModuleClass);
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(2);

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
	if (ImGui::BeginPopupContextItem())
	{
		ImGui::BeginDisabled(!bCanRemoveModule || SelectedModule != Module);
		if (ImGui::MenuItem("Delete Module"))
		{
			bChanged |= RemoveSelectedModule();
		}
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(2);

	if (bSharedFromPreviousLOD)
	{
		DrawSharedModuleStripeOverlay(ImGui::GetWindowDrawList(), ModuleRect);
	}

	DrawParticleModuleLabel(ImGui::GetWindowDrawList(), ModuleRect, ModuleName, bCanToggleModule);

	if (bCanReorderModule && ImGui::BeginDragDropSource())
	{
		const FParticleModuleDragDropPayload Payload = { EmitterIndex, Module };
		ImGui::SetDragDropPayload("ParticleModuleReorder", &Payload, sizeof(Payload));
		ImGui::TextUnformatted(ModuleName);
		ImGui::EndDragDropSource();
	}

	if (bCanReorderModule && ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("ParticleModuleReorder"))
		{
			if (Payload->DataSize == sizeof(FParticleModuleDragDropPayload))
			{
				const FParticleModuleDragDropPayload& DragPayload = *static_cast<const FParticleModuleDragDropPayload*>(Payload->Data);
				if (DragPayload.EmitterIndex == EmitterIndex && DragPayload.Module && DragPayload.Module != Module && CurrentLODLevel)
				{
					const int32 DraggedModuleIndex = FindParticleModuleIndex(CurrentLODLevel, DragPayload.Module);
					if (DraggedModuleIndex >= 0)
					{
						const bool bDropAfter = ImGui::GetMousePos().y >= (ModuleRect.Min.y + ModuleRect.Max.y) * 0.5f;
						const int32 TargetIndex = bDropAfter ? ModuleIndexInStack + 1 : ModuleIndexInStack;
						if (CurrentLODLevel->MoveModule(DraggedModuleIndex, TargetIndex))
						{
							CurrentLODLevel->CacheModules();
							if (UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject))
							{
								Asset->CacheSystemModuleInfo();
							}
							SelectedEmitterIndex = EmitterIndex;
							SelectedModule = DragPayload.Module;
							SyncEditedLODSelection();
							MarkDirty();
							bChanged = true;
						}
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	if (bCanToggleModule)
	{
		const ImVec2 RowEnd = ImVec2(RowStart.x, RowStart.y + ParticleModuleHeight);
		const float CheckboxSize = ImGui::GetFontSize() + ParticleModuleCheckboxFramePadding * 2.0f;
		ImGui::SetCursorScreenPos(ImVec2(
			RowStart.x + RowWidth - CheckboxSize - ParticleModuleCheckboxRightPadding,
			RowStart.y + (ParticleModuleHeight - CheckboxSize) * 0.5f));
		ImGui::PushID(Module);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ParticleModuleCheckboxFramePadding, ParticleModuleCheckboxFramePadding));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(92, 92, 92, 255));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(108, 108, 108, 255));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(122, 122, 122, 255));
		ImGui::PushStyleColor(ImGuiCol_CheckMark, IM_COL32(255, 255, 255, 255));
		bool bEnabled = Module->IsEnabled();
		if (ImGui::Checkbox("##ModuleEnabled", &bEnabled))
		{
			Module->SetEnabled(bEnabled);
			MarkDirty();
			bChanged = true;
		}
		ImGui::PopStyleColor(4);
		ImGui::PopStyleVar();
		ImGui::PopID();
		ImGui::SetCursorScreenPos(RowEnd);
	}

	ImGui::Dummy(ImVec2(1.0f, ParticleModuleItemSpacing));

	return bChanged;
}

bool FParticleSystemEditorWidget::RenderCurveEditorPanel()
{
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 HeaderStart = ImGui::GetCursorScreenPos();
	const float Width = ImGui::GetContentRegionAvail().x;
	DrawList->AddRectFilled(HeaderStart, ImVec2(HeaderStart.x + Width, HeaderStart.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ParticlePanelHeaderColor));
	DrawList->AddRectFilled(HeaderStart, ImVec2(HeaderStart.x + ParticlePanelAccentWidth, HeaderStart.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ParticlePanelAccentColor));
	DrawList->AddText(ImVec2(HeaderStart.x + ParticlePanelTitleOffsetX, HeaderStart.y + ParticlePanelTitleOffsetY), ImGui::GetColorU32(ImGuiCol_Text), "Curve Editor");
	ImGui::Dummy(ImVec2(Width, ParticleCurveEditorHeaderSpacing));

	bool bChanged = false;

	if (!SelectedModule)
	{
		ImGui::TextDisabled("No module selected");
		return false;
	}

	// ── 활성 커브 채널 목록 수집 ────────────────────────────────────────────
	struct FCurveChannel
	{
		FString              Name;
		FParticleFloatCurve* MinCurve;
		FParticleFloatCurve* MaxCurve; // null = ConstantCurve (Min만 사용)
	};
	constexpr int32 MaxChannels = 8;
	FCurveChannel   Channels[MaxChannels];
	int32           ChannelCount = 0;
	bool            bIsCurveType = false;

	auto TryAddFloat = [&](UDistributionFloat* D, const char* Label)
	{
		if (!D) return;
		const bool bCurve   = (D->Type == EDistributionType::ConstantCurve || D->Type == EDistributionType::UniformCurve);
		const bool bUniform = (D->Type == EDistributionType::UniformCurve);
		if (!bCurve) return;
		bIsCurveType = true;
		if (ChannelCount < MaxChannels)
			Channels[ChannelCount++] = { FString(Label), &D->MinCurve, bUniform ? &D->MaxCurve : nullptr };
	};
	auto TryAddVector = [&](UDistributionVector* D, const FString& XL, const FString& YL, const FString& ZL)
	{
		if (!D) return;
		const bool bCurve   = (D->Type == EDistributionType::ConstantCurve || D->Type == EDistributionType::UniformCurve);
		const bool bUniform = (D->Type == EDistributionType::UniformCurve);
		if (!bCurve) return;
		bIsCurveType = true;
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { XL, &D->MinCurve.X, bUniform ? &D->MaxCurve.X : nullptr };
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { YL, &D->MinCurve.Y, bUniform ? &D->MaxCurve.Y : nullptr };
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { ZL, &D->MinCurve.Z, bUniform ? &D->MaxCurve.Z : nullptr };
	};
	auto TryAddColor = [&](UDistributionLinearColor* D)
	{
		if (!D) return;
		const bool bCurve   = (D->Type == EDistributionType::ConstantCurve || D->Type == EDistributionType::UniformCurve);
		const bool bUniform = (D->Type == EDistributionType::UniformCurve);
		if (!bCurve) return;
		bIsCurveType = true;
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { FString("R"), &D->MinCurve.R, bUniform ? &D->MaxCurve.R : nullptr };
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { FString("G"), &D->MinCurve.G, bUniform ? &D->MaxCurve.G : nullptr };
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { FString("B"), &D->MinCurve.B, bUniform ? &D->MaxCurve.B : nullptr };
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { FString("A"), &D->MinCurve.A, bUniform ? &D->MaxCurve.A : nullptr };
	};

	TArray<const FProperty*> Props;
	SelectedModule->GetClass()->GetAllProperties(Props);
	for (const FProperty* Prop : Props)
	{
		if (!Prop || Prop->GetType() != EPropertyType::Distribution)
		{
			continue;
		}

		const FDistributionProperty& DistProp = static_cast<const FDistributionProperty&>(*Prop);
		UObject* DistObject = DistProp.GetDistributionPropertyValue(const_cast<void*>(DistProp.ContainerPtrToValuePtr(SelectedModule)));
		if (UDistributionFloat* FloatDist = Cast<UDistributionFloat>(DistObject))
		{
			TryAddFloat(FloatDist, Prop->Name.c_str());
		}
		else if (UDistributionVector* VectorDist = Cast<UDistributionVector>(DistObject))
		{
			const FString XLabel = Prop->Name + ".X";
			const FString YLabel = Prop->Name + ".Y";
			const FString ZLabel = Prop->Name + ".Z";
			TryAddVector(VectorDist, XLabel, YLabel, ZLabel);
		}
		else if (UDistributionLinearColor* ColorDist = Cast<UDistributionLinearColor>(DistObject))
		{
			TryAddColor(ColorDist);
		}
	}

	if (!bIsCurveType)
	{
		ImGui::TextDisabled("Distribution type is not curve-based.");
		ImGui::TextDisabled("Set Type to ConstantCurve or UniformCurve.");
		return false;
	}

	if (SelectedModule != CurvePrevModule)
	{
		CurvePrevModule  = SelectedModule;
		CurveSelectedKey = -1;
		CurveSelCurve    = 0;
		CurveChannelIdx  = 0;
		bCurveDragKey = false;
		CurveDraggingTangentHandle = ECurveTangentHandle::None;
		bCurvePan = false;
		PFitCurveViewToKeys(Channels[0].MinCurve, Channels[0].MaxCurve, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV);
	}

	CurveChannelIdx = (std::max)(0, (std::min)(CurveChannelIdx, ChannelCount - 1));
	FCurveChannel& Ch = Channels[CurveChannelIdx];
	if (CurveSelCurve == 1 && !Ch.MaxCurve)
	{
		CurveSelCurve = 0;
	}

	if (ImGui::Button("Fit##CurveFit"))
	{
		PFitCurveViewToKeys(Ch.MinCurve, Ch.MaxCurve, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV);
	}
	if (ChannelCount > 1)
	{
		const char* Names[MaxChannels];
		for (int32 i = 0; i < ChannelCount; ++i) Names[i] = Channels[i].Name.c_str();
		ImGui::SameLine();
		ImGui::TextUnformatted("Ch");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::Combo("##CurveCh", &CurveChannelIdx, Names, ChannelCount))
		{
			CurveSelectedKey = -1;
			CurveSelCurve = 0;
			CurveDraggingTangentHandle = ECurveTangentHandle::None;
			bCurveDragKey = false;
		}
	}

	constexpr float InspectorHeight = 108.0f;
	constexpr float AxisLeftPad = 54.0f;
	constexpr float AxisRightPad = 10.0f;
	constexpr float AxisTopPad = 10.0f;
	constexpr float AxisBottomPad = 28.0f;
	constexpr float KeyRadius = 6.0f;
	constexpr float TangentHandleHitRadius = 6.0f;
	constexpr float CurveTimeEpsilon = 0.001f;
	constexpr int32 TickDivisions = 5;

	const float AvailH = ImGui::GetContentRegionAvail().y;
	const float CanvasH = (std::max)(220.0f, AvailH - InspectorHeight);
	const float CanvasW = ImGui::GetContentRegionAvail().x - 2.0f;

	const ImVec2 CanvasMin = ImGui::GetCursorScreenPos();
	const ImVec2 CanvasMax = ImVec2(CanvasMin.x + CanvasW, CanvasMin.y + CanvasH);
	const ImVec2 GraphMin = ImVec2(CanvasMin.x + AxisLeftPad, CanvasMin.y + AxisTopPad);
	const ImVec2 GraphMax = ImVec2(CanvasMax.x - AxisRightPad, CanvasMax.y - AxisBottomPad);
	const float GraphW = (std::max)(1.0f, GraphMax.x - GraphMin.x);
	const float GraphH = (std::max)(1.0f, GraphMax.y - GraphMin.y);

	ImGui::InvisibleButton("##PCurveCanvas", ImVec2(CanvasW, CanvasH));
	const bool bHovered = ImGui::IsItemHovered();
	const ImGuiIO& IO = ImGui::GetIO();
	const bool bGraphHovered =
		bHovered &&
		IO.MousePos.x >= GraphMin.x && IO.MousePos.x <= GraphMax.x &&
		IO.MousePos.y >= GraphMin.y && IO.MousePos.y <= GraphMax.y;
	ImDrawList*    DL       = ImGui::GetWindowDrawList();

	DL->AddRectFilled(CanvasMin, CanvasMax, IM_COL32(22, 22, 26, 255), 3.0f);
	DL->AddRect(CanvasMin, CanvasMax, IM_COL32(70, 70, 80, 255), 3.0f);
	DL->AddRectFilled(GraphMin, GraphMax, IM_COL32(24, 24, 30, 255), 3.0f);
	DL->AddRect(GraphMin, GraphMax, IM_COL32(86, 86, 96, 255), 3.0f);

	for (int32 TickIndex = 0; TickIndex <= TickDivisions; ++TickIndex)
	{
		const float Alpha = static_cast<float>(TickIndex) / static_cast<float>(TickDivisions);
		const float GridX = GraphMin.x + Alpha * GraphW;
		const float GridY = GraphMin.y + Alpha * GraphH;
		const ImU32 GridColor = (TickIndex == TickDivisions / 2) ? IM_COL32(76, 76, 88, 255) : IM_COL32(48, 48, 58, 255);

		DL->AddLine(ImVec2(GridX, GraphMin.y), ImVec2(GridX, GraphMax.y), GridColor);
		DL->AddLine(ImVec2(GraphMin.x, GridY), ImVec2(GraphMax.x, GridY), GridColor);

		char TimeLabel[32];
		const float TickTime = CurveViewMinT + (CurveViewMaxT - CurveViewMinT) * Alpha;
		std::snprintf(TimeLabel, sizeof(TimeLabel), "%.2f", TickTime);
		const ImVec2 TimeTextSize = ImGui::CalcTextSize(TimeLabel);
		DL->AddText(
			ImVec2(GridX - TimeTextSize.x * 0.5f, GraphMax.y + 6.0f),
			IM_COL32(185, 185, 195, 255),
			TimeLabel);

		char ValueLabel[32];
		const float TickValue = CurveViewMaxV - (CurveViewMaxV - CurveViewMinV) * Alpha;
		std::snprintf(ValueLabel, sizeof(ValueLabel), "%.2f", TickValue);
		const ImVec2 ValueTextSize = ImGui::CalcTextSize(ValueLabel);
		DL->AddText(
			ImVec2(GraphMin.x - ValueTextSize.x - 8.0f, GridY - ValueTextSize.y * 0.5f),
			IM_COL32(185, 185, 195, 255),
			ValueLabel);
	}

	if (CurveViewMinT <= 0.0f && CurveViewMaxT >= 0.0f) {
		const ImVec2 P = PCurveToScreen(0.0f, CurveViewMinV, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax);
		DL->AddLine(ImVec2(P.x, GraphMin.y), ImVec2(P.x, GraphMax.y), IM_COL32(100,100,120,255), 1.5f);
	}
	if (CurveViewMinV <= 0.0f && CurveViewMaxV >= 0.0f) {
		const ImVec2 P = PCurveToScreen(CurveViewMinT, 0.0f, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax);
		DL->AddLine(ImVec2(GraphMin.x, P.y), ImVec2(GraphMax.x, P.y), IM_COL32(100,100,120,255), 1.5f);
	}

	auto DrawCurveLine = [&](FParticleFloatCurve* C, ImU32 Color)
	{
		if (!C || C->Keys.empty()) return;
		constexpr int32 Samples = 128;
		ImVec2 Prev = PCurveToScreen(CurveViewMinT, C->Eval(CurveViewMinT), CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax);
		for (int32 s = 1; s < Samples; ++s)
		{
			const float FT  = CurveViewMinT + (CurveViewMaxT - CurveViewMinT) * (static_cast<float>(s) / (Samples - 1));
			const ImVec2 Cur = PCurveToScreen(FT, C->Eval(FT), CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax);
			if (PCurvePointFinite(Prev) && PCurvePointFinite(Cur))
				DL->AddLine(Prev, Cur, Color, 2.0f);
			Prev = Cur;
		}
	};
	DrawCurveLine(Ch.MinCurve, IM_COL32(80,220,120,255));
	DrawCurveLine(Ch.MaxCurve, IM_COL32(80,200,255,255));

	FParticleFloatCurve* SelectedCurve = (CurveSelCurve == 0) ? Ch.MinCurve : Ch.MaxCurve;
	if (SelectedCurve && CurveSelectedKey >= static_cast<int32>(SelectedCurve->Keys.size()))
	{
		CurveSelectedKey = -1;
	}

	ECurveTangentHandle HoveredTangentHandle = ECurveTangentHandle::None;
	if (SelectedCurve && CurveSelectedKey >= 0 && CurveSelectedKey < static_cast<int32>(SelectedCurve->Keys.size()))
	{
		const FFloatCurveKey& Key = SelectedCurve->Keys[CurveSelectedKey];
		const ImVec2 KeyPos = PCurveToScreen(Key.Time, Key.Value, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax);
		const bool bCanShowArriveHandle =
			CurveSelectedKey > 0 &&
			SelectedCurve->Keys[CurveSelectedKey - 1].InterpMode == EParticleKeyInterpMode::Cubic;
		const bool bCanShowLeaveHandle =
			CurveSelectedKey + 1 < static_cast<int32>(SelectedCurve->Keys.size()) &&
			Key.InterpMode == EParticleKeyInterpMode::Cubic;

		if (bCanShowArriveHandle)
		{
			const ImVec2 HandlePos = PGetCurveTangentHandlePosition(Key, true, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax);
			if (PCurvePointFinite(HandlePos))
			{
				if (PCurvePointNear(IO.MousePos, HandlePos, TangentHandleHitRadius))
				{
					HoveredTangentHandle = ECurveTangentHandle::Arrive;
				}
				DL->AddLine(KeyPos, HandlePos, IM_COL32(95, 150, 255, 180), 1.5f);
				DL->AddCircleFilled(HandlePos, 4.5f, IM_COL32(95, 150, 255, 255));
				DL->AddCircle(HandlePos, 4.5f, IM_COL32(15, 20, 30, 220), 12, 1.0f);
			}
		}

		if (bCanShowLeaveHandle)
		{
			const ImVec2 HandlePos = PGetCurveTangentHandlePosition(Key, false, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax);
			if (PCurvePointFinite(HandlePos))
			{
				if (PCurvePointNear(IO.MousePos, HandlePos, TangentHandleHitRadius))
				{
					HoveredTangentHandle = ECurveTangentHandle::Leave;
				}
				DL->AddLine(KeyPos, HandlePos, IM_COL32(95, 150, 255, 180), 1.5f);
				DL->AddCircleFilled(HandlePos, 4.5f, IM_COL32(95, 150, 255, 255));
				DL->AddCircle(HandlePos, 4.5f, IM_COL32(15, 20, 30, 220), 12, 1.0f);
			}
		}
	}

	int32 HoveredKey = -1;
	int32 HoveredCurve = -1;
	auto HitTestAndDrawKeys = [&](FParticleFloatCurve* Curve, int32 CurveIdx, ImU32 BaseColor)
	{
		if (!Curve)
		{
			return;
		}

		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve->Keys.size()); ++KeyIndex)
		{
			const FFloatCurveKey& Key = Curve->Keys[KeyIndex];
			const ImVec2 KeyPos = PCurveToScreen(Key.Time, Key.Value, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax);
			if (!PCurvePointFinite(KeyPos))
			{
				continue;
			}

			if (PCurvePointNear(IO.MousePos, KeyPos, KeyRadius))
			{
				HoveredKey = KeyIndex;
				HoveredCurve = CurveIdx;
			}

			const bool bSelected = CurveSelectedKey == KeyIndex && CurveSelCurve == CurveIdx;
			const bool bIsHovered = HoveredKey == KeyIndex && HoveredCurve == CurveIdx;
			const ImU32 KeyColor =
				bSelected ? IM_COL32(255, 245, 110, 255) :
				bIsHovered ? IM_COL32(255, 205, 90, 255) :
				BaseColor;
			DL->AddCircleFilled(KeyPos, 5.0f, KeyColor);
			DL->AddCircle(KeyPos, 5.0f, IM_COL32(20, 20, 22, 220), 12, 1.0f);
		}
	};
	HitTestAndDrawKeys(Ch.MinCurve, 0, IM_COL32(60, 200, 100, 255));
	HitTestAndDrawKeys(Ch.MaxCurve, 1, IM_COL32(60, 180, 240, 255));

	if (bGraphHovered && IO.MouseWheel != 0.0f)
	{
		float MouseTime = 0.0f;
		float MouseValue = 0.0f;
		PScreenToCurve(IO.MousePos, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax, MouseTime, MouseValue);
		const float Zoom = (IO.MouseWheel > 0.0f) ? 0.85f : 1.1764706f;
		const bool bZoomTime = !IO.KeyCtrl;
		const bool bZoomValue = !IO.KeyShift;
		if (bZoomTime)
		{
			CurveViewMinT = MouseTime + (CurveViewMinT - MouseTime) * Zoom;
			CurveViewMaxT = MouseTime + (CurveViewMaxT - MouseTime) * Zoom;
			if (CurveViewMaxT <= CurveViewMinT + CurveTimeEpsilon)
			{
				CurveViewMaxT = CurveViewMinT + CurveTimeEpsilon;
			}
		}
		if (bZoomValue)
		{
			CurveViewMinV = MouseValue + (CurveViewMinV - MouseValue) * Zoom;
			CurveViewMaxV = MouseValue + (CurveViewMaxV - MouseValue) * Zoom;
			if (CurveViewMaxV <= CurveViewMinV + CurveTimeEpsilon)
			{
				CurveViewMaxV = CurveViewMinV + CurveTimeEpsilon;
			}
		}
	}

	if (bGraphHovered && HoveredKey == -1 && HoveredTangentHandle == ECurveTangentHandle::None && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		bCurvePan = true;
		bCurveCtxSuppress = false;
	}
	if (bCurvePan && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 3.0f))
	{
		const float TimeSpan = CurveViewMaxT - CurveViewMinT;
		const float ValueSpan = CurveViewMaxV - CurveViewMinV;
		CurveViewMinT -= (IO.MouseDelta.x / GraphW) * TimeSpan;
		CurveViewMaxT -= (IO.MouseDelta.x / GraphW) * TimeSpan;
		CurveViewMinV += (IO.MouseDelta.y / GraphH) * ValueSpan;
		CurveViewMaxV += (IO.MouseDelta.y / GraphH) * ValueSpan;
		bCurveCtxSuppress = true;
	}
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
	{
		bCurvePan = false;
	}

	if (HoveredTangentHandle != ECurveTangentHandle::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		CurveDraggingTangentHandle = HoveredTangentHandle;
		bCurveDragKey = false;
	}
	else if (HoveredKey != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		CurveSelectedKey = HoveredKey;
		CurveSelCurve = HoveredCurve;
		CurveDraggingTangentHandle = ECurveTangentHandle::None;
		bCurveDragKey = true;
	}
	else if (bGraphHovered && HoveredKey == -1 && HoveredTangentHandle == ECurveTangentHandle::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		CurveSelectedKey = -1;
		CurveDraggingTangentHandle = ECurveTangentHandle::None;
	}

	SelectedCurve = (CurveSelCurve == 0) ? Ch.MinCurve : Ch.MaxCurve;
	if (CurveDraggingTangentHandle != ECurveTangentHandle::None && SelectedCurve && CurveSelectedKey >= 0 && CurveSelectedKey < static_cast<int32>(SelectedCurve->Keys.size()) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		FFloatCurveKey& Key = SelectedCurve->Keys[CurveSelectedKey];
		float MouseTime = 0.0f;
		float MouseValue = 0.0f;
		PScreenToCurve(IO.MousePos, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax, MouseTime, MouseValue);

		float NewTangent = 0.0f;
		if (CurveDraggingTangentHandle == ECurveTangentHandle::Arrive)
		{
			const float DeltaTime = Key.Time - MouseTime;
			NewTangent = std::fabs(DeltaTime) > CurveTimeEpsilon ? (Key.Value - MouseValue) / DeltaTime : Key.ArriveTangent;
		}
		else
		{
			const float DeltaTime = MouseTime - Key.Time;
			NewTangent = std::fabs(DeltaTime) > CurveTimeEpsilon ? (MouseValue - Key.Value) / DeltaTime : Key.LeaveTangent;
		}

		if (Key.TangentMode == EParticleCurveTangentMode::Auto)
		{
			Key.TangentMode = EParticleCurveTangentMode::User;
		}

		if (Key.TangentMode == EParticleCurveTangentMode::Break)
		{
			if (CurveDraggingTangentHandle == ECurveTangentHandle::Arrive)
			{
				Key.ArriveTangent = NewTangent;
			}
			else
			{
				Key.LeaveTangent = NewTangent;
			}
		}
		else
		{
			Key.ArriveTangent = NewTangent;
			Key.LeaveTangent = NewTangent;
		}

		SelectedCurve->AutoSetTangents();
		bChanged = true;
	}

	if (bCurveDragKey && SelectedCurve && CurveSelectedKey >= 0 && CurveSelectedKey < static_cast<int32>(SelectedCurve->Keys.size()) && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		FFloatCurveKey& Key = SelectedCurve->Keys[CurveSelectedKey];
		const int32 KeyCount = static_cast<int32>(SelectedCurve->Keys.size());
		Key.Time += (IO.MouseDelta.x / GraphW) * (CurveViewMaxT - CurveViewMinT);
		Key.Value -= (IO.MouseDelta.y / GraphH) * (CurveViewMaxV - CurveViewMinV);
		if (CurveSelectedKey > 0)
		{
			Key.Time = (std::max)(Key.Time, SelectedCurve->Keys[CurveSelectedKey - 1].Time + CurveTimeEpsilon);
		}
		if (CurveSelectedKey + 1 < KeyCount)
		{
			Key.Time = (std::min)(Key.Time, SelectedCurve->Keys[CurveSelectedKey + 1].Time - CurveTimeEpsilon);
		}
		SelectedCurve->AutoSetTangents();
		bChanged = true;
	}
	if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
	{
		bCurveDragKey = false;
		CurveDraggingTangentHandle = ECurveTangentHandle::None;
	}

	if (HoveredKey != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		CurveSelectedKey = HoveredKey;
		CurveSelCurve = HoveredCurve;
		ImGui::OpenPopup("##PCurveKeyCtx");
	}
	else if (bGraphHovered && HoveredKey == -1 && HoveredTangentHandle == ECurveTangentHandle::None && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !bCurveCtxSuppress)
	{
		PScreenToCurve(IO.MousePos, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV, GraphMin, GraphMax, CurveCtxTime, CurveCtxValue);
		ImGui::OpenPopup("##PCurveEmptyCtx");
	}

	if (ImGui::BeginPopup("##PCurveKeyCtx"))
	{
		FParticleFloatCurve* ContextCurve = (CurveSelCurve == 0) ? Ch.MinCurve : Ch.MaxCurve;
		if (ContextCurve && CurveSelectedKey >= 0 && CurveSelectedKey < static_cast<int32>(ContextCurve->Keys.size()))
		{
			FFloatCurveKey& Key = ContextCurve->Keys[CurveSelectedKey];
			if (ImGui::MenuItem("Delete Key"))
			{
				ContextCurve->Keys.erase(ContextCurve->Keys.begin() + CurveSelectedKey);
				ContextCurve->AutoSetTangents();
				CurveSelectedKey = -1;
				bChanged = true;
			}
			else
			{
				ImGui::Separator();
				if (ImGui::MenuItem("Constant", nullptr, Key.InterpMode == EParticleKeyInterpMode::Constant))
				{
					Key.InterpMode = EParticleKeyInterpMode::Constant;
					ContextCurve->AutoSetTangents();
					bChanged = true;
				}
				if (ImGui::MenuItem("Linear", nullptr, Key.InterpMode == EParticleKeyInterpMode::Linear))
				{
					Key.InterpMode = EParticleKeyInterpMode::Linear;
					ContextCurve->AutoSetTangents();
					bChanged = true;
				}
				if (ImGui::MenuItem("Cubic", nullptr, Key.InterpMode == EParticleKeyInterpMode::Cubic))
				{
					Key.InterpMode = EParticleKeyInterpMode::Cubic;
					Key.TangentMode = EParticleCurveTangentMode::Auto;
					ContextCurve->AutoSetTangents();
					bChanged = true;
				}

				if (Key.InterpMode == EParticleKeyInterpMode::Cubic)
				{
					ImGui::Separator();
					if (ImGui::MenuItem("Tangent Auto", nullptr, Key.TangentMode == EParticleCurveTangentMode::Auto))
					{
						Key.TangentMode = EParticleCurveTangentMode::Auto;
						ContextCurve->AutoSetTangents();
						bChanged = true;
					}
					if (ImGui::MenuItem("Tangent User", nullptr, Key.TangentMode == EParticleCurveTangentMode::User))
					{
						Key.TangentMode = EParticleCurveTangentMode::User;
						Key.LeaveTangent = Key.ArriveTangent;
						ContextCurve->AutoSetTangents();
						bChanged = true;
					}
					if (ImGui::MenuItem("Tangent Break", nullptr, Key.TangentMode == EParticleCurveTangentMode::Break))
					{
						Key.TangentMode = EParticleCurveTangentMode::Break;
						bChanged = true;
					}
				}
			}
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("##PCurveEmptyCtx"))
	{
		auto AddAndSelectKey = [&](FParticleFloatCurve* Curve, int32 CurveIdx)
		{
			if (!Curve)
			{
				return;
			}

			Curve->AddKey(CurveCtxTime, CurveCtxValue);
			int32 NewKeyIndex = -1;
			float BestDistance = FLT_MAX;
			for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve->Keys.size()); ++KeyIndex)
			{
				const float Distance = std::fabs(Curve->Keys[KeyIndex].Time - CurveCtxTime);
				if (Distance < BestDistance)
				{
					BestDistance = Distance;
					NewKeyIndex = KeyIndex;
				}
			}
			CurveSelectedKey = NewKeyIndex;
			CurveSelCurve = CurveIdx;
			bChanged = true;
		};

		if (Ch.MinCurve && ImGui::MenuItem("Add Key (Min)"))
		{
			AddAndSelectKey(Ch.MinCurve, 0);
		}
		if (Ch.MaxCurve && ImGui::MenuItem("Add Key (Max)"))
		{
			AddAndSelectKey(Ch.MaxCurve, 1);
		}
		if (ImGui::MenuItem("Fit to Keys"))
		{
			PFitCurveViewToKeys(Ch.MinCurve, Ch.MaxCurve, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV);
		}
		ImGui::EndPopup();
	}

	if (HoveredTangentHandle != ECurveTangentHandle::None && SelectedCurve && CurveSelectedKey >= 0 && CurveSelectedKey < static_cast<int32>(SelectedCurve->Keys.size()))
	{
		const FFloatCurveKey& Key = SelectedCurve->Keys[CurveSelectedKey];
		const float Tangent = (HoveredTangentHandle == ECurveTangentHandle::Arrive) ? Key.ArriveTangent : Key.LeaveTangent;
		ImGui::SetTooltip("%s Tangent %.3f", HoveredTangentHandle == ECurveTangentHandle::Arrive ? "Arrive" : "Leave", Tangent);
	}
	else if (HoveredKey != -1)
	{
		FParticleFloatCurve* HoverCurve = (HoveredCurve == 0) ? Ch.MinCurve : Ch.MaxCurve;
		if (HoverCurve && HoveredKey < static_cast<int32>(HoverCurve->Keys.size()))
		{
			const FFloatCurveKey& Key = HoverCurve->Keys[HoveredKey];
			const char* InterpLabel =
				Key.InterpMode == EParticleKeyInterpMode::Constant ? "Constant" :
				Key.InterpMode == EParticleKeyInterpMode::Linear ? "Linear" : "Cubic";
			ImGui::SetTooltip("T %.3f  V %.3f  [%s]  %s", Key.Time, Key.Value, HoveredCurve == 0 ? "Min" : "Max", InterpLabel);
		}
	}

	ImGui::TextColored(ImVec4(0.31f,0.86f,0.47f,1.0f), "■");
	ImGui::SameLine(0, 4); ImGui::TextDisabled("Min");
	if (Ch.MaxCurve) { ImGui::SameLine(0, 12); ImGui::TextColored(ImVec4(0.31f,0.78f,1.0f,1.0f), "■"); ImGui::SameLine(0,4); ImGui::TextDisabled("Max"); }
	ImGui::SameLine(0, 16);
	ImGui::TextDisabled("Wheel:zoom  Ctrl+Wheel:time  Shift+Wheel:value  RMB drag:pan  RMB:add/del key");

	SelectedCurve = (CurveSelCurve == 0) ? Ch.MinCurve : Ch.MaxCurve;
	if (SelectedCurve && CurveSelectedKey >= static_cast<int32>(SelectedCurve->Keys.size()))
	{
		CurveSelectedKey = -1;
	}

	ImGui::Separator();
	if (SelectedCurve && CurveSelectedKey >= 0 && CurveSelectedKey < static_cast<int32>(SelectedCurve->Keys.size()))
	{
		FFloatCurveKey& Key = SelectedCurve->Keys[CurveSelectedKey];
		char SelectedKeyLabel[64];
		std::snprintf(SelectedKeyLabel, sizeof(SelectedKeyLabel), "Selected Key: %s [%d]", CurveSelCurve == 0 ? "Min" : "Max", CurveSelectedKey);

		ImGui::Dummy(ImVec2(0.0f, 6.0f));
		{
			const ImVec2 HeaderPos = ImGui::GetCursorScreenPos();
			ImDrawList* HeaderDrawList = ImGui::GetWindowDrawList();
			const ImU32 HeaderColor = ImGui::GetColorU32(ImGuiCol_Text);
			HeaderDrawList->AddText(HeaderPos, HeaderColor, SelectedKeyLabel);
			HeaderDrawList->AddText(ImVec2(HeaderPos.x + 1.0f, HeaderPos.y), HeaderColor, SelectedKeyLabel);
			ImGui::Dummy(ImVec2(ImGui::CalcTextSize(SelectedKeyLabel).x + 1.0f, ImGui::GetTextLineHeight()));
		}
		ImGui::Dummy(ImVec2(0.0f, 6.0f));

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 3.0f));
		const float LabelWidth = 104.0f;
		const float ControlWidth = (std::clamp)(ImGui::GetContentRegionAvail().x * 0.32f, 150.0f, 220.0f);
		auto BeginInlineField = [&](const char* Label)
		{
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(Label);
			ImGui::SameLine(LabelWidth);
			ImGui::SetNextItemWidth(ControlWidth);
		};

		float KeyTime = Key.Time;
		BeginInlineField("Time");
		if (ImGui::DragFloat("##ParticleCurveKeyTime", &KeyTime, 0.01f))
		{
			if (CurveSelectedKey > 0)
			{
				KeyTime = (std::max)(KeyTime, SelectedCurve->Keys[CurveSelectedKey - 1].Time + CurveTimeEpsilon);
			}
			if (CurveSelectedKey + 1 < static_cast<int32>(SelectedCurve->Keys.size()))
			{
				KeyTime = (std::min)(KeyTime, SelectedCurve->Keys[CurveSelectedKey + 1].Time - CurveTimeEpsilon);
			}
			Key.Time = KeyTime;
			SelectedCurve->AutoSetTangents();
			bChanged = true;
		}

		BeginInlineField("Value");
		if (ImGui::DragFloat("##ParticleCurveKeyValue", &Key.Value, 0.01f))
		{
			SelectedCurve->AutoSetTangents();
			bChanged = true;
		}

		const char* InterpLabels[] = { "Constant", "Linear", "Cubic" };
		int32 InterpMode = static_cast<int32>(Key.InterpMode);
		BeginInlineField("Interpolation");
		if (ImGui::Combo("##ParticleCurveKeyInterp", &InterpMode, InterpLabels, IM_ARRAYSIZE(InterpLabels)))
		{
			Key.InterpMode = static_cast<EParticleKeyInterpMode>(InterpMode);
			if (Key.InterpMode == EParticleKeyInterpMode::Cubic)
			{
				Key.TangentMode = EParticleCurveTangentMode::Auto;
			}
			SelectedCurve->AutoSetTangents();
			bChanged = true;
		}

		if (Key.InterpMode == EParticleKeyInterpMode::Cubic)
		{
			const char* TangentLabels[] = { "Auto", "User", "Break" };
			int32 TangentMode = static_cast<int32>(Key.TangentMode);
			BeginInlineField("Tangent Mode");
			if (ImGui::Combo("##ParticleCurveKeyTangent", &TangentMode, TangentLabels, IM_ARRAYSIZE(TangentLabels)))
			{
				Key.TangentMode = static_cast<EParticleCurveTangentMode>(TangentMode);
				if (Key.TangentMode == EParticleCurveTangentMode::User)
				{
					Key.LeaveTangent = Key.ArriveTangent;
				}
				SelectedCurve->AutoSetTangents();
				bChanged = true;
			}

			if (Key.TangentMode != EParticleCurveTangentMode::Auto)
			{
				float Arrive = Key.ArriveTangent;
				BeginInlineField("Arrive Tangent");
				if (ImGui::DragFloat("##ParticleCurveKeyArrive", &Arrive, 0.01f))
				{
					Key.ArriveTangent = Arrive;
					if (Key.TangentMode == EParticleCurveTangentMode::User)
					{
						Key.LeaveTangent = Arrive;
					}
					SelectedCurve->AutoSetTangents();
					bChanged = true;
				}

				if (Key.TangentMode == EParticleCurveTangentMode::Break)
				{
					BeginInlineField("Leave Tangent");
					if (ImGui::DragFloat("##ParticleCurveKeyLeave", &Key.LeaveTangent, 0.01f))
					{
						SelectedCurve->AutoSetTangents();
						bChanged = true;
					}
				}
			}
		}

		ImGui::PopStyleVar(2);
		ImGui::Dummy(ImVec2(0.0f, 4.0f));
	}
	else
	{
		ImGui::TextDisabled("Select a key to edit interpolation and tangents.");
	}

	if (bChanged)
		HandleEditedParticleProperty(SelectedModule, nullptr);

	return bChanged;
}
