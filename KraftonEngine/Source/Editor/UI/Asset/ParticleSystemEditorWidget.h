#pragma once

#include "AssetEditorWidget.h"
#include "Engine/Object/FName.h"
#include "Editor/Slate/SWindow.h"
#include "Editor/Viewport/ParticleSystemEditorViewportClient.h"

class UParticleSystem;

class FParticleSystemEditorWidget : public FAssetEditorWidget
{
public:
	FParticleSystemEditorWidget();

	bool CanEdit(UObject* Object) const override;

	void Open(UObject* Object) override;
	void Close() override;
	void Tick(float DeltaTime) override;

	void CollectPreviewViewports(TArray<IEditorPreviewViewportClient*>& OutClients) const override;

	bool AllowsMultipleInstances() const override { return true; }

	void Render(float DeltaTime) override;

private:
	void RenderPreviewViewport();
	bool RenderDetailsPanel();

private:
	SWindow ViewportWindow;
	FParticleSystemEditorViewportClient ViewportClient;

	uint32 InstanceId;
	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;

	bool bPendingClose = false;
};