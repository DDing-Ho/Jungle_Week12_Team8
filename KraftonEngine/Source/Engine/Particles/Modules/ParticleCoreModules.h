/**
 * @file ParticleCoreModules.h
 * @brief 기본 Particle Module 정의.
 *
 * 포함 클래스:
 * - UParticleModuleRequired: Emitter 필수 설정
 * - UParticleModuleSpawn: Particle 생성량 / 생성 주기 제어
 * - UParticleModuleLifetime: Particle 수명 설정
 * - UParticleModuleLocation: Particle 초기 위치 설정
 * - UParticleModuleVelocity: Particle 초기 속도 설정
 * - UParticleModuleColor: Particle 색상 / 알파 설정
 * - UParticleModuleSize: Particle 크기 설정
 */

#pragma once

#include "ParticleModules.h"

/** Emitter 필수 렌더링 설정 모듈 */
class UParticleModuleRequired : public UParticleModule
{
  public:
    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Required; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }

    EParticleEmitterType GetEmitterType() const { return EmitterType; }
    void                 SetEmitterType(EParticleEmitterType InType) { EmitterType = InType; }

    UMaterialInterface *GetMaterial() const { return Material; }
    void                SetMaterial(UMaterialInterface *InMaterial) { Material = InMaterial; }

    EParticleSortMode GetSortMode() const { return SortMode; }
    void              SetSortMode(EParticleSortMode InSortMode) { SortMode = InSortMode; }

  private:
    EParticleEmitterType EmitterType = EParticleEmitterType::PET_Sprite; // 기본 Emitter 타입
    UMaterialInterface  *Material = nullptr;                         // Particle 렌더링 Material
    EParticleSortMode    SortMode = EParticleSortMode::PSM_None;         // Particle 정렬 방식
    int32                TranslucencySortPriority = 0;               // Translucent Pass 정렬 우선순위
};

/** Particle 생성량과 Burst를 제어하는 모듈 */
class UParticleModuleSpawn : public UParticleModule
{
  public:
    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Spawn; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }

    float GetSpawnRate() const { return SpawnRate; }
    void  SetSpawnRate(float InSpawnRate) { SpawnRate = InSpawnRate; }

  private:
    float SpawnRate = 10.0f; // 초당 Particle 생성 수
    int32 BurstCount = 0;    // 순간 Spawn 개수
};

/** Particle 수명을 설정하는 모듈 */
class UParticleModuleLifetime : public UParticleModule
{
  public:
    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Lifetime; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }

    float GetLifetime() const { return Lifetime; }
    void  SetLifetime(float InLifetime) { Lifetime = InLifetime; }

  private:
    float Lifetime = 1.0f; // Particle 전체 수명
};

/** Particle 초기 위치와 Spawn 영역을 설정하는 모듈 */
class UParticleModuleLocation : public UParticleModule
{
  public:
    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Location; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }

    FVector GetInitialLocation() const { return InitialLocation; }
    void    SetInitialLocation(const FVector &InLocation) { InitialLocation = InLocation; }

  private:
    FVector                 InitialLocation = FVector::ZeroVector; // Spawn 시 초기 위치 오프셋
    float                   SphereRadius = 0.0f;                   // Sphere Spawn 반경
    float                   CylinderRadius = 0.0f;                 // Cylinder Spawn 반경
    float                   CylinderHeight = 0.0f;                 // Cylinder Spawn 높이
    FParticleRandomSeedInfo RandomSeedInfo;                        // Seed 기반 위치 난수 설정
};

/** Particle 초기 속도를 설정하는 모듈 */
class UParticleModuleVelocity : public UParticleModule
{
  public:
    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Velocity; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }

    FVector GetInitialVelocity() const { return InitialVelocity; }
    void    SetInitialVelocity(const FVector &InVelocity) { InitialVelocity = InVelocity; }

  private:
    FVector                 InitialVelocity = FVector::ZeroVector; // Spawn 시 초기 속도
    FParticleRandomSeedInfo RandomSeedInfo;                        // Seed 기반 속도 난수 설정
};

/** Particle 색상과 Alpha 변화를 설정하는 모듈 */
class UParticleModuleColor : public UParticleModule
{
  public:
    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Color; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate; }

    FColor GetInitialColor() const { return InitialColor; }
    void   SetInitialColor(const FColor &InColor) { InitialColor = InColor; }

  private:
    FColor InitialColor = FColor::White(); // Spawn 시 초기 색상
    FColor FinalColor = FColor::White();   // 수명 종료 시 목표 색상
    bool   bUseColorOverLife = false;    // 수명 기반 색상 변화 사용 여부
};

/** Particle 크기와 수명 기반 크기 변화를 설정하는 모듈 */
class UParticleModuleSize : public UParticleModule
{
  public:
    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Size; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate; }

    FVector GetInitialSize() const { return InitialSize; }
    void    SetInitialSize(const FVector &InSize) { InitialSize = InSize; }

  private:
    FVector                 InitialSize = FVector(1.0f, 1.0f, 1.0f); // Spawn 시 초기 크기
    FVector                 FinalSize = FVector(1.0f, 1.0f, 1.0f);   // 수명 종료 시 목표 크기
    bool                    bUseSizeByLife = false;                  // 수명 기반 크기 변화 사용 여부
    FParticleRandomSeedInfo RandomSeedInfo;                          // Seed 기반 크기 난수 설정
};
