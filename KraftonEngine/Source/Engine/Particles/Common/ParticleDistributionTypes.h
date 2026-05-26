/**
 * @file ParticleDistributionTypes.h
 * @brief 에디터 측 Distribution UObject 정의.
 *
 * 에디터에서 커브 및 범위를 편집하고, BuildRaw()로 런타임 FRawDistribution을 생성한다.
 * 런타임에는 FRawDistribution* 만 사용하며 UDistribution* 은 필요 없다.
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

    /** 현재 설정으로 런타임 FRawDistributionFloat를 생성한다. */
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
    bool              bCanBeBaked = true;
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
    bool              bCanBeBaked = true;
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
    bool               bCanBeBaked = true;
    FLinearColorCurve  MinCurve;
    FLinearColorCurve  MaxCurve;
};
