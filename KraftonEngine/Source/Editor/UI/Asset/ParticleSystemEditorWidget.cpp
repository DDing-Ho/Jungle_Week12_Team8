#include "ParticleSystemEditorWidget.h"

#include "Engine/Particles/Assets/ParticleAsset.h"
#include "Engine/GameFramework/WorldContext.h"
#include "Engine/Runtime/Engine.h"
#include "Engine/Component/ParticleSystemComponent.h"
#include "Engine/Viewport/Viewport.h"
#include "Editor/Slate/SlateApplication.h"

#include <imgui.h>

// 바닥 시각화
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"
#include "GameFramework/StaticMeshActor.h"
#include "Engine/Component/StaticMeshComponent.h"

static uint32 GNextParticleSystemEditorInstanceId = 0;

FParticleSystemEditorWidget::FParticleSystemEditorWidget()
    : InstanceId(GNextParticleSystemEditorInstanceId++)
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

    FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
    WorldContext.World->SetWorldType(EWorldType::EditorPreview);
    WorldContext.World->InitWorld();

    AActor* Actor = WorldContext.World->SpawnActor<AActor>();
    if (UParticleSystem* Asset = Cast<UParticleSystem>(EditedObject))
    {
        UParticleSystemComponent* Comp = Actor->AddComponent<UParticleSystemComponent>();
        Comp->SetTemplate(Asset);
        Actor->SetRootComponent(Comp);
    }
    Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

    AStaticMeshActor* FloorActor = WorldContext.World->SpawnActor<AStaticMeshActor>();
    FloorActor->InitDefaultComponents("Asset/Mesh/CubeGrid/SM_CubeGrid_StaticMesh.uasset");
    FloorActor->SetActorLocation(FVector(0.0f, 0.0f, -0.1f));
    FloorActor->SetActorScale(FVector(10.0f, 10.0f, 0.02f));

    ImVec2 ViewportSize = ImGui::GetContentRegionAvail();

	ViewportClient.Initialize(GEngine->GetRenderer().GetFD3DDevice().GetDevice(), ViewportSize.x, ViewportSize.y);
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

    VisibleTitle += AssetPath;

    if (IsDirty())
    {
        VisibleTitle += " *";
    }

    ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_None;
    if (ViewportClient.IsMouseOverViewport())
    {
        WindowFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    }

    FString WindowTitle = VisibleTitle + WindowIdSuffix;
    if (ConsumeFocusRequest())
    {
        ImGui::SetNextWindowFocus();
    }

    if (!ImGui::Begin(WindowTitle.c_str(), &bWindowOpen, WindowFlags))
    {
        ImGui::End();
        if (!bWindowOpen)
        {
            Close();
        }
        return;
    }

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
    {
        FSlateApplication::Get().BringViewportToFront(&ViewportClient);
    }

    RenderPreviewViewport();
	const bool bChanged = RenderDetailsPanel();

	if (bChanged)
	{
		MarkDirty();
	}

	ImGui::End();

    if (!bWindowOpen)
	{
		bPendingClose = true;
	}
}

void FParticleSystemEditorWidget::RenderPreviewViewport()
{
    const float spacing = 10.0f;
	ImGui::BeginGroup();
	{
        float AvailableWidth = ImGui::GetContentRegionAvail().x - spacing - ImGui::GetStyle().ItemSpacing.x;
        ImVec2 Size = ImVec2(AvailableWidth, ImGui::GetContentRegionAvail().y);

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

            constexpr float ToolbarHeight = 28.0f;
            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            DrawList->AddRectFilled(
                ViewportPos,
                ImVec2(ViewportPos.x + Size.x, ViewportPos.y + ToolbarHeight),
                IM_COL32(40, 40, 40, 255));
        }
    }
    ImGui::EndGroup();
}

bool FParticleSystemEditorWidget::RenderDetailsPanel()
{
	return false;
}
