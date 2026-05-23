/**
 * @file ParticleSystemComponent.h
 * @brief ParticleSystem Asset을 월드에서 재생하는 Runtime Component 정의.
 *
 * 포함 클래스:
 * - UParticleSystemComponent: ParticleSystem 재생 / 정지 / Tick / RenderData 관리 Component
 */

#pragma once

#include "ParticleEmitterInstance.h"

/** 월드에 배치되어 ParticleSystem을 재생하고 렌더 데이터를 관리하는 Component */
class UParticleSystemComponent : public UObject
{
  public:
    void InitializeComponent();          // Component 초기화
    void TickComponent(float DeltaTime); // Component 매 프레임 갱신
    void ActivateSystem();               // ParticleSystem 재생
    void DeactivateSystem();             // ParticleSystem 정지
    void ResetSystem();                  // ParticleSystem 리셋

    UParticleSystem *GetTemplate() const { return Template; }
    void             SetTemplate(UParticleSystem *InTemplate) { Template = InTemplate; }

    const TArray<FParticleEmitterInstance *> &GetEmitterInstances() const { return EmitterInstances; }
    const TArray<FDynamicEmitterDataBase *>  &GetEmitterRenderData() const { return EmitterRenderData; }
    const TArray<FParticleEventCollideData>  &GetCollisionEvents() const { return CollisionEvents; }

    bool IsActive() const { return bIsActive; }

    void SetParticleVisible(bool bInVisible) { bParticleVisible = bInVisible; }
    bool IsParticleVisible() const { return bParticleVisible; }

  private:
    TArray<FParticleEmitterInstance *> EmitterInstances;        // Runtime Emitter Instance 목록
    UParticleSystem                   *Template = nullptr;      // 재생할 ParticleSystem Asset
    TArray<FDynamicEmitterDataBase *>  EmitterRenderData;       // 렌더 패스 전달용 데이터
    TArray<FParticleEventCollideData>  CollisionEvents;         // 이번 프레임 Collision Event 목록
    bool                               bIsActive = false;       // 재생 상태
    bool                               bParticleVisible = true; // ShowFlag 표시 여부
};
