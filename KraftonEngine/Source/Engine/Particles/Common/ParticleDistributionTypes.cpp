/**
 * @file ParticleDistributionTypes.cpp
 * @brief 에디터 측 Distribution UObject 구현.
 *
 * BuildRaw(): 커브 데이터를 NUM_BAKED_SAMPLES 테이블로 굽고 FRawDistribution을 반환.
 * Serialize(): 에디터 데이터(Type, Min, Max, MinCurve, MaxCurve)를 저장/로드.
 */

#include "Particles/Common/ParticleDistributionTypes.h"
#include "Serialization/Archive.h"

// ─────────────────────────────────────────────────────────────────────────────
// UDistributionFloat
// ─────────────────────────────────────────────────────────────────────────────

FRawDistributionFloat UDistributionFloat::BuildRaw() const
{
    FRawDistributionFloat Raw;
    Raw.Type = Type;
    Raw.Min  = Min;
    Raw.Max  = Max;

    if (Type == EDistributionType::ConstantCurve || Type == EDistributionType::UniformCurve)
    {
        Raw.BakedMin.resize(NUM_BAKED_SAMPLES);
        Raw.BakedMax.resize(NUM_BAKED_SAMPLES);

        for (int32 i = 0; i < NUM_BAKED_SAMPLES; ++i)
        {
            const float T  = static_cast<float>(i) / static_cast<float>(NUM_BAKED_SAMPLES - 1);
            Raw.BakedMin[i] = MinCurve.Eval(T);
            Raw.BakedMax[i] = (Type == EDistributionType::UniformCurve) ? MaxCurve.Eval(T) : Raw.BakedMin[i];
        }
    }

    return Raw;
}

void UDistributionFloat::Serialize(FArchive& Ar)
{
    uint8 TypeInt = static_cast<uint8>(Type);
    Ar << TypeInt;
    if (Ar.IsLoading()) Type = static_cast<EDistributionType>(TypeInt);

    Ar << Min << Max;
    Ar << MinCurve << MaxCurve;
}

// ─────────────────────────────────────────────────────────────────────────────
// UDistributionVector
// ─────────────────────────────────────────────────────────────────────────────

FRawDistributionVector UDistributionVector::BuildRaw() const
{
    FRawDistributionVector Raw;
    Raw.Type      = Type;
    Raw.Min       = Min;
    Raw.Max       = Max;
    Raw.bLockAxes = bLockAxes;

    if (Type == EDistributionType::ConstantCurve || Type == EDistributionType::UniformCurve)
    {
        Raw.BakedMin.resize(NUM_BAKED_SAMPLES);
        Raw.BakedMax.resize(NUM_BAKED_SAMPLES);

        for (int32 i = 0; i < NUM_BAKED_SAMPLES; ++i)
        {
            const float T  = static_cast<float>(i) / static_cast<float>(NUM_BAKED_SAMPLES - 1);
            Raw.BakedMin[i] = MinCurve.Eval(T);
            Raw.BakedMax[i] = (Type == EDistributionType::UniformCurve) ? MaxCurve.Eval(T) : Raw.BakedMin[i];
        }
    }

    return Raw;
}

void UDistributionVector::Serialize(FArchive& Ar)
{
    uint8 TypeInt = static_cast<uint8>(Type);
    Ar << TypeInt;
    if (Ar.IsLoading()) Type = static_cast<EDistributionType>(TypeInt);

    Ar << Min << Max << bLockAxes;
    Ar << MinCurve << MaxCurve;
}

// ─────────────────────────────────────────────────────────────────────────────
// UDistributionLinearColor
// ─────────────────────────────────────────────────────────────────────────────

static FLinearColor LerpColorDist(const FLinearColor& A, const FLinearColor& B, float T)
{
    return FLinearColor(
        A.R + (B.R - A.R) * T,
        A.G + (B.G - A.G) * T,
        A.B + (B.B - A.B) * T,
        A.A + (B.A - A.A) * T
    );
}

FRawDistributionLinearColor UDistributionLinearColor::BuildRaw() const
{
    FRawDistributionLinearColor Raw;
    Raw.Type = Type;
    Raw.Min  = Min;
    Raw.Max  = Max;

    if (Type == EDistributionType::ConstantCurve || Type == EDistributionType::UniformCurve)
    {
        Raw.BakedMin.resize(NUM_BAKED_SAMPLES);
        Raw.BakedMax.resize(NUM_BAKED_SAMPLES);

        for (int32 i = 0; i < NUM_BAKED_SAMPLES; ++i)
        {
            const float T  = static_cast<float>(i) / static_cast<float>(NUM_BAKED_SAMPLES - 1);
            Raw.BakedMin[i] = MinCurve.Eval(T);
            Raw.BakedMax[i] = (Type == EDistributionType::UniformCurve) ? MaxCurve.Eval(T) : Raw.BakedMin[i];
        }
    }

    return Raw;
}

void UDistributionLinearColor::Serialize(FArchive& Ar)
{
    uint8 TypeInt = static_cast<uint8>(Type);
    Ar << TypeInt;
    if (Ar.IsLoading()) Type = static_cast<EDistributionType>(TypeInt);

    Ar << Min << Max;
    Ar << MinCurve << MaxCurve;
}
