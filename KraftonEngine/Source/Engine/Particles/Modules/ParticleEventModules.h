/**
 * @file ParticleEventModules.h
 * @brief Particle Event 생성 / 수신 Module 정의.
 *
 * 포함 클래스:
 * - UParticleModuleEventGenerator: Spawn / Death / Collision Event 생성
 * - UParticleModuleEventReceiverSpawn: Event 수신 시 Particle 생성
 * - UParticleModuleEventReceiverKillAll: Event 수신 시 전체 Particle 제거
 */

#pragma once
#include "ParticleCollisionKillModules.h"
#include "ParticleEventModules.generated.h"

struct FParticleEventData;
struct FParticleEmitterInstance;

/** Generator가 발행하는 단일 Event 선언 */
struct FParticleEventGeneratorEntry
{
    EParticleEventType Type = EParticleEventType::PEET_Custom;
    FName              EventName;
};

inline FArchive& operator<<(FArchive& Ar, FParticleEventGeneratorEntry& Entry)
{
    int32 EventTypeInt = static_cast<int32>(Entry.Type);
    Ar << EventTypeInt;
    if (Ar.IsLoading())
    {
        Entry.Type = static_cast<EParticleEventType>(EventTypeInt);
    }

    Ar << Entry.EventName;
    return Ar;
}

/** Event Receiver 모듈의 공통 기반 클래스 */
class UParticleModuleEventReceiverBase : public UParticleModule
{
  public:
    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Event; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual void Serialize(FArchive& Ar) override;

    bool MatchesEvent(const FParticleEventData& Event) const;
    virtual void ProcessEvent(FParticleEmitterInstance& OwnerEmitter, const FParticleEventData& Event) {}

  protected:
    UPROPERTY(Edit, Category="Particle", DisplayName="Listen Event Type")
    EParticleEventType ListenEventType = EParticleEventType::PEET_Collision;
    UPROPERTY(Edit, Category="Particle", DisplayName="Listen Event Name")
    FName              ListenEventName;
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

    const TArray<FParticleEventGeneratorEntry>& GetGeneratedEvents() const { return GeneratedEvents; }

  private:
    TArray<FParticleEventGeneratorEntry> GeneratedEvents; // 생성할 Event 선언 목록
};

/** Event 수신 시 Particle을 생성하는 모듈 */
UCLASS()
class UParticleModuleEventReceiverSpawn : public UParticleModuleEventReceiverBase
{
  public:
    GENERATED_BODY(UParticleModuleEventReceiverSpawn)

    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::EventReceiverSpawn; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void ProcessEvent(FParticleEmitterInstance& OwnerEmitter, const FParticleEventData& Event) override;

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Spawn Count", Min=0, Max=100000, Speed=1.0)
    int32 SpawnCount = 1; // Event 수신 시 생성할 Particle 수
};

/** Event 수신 시 Emitter의 모든 Particle을 제거하는 모듈 */
UCLASS()
class UParticleModuleEventReceiverKillAll : public UParticleModuleEventReceiverBase
{
  public:
    GENERATED_BODY(UParticleModuleEventReceiverKillAll)

    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::EventReceiverKillAll; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void ProcessEvent(FParticleEmitterInstance& OwnerEmitter, const FParticleEventData& Event) override;
};
