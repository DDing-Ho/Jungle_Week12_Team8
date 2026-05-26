/**
 * @file ParticleDistributionTypes.cpp
 * @brief 에디터 측 Distribution UObject 구현.
 *
 * BuildRaw(): 커브 데이터를 NUM_BAKED_SAMPLES 테이블로 굽고 FRawDistribution을 반환.
 * Serialize(): 에디터 데이터(Type, Min, Max, MinCurve, MaxCurve)를 저장/로드.
 */

#include "Particles/Common/ParticleDistributionTypes.h"
#include "Serialization/Archive.h"
#include <cmath>

namespace
{
    constexpr float ParticleDistributionTypeTimeEpsilon = 1e-6f;

    static float NormalizeCurveTimeForBake(float T, const FParticleFloatCurve& Curve, bool bIsLooped, float LoopKeyOffset)
    {
        if (!bIsLooped || Curve.Keys.empty())
        {
            return T;
        }

        const float StartTime = Curve.Keys.front().Time;
        const float EndTime = Curve.Keys.back().Time;
        const float LoopDuration = (EndTime - StartTime) + LoopKeyOffset;
        if (LoopDuration <= ParticleDistributionTypeTimeEpsilon)
        {
            return EndTime;
        }

        float Offset = std::fmod(T - StartTime, LoopDuration);
        if (Offset < 0.0f)
        {
            Offset += LoopDuration;
        }

        const float LoopTime = StartTime + Offset;
        return LoopTime > EndTime ? EndTime : LoopTime;
    }

    static float EvalBakeCurve(const FParticleFloatCurve& Curve, float T, bool bIsLooped, float LoopKeyOffset)
    {
        return Curve.Eval(NormalizeCurveTimeForBake(T, Curve, bIsLooped, LoopKeyOffset));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UDistributionFloat
// ─────────────────────────────────────────────────────────────────────────────

FRawDistributionFloat UDistributionFloat::BuildRaw() const
{
    FRawDistributionFloat Raw;
    Raw.Type = Type;
    Raw.Min  = Min;
    Raw.Max  = Max;
    Raw.bIsLooped = bIsLooped;
    Raw.LoopKeyOffset = LoopKeyOffset;
    Raw.bUseExtremes = bUseExtremes;
    Raw.bCanBeBaked = bCanBeBaked;
    Raw.MinCurve = MinCurve;
    Raw.MaxCurve = MaxCurve;

    if (Type == EDistributionType::ConstantCurve || Type == EDistributionType::UniformCurve)
    {
        if (bCanBeBaked)
        {
            Raw.BakedMin.resize(NUM_BAKED_SAMPLES);
            Raw.BakedMax.resize(NUM_BAKED_SAMPLES);

            for (int32 i = 0; i < NUM_BAKED_SAMPLES; ++i)
            {
                const float T = static_cast<float>(i) / static_cast<float>(NUM_BAKED_SAMPLES - 1);
                Raw.BakedMin[i] = EvalBakeCurve(Raw.MinCurve, T, Raw.bIsLooped, Raw.LoopKeyOffset);
                Raw.BakedMax[i] = (Type == EDistributionType::UniformCurve)
                    ? EvalBakeCurve(Raw.MaxCurve, T, Raw.bIsLooped, Raw.LoopKeyOffset)
                    : Raw.BakedMin[i];
            }
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
    Ar << bIsLooped << LoopKeyOffset << bUseExtremes << bCanBeBaked;
    Ar << MinCurve << MaxCurve;
}

// ─────────────────────────────────────────────────────────────────────────────
// UDistributionVector
// ─────────────────────────────────────────────────────────────────────────────

FRawDistributionVector UDistributionVector::BuildRaw() const
{
    FRawDistributionVector Raw;
    Raw.Type = Type;
    Raw.Min = Min;
    Raw.Max = Max;
    Raw.bIsLooped = bIsLooped;
    Raw.LoopKeyOffset = LoopKeyOffset;
    Raw.bUseExtremes = bUseExtremes;
    Raw.bCanBeBaked = bCanBeBaked;
    Raw.LockedAxesMode = LockedAxesMode;
    Raw.MirrorFlags = MirrorFlags;
    Raw.MinCurve = MinCurve;
    Raw.MaxCurve = MaxCurve;

    if (Type == EDistributionType::ConstantCurve || Type == EDistributionType::UniformCurve)
    {
        if (bCanBeBaked)
        {
            Raw.BakedMin.resize(NUM_BAKED_SAMPLES);
            Raw.BakedMax.resize(NUM_BAKED_SAMPLES);

            for (int32 i = 0; i < NUM_BAKED_SAMPLES; ++i)
            {
                const float T = static_cast<float>(i) / static_cast<float>(NUM_BAKED_SAMPLES - 1);
                Raw.BakedMin[i] = FVector(
                    EvalBakeCurve(Raw.MinCurve.X, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                    EvalBakeCurve(Raw.MinCurve.Y, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                    EvalBakeCurve(Raw.MinCurve.Z, T, Raw.bIsLooped, Raw.LoopKeyOffset));
                Raw.BakedMax[i] = (Type == EDistributionType::UniformCurve)
                    ? FVector(
                        EvalBakeCurve(Raw.MaxCurve.X, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                        EvalBakeCurve(Raw.MaxCurve.Y, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                        EvalBakeCurve(Raw.MaxCurve.Z, T, Raw.bIsLooped, Raw.LoopKeyOffset))
                    : Raw.BakedMin[i];
            }
        }
    }

    return Raw;
}

void UDistributionVector::Serialize(FArchive& Ar)
{
    uint8 TypeInt = static_cast<uint8>(Type);
    Ar << TypeInt;
    if (Ar.IsLoading()) Type = static_cast<EDistributionType>(TypeInt);

    int32 LockedAxesModeInt = static_cast<int32>(LockedAxesMode);
    Ar << Min << Max;
    Ar << bIsLooped << LoopKeyOffset << bUseExtremes << bCanBeBaked;
    Ar << LockedAxesModeInt;
    if (Ar.IsLoading()) LockedAxesMode = static_cast<EParticleLockedAxesMode>(LockedAxesModeInt);
    Ar << MirrorFlags;
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
    Raw.bIsLooped = bIsLooped;
    Raw.LoopKeyOffset = LoopKeyOffset;
    Raw.bUseExtremes = bUseExtremes;
    Raw.bCanBeBaked = bCanBeBaked;
    Raw.MinCurve = MinCurve;
    Raw.MaxCurve = MaxCurve;

    if (Type == EDistributionType::ConstantCurve || Type == EDistributionType::UniformCurve)
    {
        if (bCanBeBaked)
        {
            Raw.BakedMin.resize(NUM_BAKED_SAMPLES);
            Raw.BakedMax.resize(NUM_BAKED_SAMPLES);

            for (int32 i = 0; i < NUM_BAKED_SAMPLES; ++i)
            {
                const float T = static_cast<float>(i) / static_cast<float>(NUM_BAKED_SAMPLES - 1);
                Raw.BakedMin[i] = FLinearColor(
                    EvalBakeCurve(Raw.MinCurve.R, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                    EvalBakeCurve(Raw.MinCurve.G, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                    EvalBakeCurve(Raw.MinCurve.B, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                    EvalBakeCurve(Raw.MinCurve.A, T, Raw.bIsLooped, Raw.LoopKeyOffset));
                Raw.BakedMax[i] = (Type == EDistributionType::UniformCurve)
                    ? FLinearColor(
                        EvalBakeCurve(Raw.MaxCurve.R, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                        EvalBakeCurve(Raw.MaxCurve.G, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                        EvalBakeCurve(Raw.MaxCurve.B, T, Raw.bIsLooped, Raw.LoopKeyOffset),
                        EvalBakeCurve(Raw.MaxCurve.A, T, Raw.bIsLooped, Raw.LoopKeyOffset))
                    : Raw.BakedMin[i];
            }
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
    Ar << bIsLooped << LoopKeyOffset << bUseExtremes << bCanBeBaked;
    Ar << MinCurve << MaxCurve;
}
