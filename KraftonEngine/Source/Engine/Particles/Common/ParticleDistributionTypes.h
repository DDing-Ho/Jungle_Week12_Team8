/**
 * @file ParticleDistributionTypes.h
 * @brief 에디터 측 Distribution UObject 정의.
 *
 * 에디터에서 커브 및 범위를 편집하고, BuildRaw()로 런타임 평가용 값을 만든다.
 * UDistribution* 는 편집/직렬화용이고, 실제 파티클 실행은 FRawDistribution* 로 평가한다.
 *
 * 포함 클래스:
 * - UDistributionFloat:       float 분포 에디터 오브젝트
 * - UDistributionVector:      FVector 분포 에디터 오브젝트
 * - UDistributionLinearColor: FLinearColor 분포 에디터 오브젝트
 */

#pragma once

#include "Object/Object.h"
#include "Particles/Common/ParticleDistribution.h"
#include "ParticleDistributionTypes.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// UDistributionFloat
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UDistributionFloat : public UObject
{
  public:
    GENERATED_BODY(UDistributionFloat)

    virtual void Serialize(FArchive& Ar) override;

    /** 현재 편집 설정을 런타임 평가용 값 타입으로 복사한다. */
    FRawDistributionFloat BuildRaw() const;

    UPROPERTY(Edit, Category="Particle")
    EDistributionType Type    = EDistributionType::Constant;
    UPROPERTY(Edit, Category="Particle")
    float             Min     = 0.f;
    UPROPERTY(Edit, Category="Particle")
    float             Max     = 0.f;
    bool              bIsLooped = false;
    float             LoopKeyOffset = 0.f;
    bool              bUseExtremes = false;
    FParticleFloatCurve       MinCurve;
    FParticleFloatCurve       MaxCurve;
};

// ─────────────────────────────────────────────────────────────────────────────
// UDistributionVector
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UDistributionVector : public UObject
{
  public:
    GENERATED_BODY(UDistributionVector)

    virtual void Serialize(FArchive& Ar) override;

    FRawDistributionVector BuildRaw() const;

    UPROPERTY(Edit, Category="Particle")
    EDistributionType Type      = EDistributionType::Constant;
    UPROPERTY(Edit, Category="Particle")
    FVector           Min       = FVector::ZeroVector;
    UPROPERTY(Edit, Category="Particle")
    FVector           Max       = FVector::ZeroVector;
    bool              bIsLooped = false;
    float             LoopKeyOffset = 0.f;
    bool              bUseExtremes = false;
    EParticleLockedAxesMode LockedAxesMode = EParticleLockedAxesMode::None;
    uint8             MirrorFlags = PMF_None;
    FVectorCurve      MinCurve;
    FVectorCurve      MaxCurve;
};

// ─────────────────────────────────────────────────────────────────────────────
// UDistributionLinearColor
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UDistributionLinearColor : public UObject
{
  public:
    GENERATED_BODY(UDistributionLinearColor)

    virtual void Serialize(FArchive& Ar) override;

    FRawDistributionLinearColor BuildRaw() const;

    UPROPERTY(Edit, Category="Particle")
    EDistributionType  Type = EDistributionType::Constant;
    FLinearColor       Min  = FLinearColor::White();
    FLinearColor       Max  = FLinearColor::White();
    bool               bIsLooped = false;
    float              LoopKeyOffset = 0.f;
    bool               bUseExtremes = false;
    FLinearColorCurve  MinCurve;
    FLinearColorCurve  MaxCurve;
};
