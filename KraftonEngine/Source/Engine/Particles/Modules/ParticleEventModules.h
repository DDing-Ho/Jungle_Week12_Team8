/**
 * @file ParticleEventModules.h
 * @brief Particle Event 생성 / 수신 Module 정의.
 *
 * 포함 타입:
 * - FParticleEventData: Particle Event 데이터
 *
 * 포함 클래스:
 * - UParticleModuleEventGenerator: Spawn / Death / Collision Event 생성
 * - UParticleModuleEventReceiverSpawn: Event 수신 시 Particle 생성
 * - UParticleModuleEventReceiverKillAll: Event 수신 시 전체 Particle 제거
 */

#pragma once
#include "ParticleCollisionKillModules.h"
#include "ParticleEventModules.generated.h"

constexpr int32 INDEX_NONE = -1;
/** Particle Event Payload 데이터 */
struct FParticleEventData
{
    EParticleEventType Type = EParticleEventType::PEET_Custom; // Event 종류
    FVector            Location = FVector::ZeroVector;    // Event 발생 위치
    FVector            Velocity = FVector::ZeroVector;    // Event 발생 시 속도
    int32              SourceEmitterIndex = INDEX_NONE;   // Event를 발생시킨 Emitter 인덱스
    int32              SourceParticleIndex = INDEX_NONE;  // Event를 발생시킨 Particle 인덱스
};

/** Spawn, Death, Collision 같은 Particle Event를 생성하는 모듈 */
UCLASS()
class UParticleModuleEventGenerator : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleEventGenerator)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Event; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::EventGenerator; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    TArray<EParticleEventType> GeneratedEvents; // 생성할 Event 종류 목록
};

/** Event 수신 시 Particle을 생성하는 모듈 */
UCLASS()
class UParticleModuleEventReceiverSpawn : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleEventReceiverSpawn)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Event; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::EventReceiverSpawn; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    EParticleEventType ListenEventType = EParticleEventType::PEET_Collision; // 수신할 Event 종류
    int32              SpawnCount = 1;                                  // Event 수신 시 생성할 Particle 수
};

/** Event 수신 시 Emitter의 모든 Particle을 제거하는 모듈 */
UCLASS()
class UParticleModuleEventReceiverKillAll : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleEventReceiverKillAll)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Event; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::EventReceiverKillAll; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    EParticleEventType ListenEventType = EParticleEventType::PEET_Collision; // 수신할 Event 종류
};
