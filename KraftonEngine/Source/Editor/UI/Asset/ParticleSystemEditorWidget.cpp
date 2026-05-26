#include "ParticleSystemEditorWidget.h"

#include "Engine/Particles/Assets/ParticleAsset.h"
#include "Engine/Particles/Assets/ParticleSystemAssetManager.h"
#include "Engine/Particles/Modules/ParticleCoreModules.h"
#include "Engine/Particles/Modules/ParticleMotionModules.h"
#include "Engine/Particles/Modules/ParticleRenderExpressionModules.h"
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
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Object/FUObjectArray.h"
#include "Platform/Paths.h"
#include "Texture/Texture2D.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

// 바닥 시각화
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "Mesh/MeshManager.h"
#include "GameFramework/StaticMeshActor.h"
#include "Engine/Component/StaticMeshComponent.h"

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

static ID3D11ShaderResourceView* GetParticleEmitterPreviewSRV(UParticleEmitter* Emitter)
{
	if (!Emitter)
	{
		return nullptr;
	}

	UParticleLODLevel* LODLevel = Emitter->GetLODLevel(0);
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
	{ EParticleModuleClass::SubUV, "SubUV" },
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
constexpr float ParticleModuleHeight = 28.0f;
constexpr ImVec2 ParticleModuleFramePadding = ImVec2(10.0f, 4.0f);
constexpr ImVec2 ParticleModuleTextAlign = ImVec2(0.06f, 0.5f);
constexpr float ParticleModuleCheckboxFramePadding = 1.0f;
constexpr float ParticleModuleCheckboxRightPadding = 6.0f;

const FVector ParticlePreviewFloorLocation = FVector(0.0f, 0.0f, -1.0f);
const FVector ParticlePreviewFloorScale = FVector(10.0f, 10.0f, 0.02f);

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

	if (ImGui::Button("Save"))
	{
		if (FParticleSystemAssetManager::Get().Save(Asset))
		{
			ClearDirty();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Replay"))
	{
		ReplayPreviewParticleSystem();
	}
	ImGui::SameLine();
	if (ImGui::Button("Add Emitter"))
	{
		const int32 NewEmitterIndex = static_cast<int32>(Asset->GetEmitters().size());
		UParticleEmitter* Emitter = GUObjectArray.CreateObject<UParticleEmitter>(Asset);
		UParticleLODLevel* LOD = GUObjectArray.CreateObject<UParticleLODLevel>(Emitter);

		UParticleModuleRequired* Required = GUObjectArray.CreateObject<UParticleModuleRequired>(LOD);
		UParticleModuleTypeDataSprite* TypeData = GUObjectArray.CreateObject<UParticleModuleTypeDataSprite>(LOD);
		UParticleModuleSpawn* Spawn = GUObjectArray.CreateObject<UParticleModuleSpawn>(LOD);
		UParticleModuleLifetime* Lifetime = GUObjectArray.CreateObject<UParticleModuleLifetime>(LOD);
		UParticleModuleVelocity* Velocity = GUObjectArray.CreateObject<UParticleModuleVelocity>(LOD);

		Emitter->SetEmitterName(FName("Emitter_" + std::to_string(NewEmitterIndex + 1)));
		LOD->SetRequiredModule(Required);
		LOD->SetTypeDataModule(TypeData);
		LOD->AddModule(Spawn);
		LOD->AddModule(Lifetime);
		LOD->AddModule(Velocity);
		LOD->CacheModules();

		Emitter->AddLODLevel(LOD);
		Asset->AddEmitter(Emitter);
		Asset->CacheSystemModuleInfo();

		if (UParticleSystemComponent* PreviewComponent = ViewportClient.GetPreviewComponent())
		{
			PreviewComponent->SetTemplate(Asset);
		}

		SelectedEmitterIndex = NewEmitterIndex;
		SelectedModule = nullptr;
		MarkDirty();
	}
	ImGui::SameLine();
	const bool bCanRemoveEmitter = SelectedEmitterIndex >= 0 && SelectedEmitterIndex < static_cast<int32>(Asset->GetEmitters().size());
	ImGui::BeginDisabled(!bCanRemoveEmitter);
	if (ImGui::Button("Remove Emitter"))
	{
		if (SelectedEmitterIndex >= 0 && SelectedEmitterIndex < static_cast<int32>(Asset->GetEmitters().size()))
		{
			Asset->RemoveEmitter(SelectedEmitterIndex);
			Asset->CacheSystemModuleInfo();

			SelectedEmitterIndex = -1;
			SelectedModule = nullptr;

			if (UParticleSystemComponent* PreviewComponent = ViewportClient.GetPreviewComponent())
			{
				PreviewComponent->SetTemplate(Asset);
			}

			MarkDirty();
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();

	UParticleEmitter* SelectedEmitter = nullptr;
	if (SelectedEmitterIndex >= 0 && SelectedEmitterIndex < static_cast<int32>(Asset->GetEmitters().size()))
	{
		SelectedEmitter = Asset->GetEmitter(SelectedEmitterIndex);
	}

	UParticleLODLevel* SelectedLOD = SelectedEmitter ? SelectedEmitter->GetLODLevel(0) : nullptr;
	ImGui::BeginDisabled(!SelectedEmitter);
	if (ImGui::Button("Add Module"))
	{
		ImGui::OpenPopup("##AddModulePopup");
	}
	ImGui::EndDisabled();
	if (SelectedEmitter && ImGui::BeginPopup("##AddModulePopup"))
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
			if (bAlreadyExists)
			{
				continue;
			}

			if (ImGui::Selectable(Option.Name, false))
			{
				UParticleModule* NewModule = nullptr;
				switch (Option.Class)
				{
				case EParticleModuleClass::Spawn: NewModule = GUObjectArray.CreateObject<UParticleModuleSpawn>(SelectedLOD); break;
				case EParticleModuleClass::Lifetime: NewModule = GUObjectArray.CreateObject<UParticleModuleLifetime>(SelectedLOD); break;
				case EParticleModuleClass::Location: NewModule = GUObjectArray.CreateObject<UParticleModuleLocation>(SelectedLOD); break;
				case EParticleModuleClass::Velocity: NewModule = GUObjectArray.CreateObject<UParticleModuleVelocity>(SelectedLOD); break;
				case EParticleModuleClass::Color: NewModule = GUObjectArray.CreateObject<UParticleModuleColor>(SelectedLOD); break;
				case EParticleModuleClass::Size: NewModule = GUObjectArray.CreateObject<UParticleModuleSize>(SelectedLOD); break;
				case EParticleModuleClass::Rotation: NewModule = GUObjectArray.CreateObject<UParticleModuleRotation>(SelectedLOD); break;
				case EParticleModuleClass::RotationRate: NewModule = GUObjectArray.CreateObject<UParticleModuleRotationRate>(SelectedLOD); break;
				case EParticleModuleClass::Acceleration: NewModule = GUObjectArray.CreateObject<UParticleModuleAcceleration>(SelectedLOD); break;
				case EParticleModuleClass::Attractor: NewModule = GUObjectArray.CreateObject<UParticleModuleAttractor>(SelectedLOD); break;
				case EParticleModuleClass::Orbit: NewModule = GUObjectArray.CreateObject<UParticleModuleOrbit>(SelectedLOD); break;
				case EParticleModuleClass::Collision: NewModule = GUObjectArray.CreateObject<UParticleModuleCollision>(SelectedLOD); break;
				case EParticleModuleClass::Kill: NewModule = GUObjectArray.CreateObject<UParticleModuleKill>(SelectedLOD); break;
				case EParticleModuleClass::EventGenerator: NewModule = GUObjectArray.CreateObject<UParticleModuleEventGenerator>(SelectedLOD); break;
				case EParticleModuleClass::EventReceiverSpawn: NewModule = GUObjectArray.CreateObject<UParticleModuleEventReceiverSpawn>(SelectedLOD); break;
				case EParticleModuleClass::EventReceiverKillAll: NewModule = GUObjectArray.CreateObject<UParticleModuleEventReceiverKillAll>(SelectedLOD); break;
				case EParticleModuleClass::SubUV: NewModule = GUObjectArray.CreateObject<UParticleModuleSubUV>(SelectedLOD); break;
				case EParticleModuleClass::Light: NewModule = GUObjectArray.CreateObject<UParticleModuleLight>(SelectedLOD); break;
				case EParticleModuleClass::VectorField: NewModule = GUObjectArray.CreateObject<UParticleModuleVectorField>(SelectedLOD); break;
				case EParticleModuleClass::Camera: NewModule = GUObjectArray.CreateObject<UParticleModuleCamera>(SelectedLOD); break;
				case EParticleModuleClass::Parameter: NewModule = GUObjectArray.CreateObject<UParticleModuleParameter>(SelectedLOD); break;
				default: break;
				}

				if (NewModule)
				{
					SelectedLOD->AddModule(NewModule);
					SelectedLOD->CacheModules();
					Asset->CacheSystemModuleInfo();

					if (UParticleSystemComponent* PreviewComponent = ViewportClient.GetPreviewComponent())
					{
						PreviewComponent->SetTemplate(Asset);
					}

					SelectedModule = NewModule;
					MarkDirty();
				}
			}
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();

	bool bCanRemoveModule = false;
	int32 SelectedModuleIndex = -1;
	if (SelectedModule)
	{
		const TArray<UParticleModule*>& Modules = SelectedLOD->GetModules();
		for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
		{
			if (Modules[ModuleIndex] == SelectedModule)
			{
				SelectedModuleIndex = ModuleIndex;
				bCanRemoveModule = true;
				break;
			}
		}
	}
	ImGui::BeginDisabled(!bCanRemoveModule);
	if (ImGui::Button("Remove Module"))
	{
		UParticleModule* RemovedModule = SelectedModule;
		SelectedModule = nullptr;
		SelectedLOD->RemoveModule(SelectedModuleIndex);
		GUObjectArray.RemoveObject(RemovedModule);
		GUObjectArray.DestroyObject(RemovedModule);
		SelectedLOD->CacheModules();
		Asset->CacheSystemModuleInfo();

		if (UParticleSystemComponent* PreviewComponent = ViewportClient.GetPreviewComponent())
		{
			PreviewComponent->SetTemplate(Asset);
		}

		MarkDirty();
	}
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
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticleEditorZeroSpacing);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
	ImGui::BeginChild("Viewport", Size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::BeginGroup();
	{
		ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
		ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

		FViewport* VP = ViewportClient.GetViewport();
		if (VP && Size.x > 0.0f && Size.y > 0.0f)
		{
			VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
			ViewportWindow.SetRect({ViewportPos.x, ViewportPos.y, Size.x, Size.y});

			if (VP->GetSRV())
			{
				ImGui::Image((ImTextureID)VP->GetSRV(), Size);
			}

			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRectFilled(
				ViewportPos,
				ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ParticlePanelHeaderHeight),
				ImGui::GetColorU32(ParticlePanelHeaderColor));
			DrawList->AddRectFilled(
				ViewportPos,
				ImVec2(ViewportPos.x + ParticlePanelAccentWidth, ViewportPos.y + ParticlePanelHeaderHeight),
				ImGui::GetColorU32(ParticlePanelAccentColor));
			DrawList->AddText(ImVec2(ViewportPos.x + ParticlePanelTitleOffsetX, ViewportPos.y + ParticleViewportTitleOffsetY), ImGui::GetColorU32(ImGuiCol_Text), "Viewport");

		}
	}
	ImGui::EndGroup();
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
}

UParticleSystemComponent* FParticleSystemEditorWidget::GetPreviewParticleComponent() const
{
	return ViewportClient.GetPreviewComponent();
}

void FParticleSystemEditorWidget::ReplayPreviewParticleSystem()
{
	if (UParticleSystemComponent* PreviewComponent = GetPreviewParticleComponent())
	{
		PreviewComponent->DeactivateSystem();
		PreviewComponent->ActivateSystem();
	}
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
	auto DrawFloatCurve = [&bChanged](const char* Name, FParticleFloatCurve& Curve)
	{
		ImGui::PushID(Name);
		ImGui::TextUnformatted(Name);
		ImGui::NextColumn();
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::TextDisabled("Use Curve Editor for per-key interpolation");
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

		for (int32 KeyIndex = 0; KeyIndex < static_cast<int32>(Curve.Keys.size()); ++KeyIndex)
		{
			FFloatCurveKey& Key = Curve.Keys[KeyIndex];
			ImGui::PushID(KeyIndex);
			char Label[64];
			snprintf(Label, sizeof(Label), "Key %d T/V/AT/LT", KeyIndex);
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

	ImGui::SeparatorText("Distribution");
	ImGui::Columns(2, "##ParticleDistributionEditRows", false);
	ImGui::SetColumnWidth(0, ParticleDetailsColumnWidth);

	// Helper to determine what fields to show based on distribution type
	auto FloatDistFields = [&](UDistributionFloat* Dist)
	{
		if (!Dist) return;
		const bool bCurve   = (Dist->Type == EDistributionType::ConstantCurve || Dist->Type == EDistributionType::UniformCurve);
		const bool bUniform = (Dist->Type == EDistributionType::Uniform        || Dist->Type == EDistributionType::UniformCurve);
		const bool bConstant = Dist->Type == EDistributionType::Constant;
		DrawType(Dist->Type);
		DrawBool("Can be Baked", Dist->bCanBeBaked);
		if (bConstant) DrawFloat("Constant", Dist->Min);
		if (bUniform)  DrawFloat("Min", Dist->Min);
		if (bUniform)  DrawFloat("Max", Dist->Max);
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
		DrawType(Dist->Type);
		DrawBool("Can be Baked", Dist->bCanBeBaked);
		DrawLockedAxesMode("Locked Axes", Dist->LockedAxesMode);
		if (bConstant) DrawVector("Constant", Dist->Min);
		if (bUniform) DrawVector("Min", Dist->Min);
		if (bUniform) DrawVector("Max", Dist->Max);
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
		DrawType(Dist->Type);
		DrawBool("Can be Baked", Dist->bCanBeBaked);
		if (bConstant) DrawColor("Constant", Dist->Min);
		if (bUniform) DrawColor("Min", Dist->Min);
		if (bUniform) DrawColor("Max", Dist->Max);
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

	if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
	{
		UDistributionFloat* Dist = Lifetime->GetLifetimeDist();
		ImGui::TextUnformatted("Object"); ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "LifetimeDist" : "LifetimeDist is null"); ImGui::NextColumn();
		FloatDistFields(Dist);
	}
	else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
	{
		UDistributionVector* Dist = Location->GetLocationDist();
		ImGui::TextUnformatted("Object"); ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "LocationDist" : "LocationDist is null"); ImGui::NextColumn();
		VectorDistFields(Dist);
	}
	else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
	{
		UDistributionVector* Dist = Velocity->GetVelocityDist();
		ImGui::TextUnformatted("Object"); ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "VelocityDist" : "VelocityDist is null"); ImGui::NextColumn();
		VectorDistFields(Dist);
	}
	else if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
	{
		UDistributionLinearColor* Dist = Color->GetColorDist();
		ImGui::TextUnformatted("Object"); ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "ColorDist" : "ColorDist is null"); ImGui::NextColumn();
		ColorDistFields(Dist);
	}
	else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
	{
		UDistributionVector* Dist = Size->GetSizeDist();
		ImGui::TextUnformatted("Object"); ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "SizeDist" : "SizeDist is null"); ImGui::NextColumn();
		VectorDistFields(Dist);
	}
	else if (UParticleModuleRotation* Rotation = Cast<UParticleModuleRotation>(Module))
	{
		UDistributionFloat* Dist = Rotation->GetRotationDist();
		ImGui::TextUnformatted("Object"); ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "RotationDist" : "RotationDist is null"); ImGui::NextColumn();
		FloatDistFields(Dist);
	}
	else if (UParticleModuleRotationRate* RotationRate = Cast<UParticleModuleRotationRate>(Module))
	{
		UDistributionFloat* Dist = RotationRate->GetRotationRateDist();
		ImGui::TextUnformatted("Object"); ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "RotationRateDist" : "RotationRateDist is null"); ImGui::NextColumn();
		FloatDistFields(Dist);
	}
	else
	{
		ImGui::TextUnformatted("Object");
		ImGui::NextColumn();
		ImGui::TextUnformatted("No distribution variables");
		ImGui::NextColumn();
	}

	ImGui::Columns(1);
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
	if (Props.empty())
	{
		ImGui::TextDisabled("No editable properties.");
		return false;
	}

	bool bChanged = false;
	ImGui::Columns(2, "##ParticleDetailsColumns", false);
	ImGui::SetColumnWidth(0, ParticleDetailsColumnWidth);
	for (int32 PropIndex = 0; PropIndex < static_cast<int32>(Props.size()); ++PropIndex)
	{
		const FProperty* Prop = Props[PropIndex];
		if (!Prop)
		{
			continue;
		}

		ImGui::PushID(PropIndex);
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
	if (UParticleModule* Module = Cast<UParticleModule>(Object))
	{
		Module->CacheModuleValues();
	}

	UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject);
	UParticleEmitter* Emitter = (SelectedEmitterIndex >= 0 && Asset) ? Asset->GetEmitter(SelectedEmitterIndex) : nullptr;
	UParticleLODLevel* LOD = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	if (LOD)
	{
		LOD->CacheModules();
	}
	if (Asset)
	{
		Asset->CacheSystemModuleInfo();
	}
	if (UParticleSystemComponent* PreviewComponent = ViewportClient.GetPreviewComponent())
	{
		PreviewComponent->SetTemplate(Asset);
	}
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
	UParticleLODLevel* LODLevel = Emitter->GetLODLevel(0);
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

	if (ID3D11ShaderResourceView* PreviewSRV = GetParticleEmitterPreviewSRV(Emitter))
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
	DrawList->AddText(ImVec2(BadgeMin.x + 6.0f, BadgeMin.y + 2.0f), TypeColor, TypeLabel);

	const float CountBadgeWidth = 28.0f;
	const ImVec2 CountBadgeMin(TypeBadgeMax.x + 6.0f, BadgeMin.y);
	if (CountBadgeMin.x + CountBadgeWidth <= TextMaxX)
	{
		const ImVec2 CountBadgeMax(CountBadgeMin.x + CountBadgeWidth, BadgeMin.y + ParticleEmitterHeaderBadgeHeight);
		DrawList->AddRectFilled(CountBadgeMin, CountBadgeMax, IM_COL32(28, 28, 32, 180), 3.0f);
		const ImVec2 CountSize = ImGui::CalcTextSize(CountText);
		DrawList->AddText(
			ImVec2(CountBadgeMin.x + (CountBadgeWidth - CountSize.x) * 0.5f, CountBadgeMin.y + 2.0f),
			IM_COL32(245, 245, 245, 255),
			CountText);
	}

	ImGui::Dummy(ImVec2(1.0f, ParticleEmitterHeaderBottomSpacing));

	return false;
}

bool FParticleSystemEditorWidget::RenderEmitterModules(UParticleEmitter* Emitter, int32 EmitterIndex)
{
	bool bChanged = false;

	UParticleLODLevel* LODLevel = Emitter->GetLODLevel(0);

	if (UParticleModuleTypeDataBase* TypeDataModule = LODLevel->GetTypeDataModule())
	{
		bChanged |= RenderParticleModuleItem(TypeDataModule, EmitterIndex);
	}

	UParticleModuleRequired* RequiredModule = LODLevel->GetRequiredModule();
	bChanged |= RenderParticleModuleItem(RequiredModule, EmitterIndex);

	const TArray<UParticleModule*>& Modules = LODLevel->GetModules();
	for (int32 ModuleIndex = 0; ModuleIndex < static_cast<int32>(Modules.size()); ++ModuleIndex)
	{
		UParticleModule* Module = Modules[ModuleIndex];
		bChanged |= RenderParticleModuleItem(Module, EmitterIndex);
	}

	return bChanged;
}

bool FParticleSystemEditorWidget::RenderParticleModuleItem(UParticleModule* Module, int32 EmitterIndex)
{
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
	case EParticleModuleClass::SubUV: ModuleName = "SubUV"; break;
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
	const bool bModuleSelected = SelectedModule == Module;
	const FParticleModuleStyleColors& ModuleColors =
		(ModuleClass == EParticleModuleClass::Spawn) ? ParticleSpawnModuleColors :
		((ModuleClass == EParticleModuleClass::Required) ? ParticleRequiredModuleColors :
		(((ModuleClass == EParticleModuleClass::TypeDataSprite)
		|| (ModuleClass == EParticleModuleClass::TypeDataMesh)
		|| (ModuleClass == EParticleModuleClass::TypeDataBeam)
		|| (ModuleClass == EParticleModuleClass::TypeDataRibbon)) ? ParticleTypeDataModuleColors :
		ParticleNormalModuleColors));
	const ImVec4 ModuleColor = bModuleSelected ? ModuleColors.Selected : ModuleColors.Default;
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
	if (ImGui::Selectable(ModuleName, true, SelectableFlags, ImVec2(0.0f, ParticleModuleHeight)))
	{
		SelectedEmitterIndex = EmitterIndex;
		SelectedModule = Module;
	}
	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar(2);

	if (bCanToggleModule)
	{
		const ImVec2 RowEnd = ImVec2(RowStart.x, RowStart.y + ParticleModuleHeight);
		const float CheckboxSize = ImGui::GetFontSize() + ParticleModuleCheckboxFramePadding * 2.0f;
		ImGui::SetCursorScreenPos(ImVec2(
			RowStart.x + RowWidth - CheckboxSize - ParticleModuleCheckboxRightPadding,
			RowStart.y + (ParticleModuleHeight - CheckboxSize) * 0.5f));
		ImGui::PushID(Module);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ParticleModuleCheckboxFramePadding, ParticleModuleCheckboxFramePadding));
		bool bEnabled = Module->IsEnabled();
		if (ImGui::Checkbox("##ModuleEnabled", &bEnabled))
		{
			Module->SetEnabled(bEnabled);
			MarkDirty();
			bChanged = true;
		}
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
		const char*          Name;
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
			Channels[ChannelCount++] = { Label, &D->MinCurve, bUniform ? &D->MaxCurve : nullptr };
	};
	auto TryAddVector = [&](UDistributionVector* D, const char* XL, const char* YL, const char* ZL)
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
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { "R", &D->MinCurve.R, bUniform ? &D->MaxCurve.R : nullptr };
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { "G", &D->MinCurve.G, bUniform ? &D->MaxCurve.G : nullptr };
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { "B", &D->MinCurve.B, bUniform ? &D->MaxCurve.B : nullptr };
		if (ChannelCount < MaxChannels) Channels[ChannelCount++] = { "A", &D->MinCurve.A, bUniform ? &D->MaxCurve.A : nullptr };
	};

	if (UParticleModuleLifetime* M      = Cast<UParticleModuleLifetime>(SelectedModule))     TryAddFloat(M->GetLifetimeDist(), "Lifetime");
	else if (UParticleModuleRotation* M = Cast<UParticleModuleRotation>(SelectedModule))     TryAddFloat(M->GetRotationDist(), "Rotation");
	else if (UParticleModuleRotationRate* M = Cast<UParticleModuleRotationRate>(SelectedModule)) TryAddFloat(M->GetRotationRateDist(), "RotRate");
	else if (UParticleModuleLocation* M = Cast<UParticleModuleLocation>(SelectedModule))     TryAddVector(M->GetLocationDist(), "Loc.X","Loc.Y","Loc.Z");
	else if (UParticleModuleVelocity* M = Cast<UParticleModuleVelocity>(SelectedModule))     TryAddVector(M->GetVelocityDist(), "Vel.X","Vel.Y","Vel.Z");
	else if (UParticleModuleSize* M     = Cast<UParticleModuleSize>(SelectedModule))         TryAddVector(M->GetSizeDist(),     "Size.X","Size.Y","Size.Z");
	else if (UParticleModuleColor* M    = Cast<UParticleModuleColor>(SelectedModule))        TryAddColor(M->GetColorDist());

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

	if (ChannelCount > 1)
	{
		const char* Names[MaxChannels];
		for (int32 i = 0; i < ChannelCount; ++i) Names[i] = Channels[i].Name;
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::Combo("Ch##CurveCh", &CurveChannelIdx, Names, ChannelCount))
		{
			CurveSelectedKey = -1;
			CurveSelCurve = 0;
			CurveDraggingTangentHandle = ECurveTangentHandle::None;
			bCurveDragKey = false;
		}
		ImGui::SameLine();
	}
	if (ImGui::Button("Fit##CurveFit"))
	{
		PFitCurveViewToKeys(Ch.MinCurve, Ch.MaxCurve, CurveViewMinT, CurveViewMaxT, CurveViewMinV, CurveViewMaxV);
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
