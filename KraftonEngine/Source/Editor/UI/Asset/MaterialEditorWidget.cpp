#include "MaterialEditorWidget.h"

#include "Component/Light/DirectionalLightComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Editor/UI/ContentBrowser/ContentItem.h"
#include "GameFramework/AActor.h"
#include "GameFramework/Light/DirectionalLightActor.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Mesh/MeshManager.h"
#include "Mesh/StaticMesh.h"
#include "Object/ObjectFactory.h"
#include "Platform/Paths.h"
#include "Runtime/Engine.h"
#include "Settings/EditorSettings.h"
#include "Slate/SlateApplication.h"
#include "Texture/Texture2D.h"
#include "Render/Shader/Shader.h"
#include "UI/Toolbar/ViewportToolbar.h"
#include "Viewport/Viewport.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>

namespace
{
	static uint32 GNextMaterialEditorInstanceId = 0;

	bool IsColorTextureSlot(const FString& SlotName)
	{
		return SlotName == "DiffuseTexture"
			|| SlotName == "ParticleTexture"
			|| SlotName == "EmissiveTexture"
			|| SlotName == "Custom0Texture"
			|| SlotName == "Custom1Texture";
	}

	bool SupportsStaticMeshPreview(UMaterial* Material)
	{
		if (!Material)
		{
			return false;
		}

		FShader* Shader = Material->GetShader();
		if (!Shader)
		{
			return false;
		}

		for (const FMaterialTextureBindingInfo& Binding : Shader->GetTextureBindings())
		{
			if (Binding.Name == "ParticleTexture")
			{
				return false;
			}
		}

		return true;
	}

	TArray<FMaterialTextureBindingInfo> GetEditableTextureBindings(UMaterial* Material)
	{
		TArray<FMaterialTextureBindingInfo> Bindings;
		if (!Material)
		{
			return Bindings;
		}

		if (FShader* Shader = Material->GetShader())
		{
			Bindings = Shader->GetTextureBindings();
		}

		if (!Bindings.empty())
		{
			return Bindings;
		}

		if (TMap<FString, UTexture2D*>* Textures = Material->GetTexture())
		{
			uint32 Index = 0;
			for (const auto& Pair : *Textures)
			{
				FMaterialTextureBindingInfo Info;
				Info.Name = Pair.first;
				Info.SlotIndex = Index++;
				Bindings.push_back(Info);
			}

			std::sort(Bindings.begin(), Bindings.end(),
				[](const FMaterialTextureBindingInfo& A, const FMaterialTextureBindingInfo& B)
				{
					return A.Name < B.Name;
				});
		}

		return Bindings;
	}

	bool AcceptTextureImageDrop(const FString& SlotName, UMaterial* Material, UStaticMeshComponent* PreviewMeshComponent)
	{
		if (!Material || !ImGui::BeginDragDropTarget())
		{
			return false;
		}

		bool bChanged = false;
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("ImageContentItem"))
		{
			const FContentItem* Item = static_cast<const FContentItem*>(Payload->Data);
			if (Item)
			{
				const FString TexturePath = FPaths::MakeProjectRelative(
					FPaths::ToUtf8(Item->Path.generic_wstring()));
				ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
				UTexture2D* NewTexture = UTexture2D::LoadFromFile(
					TexturePath,
					Device,
					IsColorTextureSlot(SlotName) ? ETextureColorSpace::SRGB : ETextureColorSpace::Linear);

				if (NewTexture)
				{
					Material->SetTextureParameter(SlotName, NewTexture);
					Material->RebuildCachedSRVs();
					if (PreviewMeshComponent)
					{
						PreviewMeshComponent->SetMaterial(0, Material);
					}
					bChanged = true;
				}
			}
		}

		ImGui::EndDragDropTarget();
		return bChanged;
	}
}

FMaterialEditorWidget::FMaterialEditorWidget()
	: InstanceId(GNextMaterialEditorInstanceId++)
{
	const FString Id = std::to_string(InstanceId);
	PreviewWorldHandle = FName("MaterialEditorPreview_" + Id);
	WindowIdSuffix = "###MaterialEditor_" + Id;
}

bool FMaterialEditorWidget::CanEdit(UObject* Object) const
{
	return Object && Object->IsA<UMaterial>();
}

bool FMaterialEditorWidget::IsEditingObject(UObject* Object) const
{
	if (FAssetEditorWidget::IsEditingObject(Object))
	{
		return true;
	}

	const UMaterial* CurrentMaterial = Cast<UMaterial>(EditedObject);
	const UMaterial* RequestedMaterial = Cast<UMaterial>(Object);
	if (!IsOpen() || !CurrentMaterial || !RequestedMaterial)
	{
		return false;
	}

	const FString& CurrentPath = CurrentMaterial->GetAssetPathFileName();
	return !CurrentPath.empty()
		&& CurrentPath != "None"
		&& CurrentPath == RequestedMaterial->GetAssetPathFileName();
}

void FMaterialEditorWidget::Open(UObject* Object)
{
	FAssetEditorWidget::Open(Object);
	if (!IsOpen())
	{
		return;
	}

	UMaterial* Material = Cast<UMaterial>(EditedObject);
	ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
	EditingMaterial = Material ? Material->CreateEditableCopy(Device) : nullptr;
	if (!EditingMaterial)
	{
		Close();
		return;
	}

	bSupportsStaticMeshPreview = SupportsStaticMeshPreview(EditingMaterial);
	if (!bSupportsStaticMeshPreview)
	{
		return;
	}

	FWorldContext& WorldContext = GEngine->CreateWorldContext(EWorldType::EditorPreview, PreviewWorldHandle);
	WorldContext.World->SetWorldType(EWorldType::EditorPreview);
	WorldContext.World->InitWorld();

	AActor* Actor = WorldContext.World->SpawnActor<AActor>();
	PreviewMeshComponent = Actor->AddComponent<UStaticMeshComponent>();
	if (UStaticMesh* SphereMesh = FMeshManager::LoadStaticMesh("Asset/Mesh/BasicShape/Sphere_StaticMesh.uasset", Device))
	{
		PreviewMeshComponent->SetStaticMesh(SphereMesh);
	}
	PreviewMeshComponent->SetMaterial(0, EditingMaterial);
	Actor->SetRootComponent(PreviewMeshComponent);
	Actor->SetActorLocation(FVector(0.0f, 0.0f, 0.0f));

	ADirectionalLightActor* LightActor = WorldContext.World->SpawnActor<ADirectionalLightActor>();
	LightActor->InitDefaultComponents();
	LightActor->SetActorRotation(FVector(0.0f, 45.0f, -45.0f));
	if (UDirectionalLightComponent* LightComp = LightActor->GetComponentByClass<UDirectionalLightComponent>())
	{
		LightComp->SetShadowBias(0.0f);
		LightComp->PushToScene();
	}

	ViewportClient.Initialize(Device, 640, 480);
	ViewportClient.SetPreviewWorld(WorldContext.World);
	ViewportClient.SetPreviewActor(Actor);
	ViewportClient.SetPreviewMeshComponent(PreviewMeshComponent);
	ViewportClient.ResetCameraToPreviewBounds();

	WorldContext.World->SetEditorPOVProvider(&ViewportClient);
	FSlateApplication::Get().RegisterViewport(&MaterialViewportWindow, &ViewportClient);
	bViewportRegistered = true;
}

void FMaterialEditorWidget::Close()
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

	if (bViewportRegistered)
	{
		FSlateApplication::Get().UnregisterViewport(&ViewportClient);
		bViewportRegistered = false;
	}
	ViewportClient.Release();
	PreviewMeshComponent = nullptr;
	bSupportsStaticMeshPreview = true;

	if (EditingMaterial)
	{
		GUObjectArray.DestroyObject(EditingMaterial);
		EditingMaterial = nullptr;
	}
}

void FMaterialEditorWidget::Tick(float DeltaTime)
{
	if (bSupportsStaticMeshPreview && ViewportClient.IsRenderable())
	{
		ViewportClient.Tick(DeltaTime);
	}
}

void FMaterialEditorWidget::CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const
{
	if (IsOpen() && bSupportsStaticMeshPreview)
	{
		OutClients.push_back(const_cast<FStaticMeshEditorViewportClient*>(&ViewportClient));
	}
}

void FMaterialEditorWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	if (!IsOpen() || !EditedObject)
	{
		return;
	}

	UMaterial* OriginalMaterial = Cast<UMaterial>(EditedObject);
	UMaterial* Material = EditingMaterial;

	bool bWindowOpen = true;
	FString VisibleTitle = "Material Editor";
	const FString AssetPath = OriginalMaterial ? OriginalMaterial->GetAssetPathFileName() : FString();
	if (!AssetPath.empty())
	{
		VisibleTitle += " - ";
		VisibleTitle += AssetPath;
	}
	if (IsDirty())
	{
		VisibleTitle += " *";
	}

	ImGui::SetNextWindowSize(ImVec2(1080.0f, 640.0f), ImGuiCond_Once);
	ImGuiWindowFlags WindowFlags = ImGuiWindowFlags_NoSavedSettings;
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

	if (ImGui::Button("Save"))
	{
		if (OriginalMaterial && Material)
		{
			Material->SetAssetPathFileName(OriginalMaterial->GetAssetPathFileName());
		}
		if (FMaterialManager::Get().SaveMaterial(Material))
		{
			if (!OriginalMaterial || OriginalMaterial->CopyEditableStateFrom(Material))
			{
				ClearDirty();
			}
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", AssetPath.empty() ? "Unsaved material" : AssetPath.c_str());
	ImGui::Separator();

	static float DetailsWidth = 360.0f;
	if (bSupportsStaticMeshPreview)
	{
		RenderPreviewViewport(DetailsWidth);
		ImGui::SameLine();
	}
	else
	{
		RenderPreviewUnavailable(DetailsWidth);
		ImGui::SameLine();
	}

	ImGui::BeginChild("MaterialDetails", ImVec2(DetailsWidth, 0), true);
	const bool bChanged = RenderDetailsPanel(Material);
	ImGui::EndChild();

	if (bChanged)
	{
		MarkDirty();
	}

	ImGui::End();

	if (!bWindowOpen)
	{
		Close();
	}
}

void FMaterialEditorWidget::RenderPreviewViewport(float DetailsWidth)
{
	ImGui::BeginGroup();
	{
		float AvailableWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		AvailableWidth = (std::max)(AvailableWidth, 220.0f);
		ImVec2 Size = ImVec2(AvailableWidth, ImGui::GetContentRegionAvail().y);

		ImVec2 ViewportPos = ImGui::GetCursorScreenPos();
		ViewportClient.SetViewportRect(ViewportPos.x, ViewportPos.y, Size.x, Size.y);

		FViewport* VP = ViewportClient.GetViewport();
		if (VP && Size.x > 0.0f && Size.y > 0.0f)
		{
			VP->RequestResize(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y));
			MaterialViewportWindow.SetRect({ViewportPos.x, ViewportPos.y, Size.x, Size.y});

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

			FViewportToolbarContext Context;
			Context.Renderer = &GEngine->GetRenderer();
			Context.Settings = &FEditorSettings::Get().MeshEditorViewportSettings;
			Context.RenderOptions = &ViewportClient.GetRenderOptions();
			Context.ToolbarLeft = ViewportPos.x;
			Context.ToolbarTop = ViewportPos.y;
			Context.ToolbarWidth = Size.x;
			Context.bReservePlayStopSpace = false;
			Context.bShowAddActor = false;
			Context.bShowGizmoControls = false;

			FViewportToolbar::Render(Context);
		}
	}
	ImGui::EndGroup();
}

void FMaterialEditorWidget::RenderPreviewUnavailable(float DetailsWidth)
{
	ImGui::BeginGroup();
	{
		float AvailableWidth = ImGui::GetContentRegionAvail().x - DetailsWidth - ImGui::GetStyle().ItemSpacing.x;
		AvailableWidth = (std::max)(AvailableWidth, 220.0f);
		ImVec2 Size = ImVec2(AvailableWidth, ImGui::GetContentRegionAvail().y);

		ImGui::BeginChild("MaterialPreviewUnavailable", Size, true);
		ImGui::TextDisabled("Preview unavailable for this shader.");
		ImGui::TextWrapped("Particle Sprite materials can be edited here, but they are rendered through the particle pipeline.");
		ImGui::EndChild();
	}
	ImGui::EndGroup();
}

bool FMaterialEditorWidget::RenderDetailsPanel(UMaterial* Material)
{
	if (!Material)
	{
		ImGui::TextDisabled("No material data.");
		return false;
	}

	bool bChanged = false;
	ImGui::TextUnformatted("Material Details");
	ImGui::Separator();

	const char* BlendModeItems[] =
	{
		MaterialBlendMode::ToString(EMaterialBlendMode::Opaque),
		MaterialBlendMode::ToString(EMaterialBlendMode::Translucent),
	};
	int BlendModeIndex = static_cast<int>(Material->GetBlendMode());
	if (BlendModeIndex < 0 || BlendModeIndex >= IM_ARRAYSIZE(BlendModeItems))
	{
		BlendModeIndex = static_cast<int>(EMaterialBlendMode::Opaque);
	}
	if (ImGui::Combo("Blend Mode", &BlendModeIndex, BlendModeItems, IM_ARRAYSIZE(BlendModeItems)))
	{
		Material->SetBlendMode(static_cast<EMaterialBlendMode>(BlendModeIndex));
		bChanged = true;
	}
	ImGui::Spacing();

	if (ImGui::CollapsingHeader("Shader Parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		bChanged |= RenderShaderParameters(Material);
	}

	if (ImGui::CollapsingHeader("Textures", ImGuiTreeNodeFlags_DefaultOpen))
	{
		bChanged |= RenderTextureSlots(Material);
	}

	return bChanged;
}

bool FMaterialEditorWidget::RenderShaderParameters(UMaterial* Material)
{
	bool bChanged = false;
	const auto Layout = Material->GetParameterInfo();
	if (Layout.empty())
	{
		ImGui::TextDisabled("No reflected shader parameters.");
		return false;
	}

	for (const auto& Pair : Layout)
	{
		const FString& ParamName = Pair.first;
		const FMaterialParameterInfo* Info = Pair.second;
		if (!Info)
		{
			continue;
		}

		ImGui::PushID(ParamName.c_str());
		ImGui::TextUnformatted(ParamName.c_str());

		switch (Info->Size)
		{
		case sizeof(float):
		{
			float Value = 0.0f;
			if (Material->GetScalarParameter(ParamName, Value)
				&& ImGui::DragFloat("##Scalar", &Value, 0.01f))
			{
				Material->SetScalarParameter(ParamName, Value);
				bChanged = true;
			}
			break;
		}
		case sizeof(float) * 3:
		{
			FVector Value;
			if (Material->GetVector3Parameter(ParamName, Value)
				&& ImGui::DragFloat3("##Vector3", &Value.X, 0.01f))
			{
				Material->SetVector3Parameter(ParamName, Value);
				bChanged = true;
			}
			break;
		}
		case sizeof(float) * 4:
		{
			FVector4 Value;
			if (Material->GetVector4Parameter(ParamName, Value)
				&& ImGui::DragFloat4("##Vector4", &Value.X, 0.01f))
			{
				Material->SetVector4Parameter(ParamName, Value);
				bChanged = true;
			}
			break;
		}
		case sizeof(float) * 16:
		{
			FMatrix Value;
			if (Material->GetMatrixParameter(ParamName, Value))
			{
				bool bMatrixChanged = false;
				bMatrixChanged |= ImGui::DragFloat4("Row 0", &Value.Data[0], 0.01f);
				bMatrixChanged |= ImGui::DragFloat4("Row 1", &Value.Data[4], 0.01f);
				bMatrixChanged |= ImGui::DragFloat4("Row 2", &Value.Data[8], 0.01f);
				bMatrixChanged |= ImGui::DragFloat4("Row 3", &Value.Data[12], 0.01f);
				if (bMatrixChanged)
				{
					Material->SetMatrixParameter(ParamName, Value);
					bChanged = true;
				}
			}
			break;
		}
		default:
			ImGui::TextDisabled("Unsupported parameter size: %u", Info->Size);
			break;
		}

		ImGui::Spacing();
		ImGui::PopID();
	}

	return bChanged;
}

bool FMaterialEditorWidget::RenderTextureSlots(UMaterial* Material)
{
	bool bChanged = false;
	const ImVec2 ThumbnailSize(72.0f, 72.0f);

	const TArray<FMaterialTextureBindingInfo> Bindings = GetEditableTextureBindings(Material);
	if (Bindings.empty())
	{
		ImGui::TextDisabled("This shader exposes no material textures.");
		return false;
	}

	for (const FMaterialTextureBindingInfo& Binding : Bindings)
	{
		const FString& SlotName = Binding.Name;
		UTexture2D* Texture = nullptr;
		Material->GetTextureParameter(SlotName, Texture);

		ImGui::PushID(SlotName.c_str());
		ImGui::TextUnformatted(SlotName.c_str());

		if (Texture && Texture->GetSRV())
		{
			ImGui::Image((ImTextureID)Texture->GetSRV(), ThumbnailSize);
			bChanged |= AcceptTextureImageDrop(SlotName, Material, PreviewMeshComponent);
		}
		else
		{
			ImGui::Button("Drop Image", ThumbnailSize);
			bChanged |= AcceptTextureImageDrop(SlotName, Material, PreviewMeshComponent);
		}

		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextDisabled("t%u", Binding.SlotIndex);
		const FString TexturePath = Texture ? Texture->GetSourcePath() : FString("None");
		ImGui::TextWrapped("%s", TexturePath.c_str());
		ImGui::TextDisabled("Image drag/drop target");
		ImGui::EndGroup();

		ImGui::Spacing();
		ImGui::PopID();
	}

	return bChanged;
}
