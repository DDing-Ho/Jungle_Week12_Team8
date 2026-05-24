/**
 * @file ParticleMotionModules.h
 * @brief Particle 운동 / 회전 / 힘 관련 Module 정의.
 *
 * 포함 클래스:
 * - UParticleModuleRotation: Particle 초기 회전 설정
 * - UParticleModuleRotationRate: Particle 회전 속도 설정
 * - UParticleModuleAcceleration: 가속도 / Drag 적용
 * - UParticleModuleAttractor: 특정 위치 방향 흡인
 * - UParticleModuleOrbit: 렌더링 위치 궤도 오프셋 적용
 */

#pragma once

#include "ParticleCoreModules.h"
#include "ParticleMotionModules.generated.h"

/** Particle 초기 회전을 설정하는 모듈 */
UCLASS()
class UParticleModuleRotation : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleRotation)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Rotation; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Rotation; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    float                   InitialRotation = 0.0f; // Spawn 시 초기 회전값
    FRotator                InitialMeshRotation;    // Mesh Particle 초기 회전값
    FParticleRandomSeedInfo RandomSeedInfo;         // Seed 기반 회전 난수 설정
};

/** Particle 회전 속도를 설정하는 모듈 */
UCLASS()
class UParticleModuleRotationRate : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleRotationRate)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_RotationRate; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::RotationRate; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    float                   InitialRotationRate = 0.0f;  // Spawn 시 초기 회전 속도
    float                   RotationRateOverLife = 0.0f; // 수명 기반 회전 속도 변화
    FVector                 MeshRotationRate;            // Mesh Particle 회전 속도
    FParticleRandomSeedInfo RandomSeedInfo;              // Seed 기반 회전 속도 난수 설정
};

/** Particle 속도 변화와 Drag를 적용하는 모듈 */
UCLASS()
class UParticleModuleAcceleration : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleAcceleration)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Acceleration; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Acceleration; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    FVector Acceleration = FVector::ZeroVector;      // 매 프레임 적용할 가속도
    FVector ConstAcceleration = FVector::ZeroVector; // 고정 가속도
    float   Drag = 0.0f;                             // 속도 감쇠 계수
    bool    bUseAccelerationOverLife = false;        // 수명 기반 가속도 변화 사용 여부
};

/** 특정 위치나 Actor 방향으로 Particle을 끌어당기는 모듈 */
UCLASS()
class UParticleModuleAttractor : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleAttractor)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Attractor; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Attractor; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    FVector TargetLocation = FVector::ZeroVector; // 흡인 목표 위치
    float   Strength = 0.0f;                      // 흡인 강도
    float   Radius = 0.0f;                        // 흡인 영향 반경
};

/** Particle 렌더링 위치에 궤도 오프셋을 적용하는 모듈 */
UCLASS()
class UParticleModuleOrbit : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleOrbit)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Orbit; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Orbit; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    FVector Offset = FVector::ZeroVector;       // 렌더링 위치 오프셋
    FVector RotationRate = FVector::ZeroVector; // 궤도 회전 속도
};
