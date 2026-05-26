/**
 * @file ParticleRandomTypes.h
 * @brief Particle Module에서 사용할 Seed 기반 난수 설정 정의.
 *
 * 포함 타입:
 * - FParticleRandomSeedInfo: Seed 기반 난수 재현 설정
 */

#pragma once

#include "ParticleTypes.h"
#include "ParticleDistribution.h"
#include "ParticleRandomTypes.generated.h"

/** Seed 기반 난수 재현 설정 */
USTRUCT()
struct FParticleRandomSeedInfo
{
    GENERATED_BODY(FParticleRandomSeedInfo)

    UPROPERTY(Edit, Category="Particle")
    bool  bUseSeed = false; // Seed 기반 난수 사용 여부
    UPROPERTY(Edit, Category="Particle", Speed=1.0)
    int32 Seed = 0;         // 난수 재현용 Seed 값
};
