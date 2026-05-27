#pragma once

#include "AssetEditorWidget.h"
#include "Engine/Object/FName.h"
#include "Editor/Slate/SWindow.h"
#include "Editor/Viewport/ParticleSystemEditorViewportClient.h"

struct ImVec2;
class UParticleSystem;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleModule;
class UParticleSystemComponent;
class UObject;
class FProperty;

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
	void RenderPreviewStatsOverlay(const ImVec2& ViewportPos, const ImVec2& ViewportSize) const;
	UParticleSystemComponent* GetPreviewParticleComponent() const;
	void ReplayPreviewParticleSystem();
	bool RenderDetailsPanel();
	bool RenderEditableProperties(UObject* Object);
	bool RenderParticleProperty(const FProperty& Prop, void* Container);
	bool RenderParticleDistribution(UParticleModule* Module);
	void HandleEditedParticleProperty(UObject* Object, const FProperty* Prop);
	bool RenderEmittersPanel();
	bool RenderEmitterBlock(UParticleEmitter* Emitter, int32 EmitterIndex);
	bool RenderEmitterHeader(UParticleEmitter* Emitter, int32 EmitterIndex);
	bool RenderEmitterModules(UParticleEmitter* Emitter, int32 EmitterIndex);
	bool RenderParticleModuleItem(UParticleModule* Module, int32 EmitterIndex);
	bool RenderCurveEditorPanel();
	bool RemoveSelectedModule();
	int32 GetEditedLODIndex(const UParticleSystem* Asset) const;
	UParticleLODLevel* GetEditedLODLevel(UParticleEmitter* Emitter) const;
	const UParticleLODLevel* GetEditedLODLevel(const UParticleEmitter* Emitter) const;
	void SyncEditedLODSelection(bool bRefreshPreview = true);
	bool InsertLODRelativeToCurrent(bool bInsertAfter);

private:
	enum class ECurveTangentHandle : uint8
	{
		None,
		Arrive,
		Leave,
	};

	SWindow ViewportWindow;
	FParticleSystemEditorViewportClient ViewportClient;

	uint32 InstanceId;
	FName PreviewWorldHandle = FName::None;
	FString WindowIdSuffix;

	float LeftPanelRatio = 0.4f;
	float LeftTopPanelRatio = 0.6f;
	float RightTopPanelRatio = 0.4f;

	int32 SelectedEmitterIndex = -1;
	int32 EditedLODIndex = 0;
	UParticleModule* SelectedModule = nullptr;

	// Curve editor panel state
	float            CurveViewMinT     = 0.0f;
	float            CurveViewMaxT     = 1.0f;
	float            CurveViewMinV     = -10.0f;
	float            CurveViewMaxV     = 10.0f;
	int32            CurveSelectedKey  = -1;
	int32            CurveSelCurve     = 0;   // 0 = MinCurve, 1 = MaxCurve
	bool             bCurveDragKey     = false;
	ECurveTangentHandle CurveDraggingTangentHandle = ECurveTangentHandle::None;
	bool             bCurvePan         = false;
	bool             bCurveCtxSuppress = false;
	float            CurveCtxTime      = 0.0f;
	float            CurveCtxValue     = 0.0f;
	int32            CurveChannelIdx   = 0;
	UParticleModule* CurvePrevModule   = nullptr;

	bool bPendingClose = false;

	// Viewport playback controls
	bool  bAnimPaused       = false;
	bool  bAnimRealtime     = true;
	bool  bAnimLoop         = true;
	float AnimSpeedScale    = 1.0f;
	bool  bWasActive        = false;
	bool  bShowPreviewStats = false;
};
