#include "ParticleSystemEditorWidget.h"

#include "Engine/Particles/Assets/ParticleAsset.h"
#include "Engine/Particles/Assets/ParticleSystemAssetManager.h"
#include "Engine/Particles/Modules/ParticleCoreModules.h"
#include "Engine/GameFramework/WorldContext.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/Component/ParticleSystemComponent.h"
#include "Engine/Viewport/Viewport.h"
#include "Editor/Slate/SlateApplication.h"
#include "Object/FUObjectArray.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

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

constexpr FParticleModuleStyleColors ParticleTypeDataModuleColors = { ImVec4(0.078f, 0.078f, 0.098f, 1.0f), ImVec4(1.0f, 0.39f, 0.0f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleRequiredModuleColors = { ImVec4(0.784f, 0.784f, 0.392f, 1.0f), ImVec4(1.0f, 0.882f, 0.196f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleSpawnModuleColors = { ImVec4(0.784f, 0.392f, 0.392f, 1.0f), ImVec4(1.0f, 0.196f, 0.196f, 1.0f) };
constexpr FParticleModuleStyleColors ParticleNormalModuleColors = { ImVec4(0.157f, 0.157f, 0.192f, 1.0f), ImVec4(1.0f, 0.392f, 0.0f, 1.0f) };

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
	if (!bCanRemoveEmitter)
	{
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Remove Emitter"))
	{
		Asset->RemoveEmitter(SelectedEmitterIndex);
		Asset->CacheSystemModuleInfo();

		if (UParticleSystemComponent* PreviewComponent = ViewportClient.GetPreviewComponent())
		{
			PreviewComponent->SetTemplate(Asset);
		}

		SelectedEmitterIndex = -1;
		SelectedModule = nullptr;
		MarkDirty();
	}
	if (!bCanRemoveEmitter)
	{
		ImGui::EndDisabled();
	}

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

	ImGui::Columns(2, "##ParticleDetailsColumns", false);
	ImGui::SetColumnWidth(0, ParticleDetailsColumnWidth);

	if (SelectedModule)
	{
		const char* ModuleName = "Unknown";
		switch (SelectedModule->GetModuleClass())
		{
		case EParticleModuleClass::Required: ModuleName = "Required"; break;
		case EParticleModuleClass::Spawn: ModuleName = "Spawn"; break;
		case EParticleModuleClass::Lifetime: ModuleName = "Lifetime"; break;
		case EParticleModuleClass::Location: ModuleName = "Location"; break;
		case EParticleModuleClass::Velocity: ModuleName = "Velocity"; break;
		case EParticleModuleClass::Color: ModuleName = "Color"; break;
		case EParticleModuleClass::Size: ModuleName = "Size"; break;
		case EParticleModuleClass::TypeDataSprite: ModuleName = "TypeData Sprite"; break;
		case EParticleModuleClass::TypeDataMesh: ModuleName = "TypeData Mesh"; break;
		case EParticleModuleClass::TypeDataBeam: ModuleName = "TypeData Beam"; break;
		case EParticleModuleClass::TypeDataRibbon: ModuleName = "TypeData Ribbon"; break;
		default: break;
		}
		ImGui::TextUnformatted("Selection"); ImGui::NextColumn(); ImGui::TextUnformatted("Module"); ImGui::NextColumn();
		ImGui::TextUnformatted("Module"); ImGui::NextColumn(); ImGui::TextUnformatted(ModuleName); ImGui::NextColumn();
		ImGui::TextUnformatted("Enabled"); ImGui::NextColumn(); ImGui::TextUnformatted(SelectedModule->IsEnabled() ? "true" : "false"); ImGui::NextColumn();
		ImGui::TextUnformatted("Update Phase"); ImGui::NextColumn();
		switch (SelectedModule->GetUpdatePhase())
		{
		case EParticleModuleUpdatePhase::PMUP_Spawn: ImGui::TextUnformatted("Spawn"); break;
		case EParticleModuleUpdatePhase::PMUP_Update: ImGui::TextUnformatted("Update"); break;
		case EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate: ImGui::TextUnformatted("Spawn and Update"); break;
		}
		ImGui::NextColumn();
		if (UParticleModuleSpawn* SpawnModule = Cast<UParticleModuleSpawn>(SelectedModule))
		{
			ImGui::TextUnformatted("Spawn Rate"); ImGui::NextColumn(); ImGui::Text("%.2f", SpawnModule->GetSpawnRate()); ImGui::NextColumn();
			ImGui::TextUnformatted("Burst Count"); ImGui::NextColumn(); ImGui::Text("%d", SpawnModule->GetBurstCount()); ImGui::NextColumn();
		}
		else if (UParticleModuleRequired* RequiredModule = Cast<UParticleModuleRequired>(SelectedModule))
		{
			ImGui::TextUnformatted("Emitter Type"); ImGui::NextColumn();
			switch (RequiredModule->GetEmitterType())
			{
			case EParticleEmitterType::PET_Sprite: ImGui::TextUnformatted("Sprite"); break;
			case EParticleEmitterType::PET_Mesh: ImGui::TextUnformatted("Mesh"); break;
			case EParticleEmitterType::PET_Beam: ImGui::TextUnformatted("Beam"); break;
			case EParticleEmitterType::PET_Ribbon: ImGui::TextUnformatted("Ribbon"); break;
			}
			ImGui::NextColumn();
			ImGui::TextUnformatted("Sort Mode"); ImGui::NextColumn();
			switch (RequiredModule->GetSortMode())
			{
			case EParticleSortMode::PSM_None: ImGui::TextUnformatted("None"); break;
			case EParticleSortMode::PSM_DistanceToView: ImGui::TextUnformatted("Distance To View"); break;
			case EParticleSortMode::PSM_Age: ImGui::TextUnformatted("Age"); break;
			}
			ImGui::NextColumn();
		}
	}
	else if (SelectedEmitterIndex >= 0 && SelectedEmitterIndex < static_cast<int32>(Asset->GetEmitters().size()))
	{
		UParticleEmitter* Emitter = Asset->GetEmitter(SelectedEmitterIndex);
		const FString EmitterName = Emitter->GetEmitterName().ToString();
		ImGui::TextUnformatted("Selection"); ImGui::NextColumn(); ImGui::TextUnformatted("Emitter"); ImGui::NextColumn();
		ImGui::TextUnformatted("Name"); ImGui::NextColumn(); ImGui::TextUnformatted(EmitterName.empty() ? "Particle Emitter" : EmitterName.c_str()); ImGui::NextColumn();
		ImGui::TextUnformatted("Render Mode"); ImGui::NextColumn();
		switch (Emitter->GetEmitterRenderMode())
		{
		case EParticleEmitterRenderMode::ERM_Normal: ImGui::TextUnformatted("Normal"); break;
		case EParticleEmitterRenderMode::ERM_Point: ImGui::TextUnformatted("Point"); break;
		case EParticleEmitterRenderMode::ERM_Cross: ImGui::TextUnformatted("Cross"); break;
		case EParticleEmitterRenderMode::ERM_None: ImGui::TextUnformatted("None"); break;
		}
		ImGui::NextColumn();
		ImGui::TextUnformatted("Initial Allocation"); ImGui::NextColumn(); ImGui::Text("%d", Emitter->GetInitialAllocationCount()); ImGui::NextColumn();
		ImGui::TextUnformatted("Max Active"); ImGui::NextColumn(); ImGui::Text("%d", Emitter->GetMaxActiveParticles()); ImGui::NextColumn();
	}
	else
	{
		ImGui::TextUnformatted("Selection"); ImGui::NextColumn(); ImGui::TextUnformatted("Particle System"); ImGui::NextColumn();
		ImGui::TextUnformatted("Source Path"); ImGui::NextColumn(); ImGui::TextUnformatted(Asset->GetSourcePath().empty() ? "Unsaved particle system" : Asset->GetSourcePath().c_str()); ImGui::NextColumn();
		ImGui::TextUnformatted("Emitters"); ImGui::NextColumn(); ImGui::Text("%d", static_cast<int32>(Asset->GetEmitters().size())); ImGui::NextColumn();
		ImGui::TextUnformatted("LOD Distances"); ImGui::NextColumn(); ImGui::Text("%d", static_cast<int32>(Asset->LODDistances.size())); ImGui::NextColumn();
	}

	ImGui::Columns(1);
	return false;
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
