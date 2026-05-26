/**
 * @file ParticleModules.h
 * @brief Particle Module 공통 기반 클래스 정의.
 *
 * 포함 클래스:
 * - UParticleModule: 모든 Particle Module의 base class
 */

#pragma once

#include "Object/Object.h"
#include "../Common/ParticleRandomTypes.h"
#include "ParticleModules.generated.h"

struct FParticleEventData;
struct FBaseParticle;
struct FParticleEmitterInstance;

/** 모든 Particle Module의 공통 기반 클래스 */
UCLASS()
class UParticleModule : public UObject
{
  public:
    GENERATED_BODY(UParticleModule)

    virtual EParticleModuleType        GetModuleType() const { return EParticleModuleType::PMT_Custom; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass       GetModuleClass() const { return EParticleModuleClass::Unknown; }

    virtual void InitializeModule(UParticleEmitter *InEmitter) {}
    virtual void PreSpawn(FParticleEmitterInstance *Owner, FBaseParticle &Particle) {}
    virtual void Spawn(FParticleEmitterInstance *Owner, FBaseParticle &Particle, float SpawnTime) {}
    virtual void Update(FParticleEmitterInstance *Owner, float DeltaTime, TArray<FParticleEventData>* /*OutEventQueue*/ = nullptr) {}
    virtual void Serialize(FArchive& Ar) override;
	//나중에 추가적 payload를 가진 module이 있으면 사용해야함
	virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) const { return 0; }

    /** UDistribution 에디터 데이터로부터 FRawDistribution 런타임 캐시를 재구성한다. */
    virtual void CacheModuleValues() {}

    /** RandomSeedInfo를 바탕으로 ModuleStream을 초기화한다. Init 시 한 번 호출. */
    void InitializeStream();

    bool IsEnabled() const { return bEnabled; }
    void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

    UPROPERTY(Edit, Category="Particle", DisplayName="Random Seed Info")
    FParticleRandomSeedInfo RandomSeedInfo; // 에디터에서 설정하는 Seed 정보

  protected:
    UPROPERTY(Edit, Category="Particle", DisplayName="Enabled")
    bool          bEnabled    = true; // Module 활성 여부
    FRandomStream ModuleStream;       // 이 모듈 전용 랜덤 스트림 (런타임, 직렬화 안 함)
};
