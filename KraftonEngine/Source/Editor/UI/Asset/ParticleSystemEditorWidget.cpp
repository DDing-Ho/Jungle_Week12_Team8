#include "ParticleSystemEditorWidget.h"

#include "Engine/Particles/Assets/ParticleAsset.h"
#include "Engine/Particles/Assets/ParticleSystemAssetManager.h"
#include "Engine/Particles/Modules/ParticleCoreModules.h"
#include "Engine/Particles/Modules/ParticleMotionModules.h"
#include "Engine/Particles/Modules/ParticleRenderExpressionModules.h"
#include "Core/Property/FEnumProperty.h"
#include "Core/Property/FStructProperty.h"
#include "Core/Property/PropertyTypes.h"
#include "Engine/GameFramework/WorldContext.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/Component/ParticleSystemComponent.h"
#include "Engine/Viewport/Viewport.h"
#include "Editor/Slate/SlateApplication.h"
#include "Editor/UI/ContentBrowser/ContentItem.h"
#include "Materials/MaterialManager.h"
#include "Object/FUObjectArray.h"
#include "Platform/Paths.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>

// 바닥 시각화
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "GameFramework/StaticMeshActor.h"
#include "Engine/Component/StaticMeshComponent.h"

static uint32 NextParticleSystemEditorInstanceId = 0;

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

constexpr FParticleModuleStyleColors ParticleTypeDataModuleColors = { ImVec4(0.078f, 0.078f, 0.098f, 1.0f), ImVec4(1.0f, 0.39f, 0.0f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleRequiredModuleColors = { ImVec4(0.784f, 0.784f, 0.392f, 1.0f), ImVec4(1.0f, 0.882f, 0.196f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleSpawnModuleColors = { ImVec4(0.784f, 0.392f, 0.392f, 1.0f), ImVec4(1.0f, 0.196f, 0.196f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleNormalModuleColors = { ImVec4(0.157f, 0.157f, 0.192f, 1.0f), ImVec4(1.0f, 0.392f, 0.0f, 1.0f) };

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

constexpr ImVec2 ParticleEditorInitialWindowSize = ImVec2(1080.0f, 640.0f);
constexpr ImVec2 ParticleEditorZeroSpacing = ImVec2(0.0f, 0.0f);

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

constexpr float ParticleEmitterSpacing = 10.0f;
constexpr float ParticleEmitterWidth = 180.0f;
constexpr float ParticleEmitterHeaderHeight = 20.0f;
constexpr float ParticleEmitterHeaderBottomSpacing = 6.0f;

constexpr float ParticleModuleItemSpacing = 2.0f;
constexpr float ParticleModuleHeight = 24.0f;
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

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticleEditorZeroSpacing);
		ImGui::BeginChild("Details", ImVec2(Layout.LeftPanelWidth, Layout.LeftBottomPanelHeight), true);
		bChanged |= RenderDetailsPanel();
		ImGui::EndChild();
		ImGui::PopStyleVar();
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
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticleEditorZeroSpacing);
		ImGui::BeginChild("Emitters", ImVec2(Layout.RightPanelWidth, Layout.RightTopPanelHeight), true);
		bChanged |= RenderEmittersPanel();
		ImGui::EndChild();
		ImGui::PopStyleVar();

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

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticleEditorZeroSpacing);
		ImGui::BeginChild("Curve Editor", ImVec2(Layout.RightPanelWidth, Layout.RightBottomPanelHeight), true);
		bChanged |= RenderCurveEditorPanel();
		ImGui::EndChild();
		ImGui::PopStyleVar();
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
				ImGui::GetColorU32(ImGuiCol_Header));
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

bool FParticleSystemEditorWidget::RenderDetailsPanel()
{
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Start = ImGui::GetCursorScreenPos();
	const float Width = ImGui::GetContentRegionAvail().x;
	DrawList->AddRectFilled(Start, ImVec2(Start.x + Width, Start.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ImGuiCol_Header));
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
	auto DrawFloatCurve = [&bChanged](const char* Name, FParticleFloatCurve& Curve)
	{
		ImGui::PushID(Name);
		ImGui::TextUnformatted(Name);
		ImGui::NextColumn();
		const char* Modes[] = { "Linear", "Constant", "CurveAuto", "CurveUser", "CurveBreak" };
		int32 Mode = static_cast<int32>(Curve.InterpMode);
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::Combo("##InterpMode", &Mode, Modes, 5))
		{
			Curve.InterpMode = static_cast<EParticleCurveInterpMode>(Mode);
			if (Curve.InterpMode == EParticleCurveInterpMode::CurveAuto)
			{
				Curve.ComputeAutoTangents();
			}
			bChanged = true;
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
			Curve.Keys.push_back({ Time, Value, 0.0f, 0.0f });
			if (Curve.InterpMode == EParticleCurveInterpMode::CurveAuto)
			{
				Curve.ComputeAutoTangents();
			}
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
				if (Curve.InterpMode == EParticleCurveInterpMode::CurveUser)
				{
					Key.ArriveTangent = Key.LeaveTangent;
				}
				if (Curve.InterpMode == EParticleCurveInterpMode::CurveAuto)
				{
					Curve.ComputeAutoTangents();
				}
				bChanged = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Remove"))
			{
				Curve.Keys.erase(Curve.Keys.begin() + KeyIndex);
				if (Curve.InterpMode == EParticleCurveInterpMode::CurveAuto)
				{
					Curve.ComputeAutoTangents();
				}
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

	ImGui::Separator();
	ImGui::TextUnformatted("Distribution");
	ImGui::Columns(2, "##ParticleDistributionEditRows", false);
	ImGui::SetColumnWidth(0, ParticleDetailsColumnWidth);

	if (UParticleModuleLifetime* Lifetime = Cast<UParticleModuleLifetime>(Module))
	{
		UDistributionFloat* Dist = Lifetime->GetLifetimeDist();
		ImGui::TextUnformatted("Object");
		ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "LifetimeDist" : "LifetimeDist is null");
		ImGui::NextColumn();
		if (Dist)
		{
			DrawType(Dist->Type);
			DrawFloat("Min", Dist->Min);
			DrawFloat("Max", Dist->Max);
			DrawFloatCurve("MinCurve", Dist->MinCurve);
			DrawFloatCurve("MaxCurve", Dist->MaxCurve);
		}
	}
	else if (UParticleModuleLocation* Location = Cast<UParticleModuleLocation>(Module))
	{
		UDistributionVector* Dist = Location->GetLocationDist();
		ImGui::TextUnformatted("Object");
		ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "LocationDist" : "LocationDist is null");
		ImGui::NextColumn();
		if (Dist)
		{
			DrawType(Dist->Type);
			DrawVector("Min", Dist->Min);
			DrawVector("Max", Dist->Max);
			DrawBool("bLockAxes", Dist->bLockAxes);
			DrawFloatCurve("MinCurve.X", Dist->MinCurve.X);
			DrawFloatCurve("MinCurve.Y", Dist->MinCurve.Y);
			DrawFloatCurve("MinCurve.Z", Dist->MinCurve.Z);
			DrawFloatCurve("MaxCurve.X", Dist->MaxCurve.X);
			DrawFloatCurve("MaxCurve.Y", Dist->MaxCurve.Y);
			DrawFloatCurve("MaxCurve.Z", Dist->MaxCurve.Z);
		}
	}
	else if (UParticleModuleVelocity* Velocity = Cast<UParticleModuleVelocity>(Module))
	{
		UDistributionVector* Dist = Velocity->GetVelocityDist();
		ImGui::TextUnformatted("Object");
		ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "VelocityDist" : "VelocityDist is null");
		ImGui::NextColumn();
		if (Dist)
		{
			DrawType(Dist->Type);
			DrawVector("Min", Dist->Min);
			DrawVector("Max", Dist->Max);
			DrawBool("bLockAxes", Dist->bLockAxes);
			DrawFloatCurve("MinCurve.X", Dist->MinCurve.X);
			DrawFloatCurve("MinCurve.Y", Dist->MinCurve.Y);
			DrawFloatCurve("MinCurve.Z", Dist->MinCurve.Z);
			DrawFloatCurve("MaxCurve.X", Dist->MaxCurve.X);
			DrawFloatCurve("MaxCurve.Y", Dist->MaxCurve.Y);
			DrawFloatCurve("MaxCurve.Z", Dist->MaxCurve.Z);
		}
	}
	else if (UParticleModuleColor* Color = Cast<UParticleModuleColor>(Module))
	{
		UDistributionLinearColor* Dist = Color->GetColorDist();
		ImGui::TextUnformatted("Object");
		ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "ColorDist" : "ColorDist is null");
		ImGui::NextColumn();
		if (Dist)
		{
			DrawType(Dist->Type);
			DrawColor("Min", Dist->Min);
			DrawColor("Max", Dist->Max);
			DrawFloatCurve("MinCurve.R", Dist->MinCurve.R);
			DrawFloatCurve("MinCurve.G", Dist->MinCurve.G);
			DrawFloatCurve("MinCurve.B", Dist->MinCurve.B);
			DrawFloatCurve("MinCurve.A", Dist->MinCurve.A);
			DrawFloatCurve("MaxCurve.R", Dist->MaxCurve.R);
			DrawFloatCurve("MaxCurve.G", Dist->MaxCurve.G);
			DrawFloatCurve("MaxCurve.B", Dist->MaxCurve.B);
			DrawFloatCurve("MaxCurve.A", Dist->MaxCurve.A);
		}
	}
	else if (UParticleModuleSize* Size = Cast<UParticleModuleSize>(Module))
	{
		UDistributionVector* Dist = Size->GetSizeDist();
		ImGui::TextUnformatted("Object");
		ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "SizeDist" : "SizeDist is null");
		ImGui::NextColumn();
		if (Dist)
		{
			DrawType(Dist->Type);
			DrawVector("Min", Dist->Min);
			DrawVector("Max", Dist->Max);
			DrawBool("bLockAxes", Dist->bLockAxes);
			DrawFloatCurve("MinCurve.X", Dist->MinCurve.X);
			DrawFloatCurve("MinCurve.Y", Dist->MinCurve.Y);
			DrawFloatCurve("MinCurve.Z", Dist->MinCurve.Z);
			DrawFloatCurve("MaxCurve.X", Dist->MaxCurve.X);
			DrawFloatCurve("MaxCurve.Y", Dist->MaxCurve.Y);
			DrawFloatCurve("MaxCurve.Z", Dist->MaxCurve.Z);
		}
	}
	else if (UParticleModuleRotation* Rotation = Cast<UParticleModuleRotation>(Module))
	{
		UDistributionFloat* Dist = Rotation->GetRotationDist();
		ImGui::TextUnformatted("Object");
		ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "RotationDist" : "RotationDist is null");
		ImGui::NextColumn();
		if (Dist)
		{
			DrawType(Dist->Type);
			DrawFloat("Min", Dist->Min);
			DrawFloat("Max", Dist->Max);
			DrawFloatCurve("MinCurve", Dist->MinCurve);
			DrawFloatCurve("MaxCurve", Dist->MaxCurve);
		}
	}
	else if (UParticleModuleRotationRate* RotationRate = Cast<UParticleModuleRotationRate>(Module))
	{
		UDistributionFloat* Dist = RotationRate->GetRotationRateDist();
		ImGui::TextUnformatted("Object");
		ImGui::NextColumn();
		ImGui::TextUnformatted(Dist ? "RotationRateDist" : "RotationRateDist is null");
		ImGui::NextColumn();
		if (Dist)
		{
			DrawType(Dist->Type);
			DrawFloat("Min", Dist->Min);
			DrawFloat("Max", Dist->Max);
			DrawFloatCurve("MinCurve", Dist->MinCurve);
			DrawFloatCurve("MaxCurve", Dist->MaxCurve);
		}
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

			const TArray<FMaterialAssetListItem>& MatFiles = FMaterialManager::Get().GetAvailableMaterialFiles();
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
				Slot->Path = FPaths::ToUtf8(ContentItem.Path.lexically_relative(FPaths::RootDir()).generic_wstring());
				bChanged = true;
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
	DrawList->AddRectFilled(HeaderStart, ImVec2(HeaderStart.x + HeaderWidth, HeaderStart.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ImGuiCol_Header));
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
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ParticleEditorZeroSpacing);
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
	ImGui::PopStyleVar();
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
	const ImVec4 EmitterHeaderColor = bEmitterSelected
		? ParticleNormalModuleColors.Selected
		: ImGui::GetStyleColorVec4(ImGuiCol_Header);
	ImGui::PushStyleColor(ImGuiCol_Header, EmitterHeaderColor);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, EmitterHeaderColor);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, EmitterHeaderColor);
	if (ImGui::Selectable(EmitterName.empty() ? "Particle Emitter" : EmitterName.c_str(), bEmitterSelected, ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.0f, ParticleEmitterHeaderHeight)))
	{
		SelectedEmitterIndex = EmitterIndex;
		SelectedModule = nullptr;
	}
	ImGui::PopStyleColor(3);
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
	ImGui::PushStyleColor(ImGuiCol_Header, ModuleColor);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ModuleColor);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, ModuleColor);
	if (ImGui::Selectable(ModuleName, true, SelectableFlags, ImVec2(0.0f, ParticleModuleHeight)))
	{
		SelectedEmitterIndex = EmitterIndex;
		SelectedModule = Module;
	}
	ImGui::PopStyleColor(3);

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
	DrawList->AddRectFilled(HeaderStart, ImVec2(HeaderStart.x + Width, HeaderStart.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ImGuiCol_Header));
	DrawList->AddRectFilled(HeaderStart, ImVec2(HeaderStart.x + ParticlePanelAccentWidth, HeaderStart.y + ParticlePanelHeaderHeight), ImGui::GetColorU32(ParticlePanelAccentColor));
	DrawList->AddText(ImVec2(HeaderStart.x + ParticlePanelTitleOffsetX, HeaderStart.y + ParticlePanelTitleOffsetY), ImGui::GetColorU32(ImGuiCol_Text), "Curve Editor");
	ImGui::Dummy(ImVec2(Width, ParticleCurveEditorHeaderSpacing));
	return false;
}
