/**
 * @file ParticleSystemComponent.h
 * @brief ParticleSystem Asset을 월드에서 재생하는 Runtime Component 정의.
 *
 * 포함 클래스:
 * - UParticleSystemComponent: ParticleSystem 재생 / 정지 / Tick / RenderData 관리 Component
 */

#pragma once
#include "Particles/Assets/ParticleAsset.h"
#include "Particles/Runtime/ParticleRuntimeTypes.h"
#include "Core/CoreTypes.h"
#include "Core/UObject/TSoftObjectPtr.h"
#include "Component/PrimitiveComponent.h"
#include "ParticleSystemComponent.generated.h"

struct FParticleEmitterInstance;
struct FDynamicEmitterDataBase;

/** 월드에 배치되어 ParticleSystem을 재생하고 렌더 데이터를 관리하는 Component */
UCLASS()
class UParticleSystemComponent : public UPrimitiveComponent
{
  public:
	GENERATED_BODY(UParticleSystemComponent)

    UParticleSystemComponent(); // Component 초기화
	void EndPlay() override;
	void Activate() override;
	void Deactivate() override;
    void ActivateSystem();               // ParticleSystem 재생
    void DeactivateSystem();             // ParticleSystem 정지
    void ResetSystem();                  // ParticleSystem 리셋

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;// Component 매 프레임 갱신
	FPrimitiveSceneProxy* CreateSceneProxy() override;

    UParticleSystem *GetTemplate() const { return Template; }
    void             SetTemplate(UParticleSystem *InTemplate);

    const TArray<FParticleEmitterInstance *> &GetEmitterInstances() const { return EmitterInstances; }
    const TArray<FDynamicEmitterDataBase *>  &GetEmitterRenderData() const { return EmitterRenderData; }
    const TArray<FParticleEventData>         &GetFrameEventQueue() const { return FrameEventQueue; }

    bool IsActive() const { return bIsActive; }
    bool IsEventTraceEnabled() const
    {
#if defined(_DEBUG)
        return bDebugEventTrace;
#else
        return false;
#endif
    }

    void SetParticleVisible(bool bInVisible) { bParticleVisible = bInVisible; }
    bool IsParticleVisible() const { return bParticleVisible; }

	void SetLODLevel(int32 Level);
	int32 GetLODLevel() {return CurrentLODLevelIndex;}

	void BuildRenderData();
	void ClearRenderData();

	void PostEditProperty(const char* PropertyName) override;
	void PostDuplicate() override;
private:
	void CreateEmitterInstances(); //Emitter정보를 가지고 Instance를 제작함
	void ClearEmitterInstances();

  private:
    TArray<FParticleEmitterInstance *>	EmitterInstances;        // Runtime Emitter Instance 목록
    TArray<FParticleEventData>			FrameEventQueue;         // 이번 프레임 Event 목록
	
	UPROPERTY(Edit, Category = "ParticleSystemComponent", DisplayName = "Template", Type = SoftObject, Class = UParticleSystem)
	TSoftObjectPtr<UParticleSystem> TemplateAsset;

	UParticleSystem					   *Template = nullptr;      // 재생할 ParticleSystem Asset
    TArray<FDynamicEmitterDataBase *>	EmitterRenderData;       // 렌더 패스 전달용 데이터

	TArray<UMaterial*>					EmitterMaterials;

	UPROPERTY(Edit, Category = "ParticleSystemComponent", DisplayName = "EmitterDelay")
	float EmitterDelay;				// Emitter시작 지연 시간

	UPROPERTY(Edit, Category = "ParticleSystemComponent", DisplayName = "Debug Event Trace")
	bool bDebugEventTrace = false; // Event Trace 로그 출력 여부

	bool bIsActive = false;			// 재생 상태
	bool bParticleVisible = true;	// ShowFlag 표시 여부
	bool bResetTriggered = false;	// 리셋 진행 여부

	float DeltaTimeTick;			 // 이번틱에 들어온 Delta 임시 저장
	float CustomTimeDilation;		 //파티클 속도 scale값

	int32 CurrentLODLevelIndex;		 // 현재 LODLevel
	int32 TotalActiveParticles;		 // 액티브된 모든 파티클 개수
};
