#pragma once
#include "Editor/UI/EditorWidget.h"
#include "ContentItem.h"
#include <d3d11.h>
#include <memory>
#include "ContentBrowserContext.h"
#include "ContentBrowserElement.h"
#include "Editor/Subsystem/AssetFactory.h"
#include "EditorFbxImportDialog.h"
#include "Mesh/MeshManager.h"

class FEditorContentBrowserWidget final : public FEditorWidget
{
	struct FDirNode
	{
		FContentItem Self;
		TArray<FDirNode> Children;
	};

public:
	void Initialize(UEditorEngine* InEditor, ID3D11Device* InDevice);
	void Render(float DeltaTime) override;
	void Refresh();
	void SaveToSettings() const;
	void SetIconSize(float Size);
	float GetIconSize() const { return BrowserContext.ContentSize.x; }

private:
	void LoadFromSettings();
	void RefreshContent();
	void DrawDirNode(const FDirNode& InNode);
	void DrawContents();
	void BeginImportSourceFile();
	void BeginFbxImport(const FString& SourcePath);
	void RenderFbxImportPopup();
	void BeginMaterialCreate(EMaterialCreatePreset Preset, const FString& DefaultName);
	void RenderMaterialCreatePopup();
	bool ExecuteMaterialCreate();
	void BeginRenameAsset(const FContentItem& Item);
	void RenderRenamePopup();
	bool ExecuteRenameAsset();
	void BeginDeleteAsset(const FContentItem& Item);
	void RenderDeletePopup();
	bool ExecuteDeleteAsset();
	bool ExecuteObjImport(const FString& SourcePath);
	void RefreshImportedAssetLists();

	TArray<FContentItem> ReadDirectory(std::wstring Path);
	FDirNode BuildDirectoryTree(const std::filesystem::path& DirPath);
	TArray<FMeshAssetListItem> ScanSkeletonAssets() const;

private:
	ContentBrowserContext BrowserContext;

	FDirNode RootNode;
	TArray<std::shared_ptr<ContentBrowserElement>> CachedBrowserElements;
	TMap<FString, std::wstring> IconFileMap;

	FEditorFbxImportDialog FbxImportDialog;

	bool bOpenMaterialCreatePopup = false;
	EMaterialCreatePreset PendingMaterialPreset = EMaterialCreatePreset::UberLit;
	char MaterialCreateName[128] = {};
	FString MaterialCreateError;

	bool bOpenRenamePopup = false;
	std::filesystem::path RenameSourcePath;
	char RenameAssetName[128] = {};
	FString RenameError;

	bool bOpenDeletePopup = false;
	std::filesystem::path DeleteSourcePath;
	FString DeleteAssetName;
	FString DeleteError;
};
