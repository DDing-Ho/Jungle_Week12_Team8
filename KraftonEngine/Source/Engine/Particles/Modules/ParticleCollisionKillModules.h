/**
 * @file ParticleCollisionKillModules.h
 * @brief Particle Collision / Kill Module 정의.
 *
 * 포함 클래스:
 * - UParticleModuleCollision: Particle 충돌 처리
 * - UParticleModuleKill: 조건 기반 Particle 제거
 */

#pragma once

#include "ParticleMotionModules.h"
#include "ParticleCollisionKillModules.generated.h"

struct FBoundingBox;
/** Particle과 World 또는 Actor의 충돌을 처리하는 모듈 */
UCLASS()
class UParticleModuleCollision : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleCollision)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Collision; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Collision; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    bool  bEnableCollision = true;  // Collision 사용 여부
    bool  bKillOnCollision = false; // 충돌 시 Particle 제거 여부
    float Bounce = 0.3f;            // 충돌 반사 계수
    float Friction = 0.1f;          // 충돌 마찰 계수
};

/** 조건 기반 Particle 제거를 처리하는 모듈 */
UCLASS()
class UParticleModuleKill : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleKill)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Kill; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Kill; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    bool  bUseKillBox = false;    // Box 기준 제거 사용 여부
    bool  bUseKillHeight = false; // Height 기준 제거 사용 여부
    FBoundingBox  KillBox;                // Particle 제거 기준 Box
    float KillHeight = 0.0f;      // Particle 제거 기준 높이
};
