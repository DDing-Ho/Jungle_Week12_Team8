/**
 * @file ParticleKillModules.h
 * @brief 조건 기반 Particle 제거 Module 정의.
 *
 * 포함 클래스:
 * - UParticleModuleKill: 조건 기반 Particle 제거
 */

#pragma once

#include "ParticleModules.h"
#include "ParticleKillModules.generated.h"

struct FBoundingBox;

/** 조건 기반 Particle 제거를 처리하는 모듈 */
UCLASS()
class UParticleModuleKill : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleKill)

    virtual EParticleModuleType        GetModuleType()  const override { return EParticleModuleType::PMT_Kill; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Kill; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void Update(FParticleEmitterInstance* Owner, float DeltaTime, TArray<FParticleEventData>* /*OutEventQueue*/ = nullptr) override;

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Use Kill Box")
    bool  bUseKillBox = false;    // Box 기준 제거 사용 여부
    UPROPERTY(Edit, Category="Particle", DisplayName="Use Kill Height")
    bool  bUseKillHeight = false; // Height 기준 제거 사용 여부
    FBoundingBox  KillBox;        // Particle 제거 기준 Box (KillBox 내부 진입 시 제거)
    UPROPERTY(Edit, Category="Particle", DisplayName="Kill Height", Speed=0.1)
    float KillHeight = 0.0f;      // 이 높이 이하로 내려간 Particle 제거
};
