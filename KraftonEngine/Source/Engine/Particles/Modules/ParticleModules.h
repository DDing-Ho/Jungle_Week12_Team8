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

/** 모든 Particle Module의 공통 기반 클래스 */
class UParticleModule : public UObject
{
  public:
    virtual EParticleModuleType        GetModuleType() const { return EParticleModuleType::PMT_Custom; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const { return EParticleModuleUpdatePhase::PMUP_Update; }

    virtual void InitializeModule(UParticleEmitter *InEmitter) {}
    virtual void PreSpawn(FParticleEmitterInstance *Owner, FBaseParticle &Particle) {}
    virtual void Spawn(FParticleEmitterInstance *Owner, FBaseParticle &Particle, float SpawnTime) {}
    virtual void Update(FParticleEmitterInstance *Owner, float DeltaTime) {}

    bool IsEnabled() const { return bEnabled; }
    void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

  protected:
    bool bEnabled = true; // Module 활성 여부
};
