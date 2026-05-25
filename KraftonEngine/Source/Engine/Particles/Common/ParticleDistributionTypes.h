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

    EDistributionType Type    = EDistributionType::Constant;
    float             Min     = 0.f;
    float             Max     = 0.f;
    FFloatCurve       MinCurve;
    FFloatCurve       MaxCurve;
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

    EDistributionType Type      = EDistributionType::Constant;
    FVector           Min       = FVector::ZeroVector;
    FVector           Max       = FVector::ZeroVector;
    bool              bLockAxes = false; // x,y,z 세 축에 동일한 값을 적용
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

    EDistributionType  Type = EDistributionType::Constant;
    FLinearColor       Min  = FLinearColor::White();
    FLinearColor       Max  = FLinearColor::White();
    FLinearColorCurve  MinCurve;
    FLinearColorCurve  MaxCurve;
};
