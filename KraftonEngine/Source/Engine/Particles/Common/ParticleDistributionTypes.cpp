/**
 * @file ParticleDistributionTypes.cpp
 * @brief 에디터 측 Distribution UObject 구현.
 *
 * BuildRaw(): 현재 편집 설정을 복사해 런타임 평가용 값을 반환.
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
    Raw.bIsLooped = bIsLooped;
    Raw.LoopKeyOffset = LoopKeyOffset;
    Raw.bUseExtremes = bUseExtremes;
    Raw.MinCurve = MinCurve;
    Raw.MaxCurve = MaxCurve;

    return Raw;
}

void UDistributionFloat::Serialize(FArchive& Ar)
{
    uint8 TypeInt = static_cast<uint8>(Type);
    Ar << TypeInt;
    if (Ar.IsLoading()) Type = static_cast<EDistributionType>(TypeInt);

    Ar << Min << Max;
    Ar << bIsLooped << LoopKeyOffset << bUseExtremes;
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
    Raw.LockedAxesMode = LockedAxesMode;
    Raw.MirrorFlags = MirrorFlags;
    Raw.MinCurve = MinCurve;
    Raw.MaxCurve = MaxCurve;

    return Raw;
}

void UDistributionVector::Serialize(FArchive& Ar)
{
    uint8 TypeInt = static_cast<uint8>(Type);
    Ar << TypeInt;
    if (Ar.IsLoading()) Type = static_cast<EDistributionType>(TypeInt);

    int32 LockedAxesModeInt = static_cast<int32>(LockedAxesMode);
    Ar << Min << Max;
    Ar << bIsLooped << LoopKeyOffset << bUseExtremes;
    Ar << LockedAxesModeInt;
    if (Ar.IsLoading()) LockedAxesMode = static_cast<EParticleLockedAxesMode>(LockedAxesModeInt);
    Ar << MirrorFlags;
    Ar << MinCurve << MaxCurve;
}

// ─────────────────────────────────────────────────────────────────────────────
// UDistributionLinearColor
// ─────────────────────────────────────────────────────────────────────────────

FRawDistributionLinearColor UDistributionLinearColor::BuildRaw() const
{
    FRawDistributionLinearColor Raw;
    Raw.Type = Type;
    Raw.Min  = Min;
    Raw.Max  = Max;
    Raw.bIsLooped = bIsLooped;
    Raw.LoopKeyOffset = LoopKeyOffset;
    Raw.bUseExtremes = bUseExtremes;
    Raw.MinCurve = MinCurve;
    Raw.MaxCurve = MaxCurve;

    return Raw;
}

void UDistributionLinearColor::Serialize(FArchive& Ar)
{
    uint8 TypeInt = static_cast<uint8>(Type);
    Ar << TypeInt;
    if (Ar.IsLoading()) Type = static_cast<EDistributionType>(TypeInt);

    Ar << Min << Max;
    Ar << bIsLooped << LoopKeyOffset << bUseExtremes;
    Ar << MinCurve << MaxCurve;
}
