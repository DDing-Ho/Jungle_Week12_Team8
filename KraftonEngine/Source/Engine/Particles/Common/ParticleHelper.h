/**
 * @file ParticleHelper.h
 * @brief ParticleData 접근 / 순회 보조 매크로 정의.
 *
 * 포함 매크로:
 * - DECLARE_PARTICLE_PTR: ParticleData에서 현재 Particle 포인터 선언
 * - BEGIN_UPDATE_LOOP: 활성 Particle 순회 시작
 * - END_UPDATE_LOOP: 활성 Particle 순회 종료
 *
 * 포함 예시:
 * - UParticleModuleSizeScaleBySpeed: 속도 기반 Size 갱신 예시 Module
 */

#pragma once

#include "../Runtime/ParticleEmitterInstance.h"
#include "ParticleHelper.generated.h"

#define DECLARE_PARTICLE_PTR(Index) FBaseParticle &Particle = *reinterpret_cast<FBaseParticle *>(ParticleData + ParticleStride * ParticleIndices[Index])

#define BEGIN_UPDATE_LOOP                                                                                                                                                                              \
    for (int32 ParticleIndex = 0; ParticleIndex < Owner->ActiveParticles; ++ParticleIndex)                                                                                                             \
    {                                                                                                                                                                                                  \
        DECLARE_PARTICLE_PTR(ParticleIndex);

#define END_UPDATE_LOOP }

/** 속도 기반 Size Scale 예시 모듈 */
UCLASS()
class UParticleModuleSizeScaleBySpeed : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleSizeScaleBySpeed)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Size; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }

    virtual void Update(FParticleEmitterInstance *Owner, float DeltaTime) override;

  private:
    FVector SpeedScale = FVector(1.0f, 1.0f, 1.0f); // 속도 기반 크기 배율
    FVector MaxScale = FVector(10.0f, 10.0f, 1.0f); // 최대 크기 배율
};
