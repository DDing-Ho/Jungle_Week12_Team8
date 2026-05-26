#pragma once

#include "AssetEditorWidget.h"
#include "Engine/Object/FName.h"
#include "Editor/Slate/SWindow.h"
#include "Editor/Viewport/ParticleSystemEditorViewportClient.h"

struct ImVec2;
class UParticleSystem;
class UParticleEmitter;
class UParticleModule;

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
	void RenderPreviewViewport(const ImVec2& Size);
	bool RenderDetailsPanel();
	bool RenderEmittersPanel();
	bool RenderEmitterBlock(UParticleEmitter* Emitter, int32 EmitterIndex);
	bool RenderEmitterHeader(UParticleEmitter* Emitter, int32 EmitterIndex);
	bool RenderEmitterModules(UParticleEmitter* Emitter, int32 EmitterIndex);
	bool RenderParticleModuleItem(UParticleModule* Module, int32 EmitterIndex);
	bool RenderCurveEditorPanel();

private:
	SWindow ViewportWindow;
	FParticleSystemEditorViewportClient ViewportClient;

	uint32 InstanceId;
	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;

	float LeftPanelRatio = 0.4f;
	float LeftTopPanelRatio = 0.6f;
	float RightTopPanelRatio = 0.6f;

	int32 SelectedEmitterIndex = -1;
	UParticleModule* SelectedModule = nullptr;

	bool bPendingClose = false;
};
