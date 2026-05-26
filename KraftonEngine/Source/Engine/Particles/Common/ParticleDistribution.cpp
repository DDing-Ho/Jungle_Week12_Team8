/**
 * @file ParticleDistribution.cpp
 * @brief FRandomStream, FFloatCurve, FRawDistributionFloat/Vector/LinearColor 런타임 구현.
 */

#include "Particles/Common/ParticleDistribution.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float ParticleCurveTimeEpsilon = 1e-6f;

    static FArchive& operator<<(FArchive& Ar, FFloatCurveKey& Key)
    {
        Ar << Key.Time;
        Ar << Key.Value;

        int32 InterpMode = static_cast<int32>(Key.InterpMode);
        Ar << InterpMode;
        if (Ar.IsLoading())
        {
            Key.InterpMode = static_cast<EParticleKeyInterpMode>(InterpMode);
        }

        int32 TangentMode = static_cast<int32>(Key.TangentMode);
        Ar << TangentMode;
        if (Ar.IsLoading())
        {
            Key.TangentMode = static_cast<EParticleCurveTangentMode>(TangentMode);
        }

        Ar << Key.ArriveTangent;
        Ar << Key.LeaveTangent;
        return Ar;
    }

    static float NormalizeCurveTime(float T, const FParticleFloatCurve& Curve, bool bIsLooped, float LoopKeyOffset)
    {
        if (Curve.Keys.empty())
        {
            return T;
        }

        const float StartTime = Curve.Keys.front().Time;
        const float EndTime = Curve.Keys.back().Time;
        if (!bIsLooped)
        {
            return T;
        }

        const float LoopDuration = (EndTime - StartTime) + LoopKeyOffset;
        if (LoopDuration <= ParticleCurveTimeEpsilon)
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

    static float EvalFloatCurveWithOptions(const FParticleFloatCurve& Curve, float T, bool bIsLooped, float LoopKeyOffset)
    {
        return Curve.Eval(NormalizeCurveTime(T, Curve, bIsLooped, LoopKeyOffset));
    }

    static FVector ApplyLockedAxes(const FVector& Value, EParticleLockedAxesMode LockedAxesMode)
    {
        FVector Result = Value;
        switch (LockedAxesMode)
        {
        case EParticleLockedAxesMode::XY:
            Result.Y = Result.X;
            break;
        case EParticleLockedAxesMode::XZ:
            Result.Z = Result.X;
            break;
        case EParticleLockedAxesMode::YZ:
            Result.Z = Result.Y;
            break;
        case EParticleLockedAxesMode::XYZ:
            Result.Y = Result.X;
            Result.Z = Result.X;
            break;
        case EParticleLockedAxesMode::None:
        default:
            break;
        }
        return Result;
    }

    static float MaybeMirrorFloat(float Value, bool bMirror, FRandomStream* Stream)
    {
        if (!bMirror)
        {
            return Value;
        }

        const float Sign = (Stream ? Stream->GetFraction() : 0.5f) < 0.5f ? -1.0f : 1.0f;
        return std::fabs(Value) * Sign;
    }

    static FVector ApplyMirrorFlags(const FVector& Value, uint8 MirrorFlags, FRandomStream* Stream)
    {
        FVector Result = Value;
        Result.X = MaybeMirrorFloat(Result.X, (MirrorFlags & PMF_X) != 0, Stream);
        Result.Y = MaybeMirrorFloat(Result.Y, (MirrorFlags & PMF_Y) != 0, Stream);
        Result.Z = MaybeMirrorFloat(Result.Z, (MirrorFlags & PMF_Z) != 0, Stream);
        return Result;
    }

    static FLinearColor EvalLinearColorCurveWithOptions(const FLinearColorCurve& Curve, float T, bool bIsLooped, float LoopKeyOffset)
    {
        return FLinearColor(
            EvalFloatCurveWithOptions(Curve.R, T, bIsLooped, LoopKeyOffset),
            EvalFloatCurveWithOptions(Curve.G, T, bIsLooped, LoopKeyOffset),
            EvalFloatCurveWithOptions(Curve.B, T, bIsLooped, LoopKeyOffset),
            EvalFloatCurveWithOptions(Curve.A, T, bIsLooped, LoopKeyOffset));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// FRandomStream
// ─────────────────────────────────────────────────────────────────────────────

// State를 LCG 방식으로 갱신한 뒤, 그 비트를 이용해 [0, 1) 범위의 float 난수를 빠르게 만드는 함수
// 가볍고 빠른 deterministic random stream (최고 품질 난수 생성기 아님)
float FRandomStream::GetFraction() const
{
    // 1. 내부 난수 상태 갱신
    State = State * 1664525u + 1013904223u;
    // X[n+1] = (a * X[n] + c) mod m
    // a = 1664525, c = 1013904223, m = 2^32


    // 2. State의 일부 비트를 float의 mantissa에 넣어서
    //    [1.0, 2.0) 범위의 float 생성
    union { uint32 u; float f; } bits;
    bits.u = (State >> 9) | 0x3F800000u;

    // 3. [0.0, 1.0) 범위로 변환
    return bits.f - 1.0f;
}

float FRandomStream::FRandRange(float InMin, float InMax) const
{
    return InMin + (InMax - InMin) * GetFraction();
}

FVector FRandomStream::VRandRange(const FVector& InMin, const FVector& InMax) const
{
    return FVector(
        FRandRange(InMin.X, InMax.X),
        FRandRange(InMin.Y, InMax.Y),
        FRandRange(InMin.Z, InMax.Z)
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// FParticleFloatCurve
// ─────────────────────────────────────────────────────────────────────────────

void FParticleFloatCurve::AddKey(float Time, float Value, EParticleKeyInterpMode InterpMode)
{
    FFloatCurveKey Key;
    Key.Time = Time;
    Key.Value = Value;
    Key.InterpMode = InterpMode;
    Keys.push_back(Key);
    SortKeys();
    AutoSetTangents();
}

void FParticleFloatCurve::SortKeys()
{
    std::sort(Keys.begin(), Keys.end(),
        [](const FFloatCurveKey& A, const FFloatCurveKey& B) { return A.Time < B.Time; });
}

void FParticleFloatCurve::AutoSetTangents()
{
    const int32 N = static_cast<int32>(Keys.size());
    if (N == 0)
    {
        return;
    }

    for (int32 i = 0; i < N; ++i)
    {
        FFloatCurveKey& Key = Keys[i];
        if (Key.TangentMode == EParticleCurveTangentMode::User)
        {
            Key.LeaveTangent = Key.ArriveTangent;
            continue;
        }
        if (Key.TangentMode != EParticleCurveTangentMode::Auto)
        {
            continue;
        }

        float Slope = 0.0f;
        const bool bHasPrev = i > 0;
        const bool bHasNext = i + 1 < N;
        if (bHasPrev && bHasNext)
        {
            const float Dt = Keys[i + 1].Time - Keys[i - 1].Time;
            Slope = std::fabs(Dt) <= ParticleCurveTimeEpsilon ? 0.0f : (Keys[i + 1].Value - Keys[i - 1].Value) / Dt;
        }
        else if (bHasNext)
        {
            const float Dt = Keys[i + 1].Time - Keys[i].Time;
            Slope = std::fabs(Dt) <= ParticleCurveTimeEpsilon ? 0.0f : (Keys[i + 1].Value - Keys[i].Value) / Dt;
        }
        else if (bHasPrev)
        {
            const float Dt = Keys[i].Time - Keys[i - 1].Time;
            Slope = std::fabs(Dt) <= ParticleCurveTimeEpsilon ? 0.0f : (Keys[i].Value - Keys[i - 1].Value) / Dt;
        }

        Key.ArriveTangent = Slope;
        Key.LeaveTangent = Slope;
    }
}

float FParticleFloatCurve::HermiteInterp(float s, float Dt,
                                  float P0, float M0,
                                  float P1, float M1)
{
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float h00 =  2.f*s3 - 3.f*s2 + 1.f;
    const float h10 =      s3 - 2.f*s2 + s;
    const float h01 = -2.f*s3 + 3.f*s2;
    const float h11 =      s3 -     s2;
    return h00*P0 + h10*Dt*M0 + h01*P1 + h11*Dt*M1;
}

float FParticleFloatCurve::Eval(float T) const
{
    if (Keys.empty()) return 0.f;
    if (Keys.size() == 1) return Keys[0].Value;
    if (T <= Keys.front().Time) return Keys.front().Value;
    if (T >= Keys.back().Time)  return Keys.back().Value;

    int32 Lo = 0, Hi = static_cast<int32>(Keys.size()) - 1;
    while (Hi - Lo > 1)
    {
        const int32 Mid = (Lo + Hi) / 2;
        if (Keys[Mid].Time <= T) Lo = Mid;
        else Hi = Mid;
    }

    const FFloatCurveKey& A = Keys[Lo];
    const FFloatCurveKey& B = Keys[Hi];
    const float Dt = B.Time - A.Time;
    const float s  = (T - A.Time) / Dt;

    switch (A.InterpMode)
    {
    case EParticleKeyInterpMode::Constant:
        return A.Value;
    case EParticleKeyInterpMode::Linear:
        return A.Value + s * (B.Value - A.Value);
    case EParticleKeyInterpMode::Cubic:
        return HermiteInterp(s, Dt, A.Value, A.LeaveTangent, B.Value, B.ArriveTangent);
    }
    return A.Value;
}

// ─────────────────────────────────────────────────────────────────────────────
// FRawDistributionFloat — 런타임 전용, BakedMin/Max 로 동작
// ─────────────────────────────────────────────────────────────────────────────

float FRawDistributionFloat::GetValue(float T, FRandomStream* Stream) const
{
    switch (Type)
    {
    case EDistributionType::Constant:
        return Min;

    case EDistributionType::Uniform:
    {
        if (bUseExtremes)
        {
            return (Stream ? Stream->GetFraction() : 0.5f) < 0.5f ? Min : Max;
        }
        const float Alpha = Stream ? Stream->GetFraction() : 0.5f;
        return Min + (Max - Min) * Alpha;
    }

    case EDistributionType::ConstantCurve:
    case EDistributionType::UniformCurve:
    {
        float MinVal = Min;
        float MaxVal = Max;

        if (bCanBeBaked && !BakedMin.empty())
        {
            const float NormT = T * static_cast<float>(NUM_BAKED_SAMPLES - 1);
            int32 Idx = static_cast<int32>(NormT);
            if (Idx >= NUM_BAKED_SAMPLES - 1) Idx = NUM_BAKED_SAMPLES - 2;
            const float Frac = NormT - static_cast<float>(Idx);
            MinVal = BakedMin[Idx] + Frac * (BakedMin[Idx + 1] - BakedMin[Idx]);
            MaxVal = BakedMax[Idx] + Frac * (BakedMax[Idx + 1] - BakedMax[Idx]);
        }
        else
        {
            MinVal = EvalFloatCurveWithOptions(MinCurve, T, bIsLooped, LoopKeyOffset);
            MaxVal = EvalFloatCurveWithOptions(MaxCurve, T, bIsLooped, LoopKeyOffset);
        }

        if (Type == EDistributionType::ConstantCurve)
        {
            return MinVal;
        }
        if (bUseExtremes)
        {
            return (Stream ? Stream->GetFraction() : 0.5f) < 0.5f ? MinVal : MaxVal;
        }
        const float Alpha  = Stream ? Stream->GetFraction() : 0.5f;
        return MinVal + (MaxVal - MinVal) * Alpha;
    }
    }
    return Min;
}

FRawDistributionFloat FRawDistributionFloat::MakeConstant(float Value)
{
    FRawDistributionFloat D;
    D.Type = EDistributionType::Constant;
    D.Min  = Value;
    D.Max  = Value;
    return D;
}

FRawDistributionFloat FRawDistributionFloat::MakeUniform(float InMin, float InMax)
{
    FRawDistributionFloat D;
    D.Type = EDistributionType::Uniform;
    D.Min  = InMin;
    D.Max  = InMax;
    return D;
}

// ─────────────────────────────────────────────────────────────────────────────
// FRawDistributionVector
// ─────────────────────────────────────────────────────────────────────────────

FVector FRawDistributionVector::GetValue(float T, FRandomStream* Stream) const
{
    switch (Type)
    {
    case EDistributionType::Constant:
        return ApplyLockedAxes(Min, LockedAxesMode);

    case EDistributionType::Uniform:
    {
        FVector Result = FVector::ZeroVector;
        if (bUseExtremes)
        {
            Result = (Stream ? Stream->GetFraction() : 0.5f) < 0.5f ? Min : Max;
        }
        else
        {
            Result = Stream ? Stream->VRandRange(Min, Max)
                            : FVector(Min.X + (Max.X - Min.X) * 0.5f,
                                      Min.Y + (Max.Y - Min.Y) * 0.5f,
                                      Min.Z + (Max.Z - Min.Z) * 0.5f);
        }

        Result = ApplyLockedAxes(Result, LockedAxesMode);
        return ApplyMirrorFlags(Result, MirrorFlags, Stream);
    }

    case EDistributionType::ConstantCurve:
    case EDistributionType::UniformCurve:
    {
        FVector MinVal = Min;
        FVector MaxVal = Max;

        if (bCanBeBaked && !BakedMin.empty())
        {
            const float NormT = T * static_cast<float>(NUM_BAKED_SAMPLES - 1);
            int32 Idx = static_cast<int32>(NormT);
            if (Idx >= NUM_BAKED_SAMPLES - 1) Idx = NUM_BAKED_SAMPLES - 2;
            const float Frac = NormT - static_cast<float>(Idx);
            MinVal = BakedMin[Idx] + (BakedMin[Idx + 1] - BakedMin[Idx]) * Frac;
            MaxVal = BakedMax[Idx] + (BakedMax[Idx + 1] - BakedMax[Idx]) * Frac;
        }
        else
        {
            MinVal = FVector(
                EvalFloatCurveWithOptions(MinCurve.X, T, bIsLooped, LoopKeyOffset),
                EvalFloatCurveWithOptions(MinCurve.Y, T, bIsLooped, LoopKeyOffset),
                EvalFloatCurveWithOptions(MinCurve.Z, T, bIsLooped, LoopKeyOffset));
            MaxVal = FVector(
                EvalFloatCurveWithOptions(MaxCurve.X, T, bIsLooped, LoopKeyOffset),
                EvalFloatCurveWithOptions(MaxCurve.Y, T, bIsLooped, LoopKeyOffset),
                EvalFloatCurveWithOptions(MaxCurve.Z, T, bIsLooped, LoopKeyOffset));
        }

        if (Type == EDistributionType::ConstantCurve)
        {
            return ApplyLockedAxes(MinVal, LockedAxesMode);
        }

        FVector Result = FVector::ZeroVector;
        if (bUseExtremes)
        {
            Result = (Stream ? Stream->GetFraction() : 0.5f) < 0.5f ? MinVal : MaxVal;
        }
        else
        {
            Result = Stream ? Stream->VRandRange(MinVal, MaxVal)
                            : FVector(MinVal.X + (MaxVal.X - MinVal.X) * 0.5f,
                                      MinVal.Y + (MaxVal.Y - MinVal.Y) * 0.5f,
                                      MinVal.Z + (MaxVal.Z - MinVal.Z) * 0.5f);
        }

        Result = ApplyLockedAxes(Result, LockedAxesMode);
        return ApplyMirrorFlags(Result, MirrorFlags, Stream);
    }
    }
    return Min;
}

FRawDistributionVector FRawDistributionVector::MakeConstant(const FVector& Value)
{
    FRawDistributionVector D;
    D.Type = EDistributionType::Constant;
    D.Min  = Value;
    D.Max  = Value;
    return D;
}

FRawDistributionVector FRawDistributionVector::MakeUniform(const FVector& InMin, const FVector& InMax)
{
    FRawDistributionVector D;
    D.Type = EDistributionType::Uniform;
    D.Min  = InMin;
    D.Max  = InMax;
    return D;
}

// ─────────────────────────────────────────────────────────────────────────────
// FRawDistributionLinearColor
// ─────────────────────────────────────────────────────────────────────────────

static FLinearColor LerpColor(const FLinearColor& A, const FLinearColor& B, float T)
{
    return FLinearColor(
        A.R + (B.R - A.R) * T,
        A.G + (B.G - A.G) * T,
        A.B + (B.B - A.B) * T,
        A.A + (B.A - A.A) * T
    );
}

FLinearColor FRawDistributionLinearColor::GetValue(float T, FRandomStream* Stream) const
{
    switch (Type)
    {
    case EDistributionType::Constant:
        return Min;

    case EDistributionType::Uniform:
    {
        if (bUseExtremes)
        {
            return (Stream ? Stream->GetFraction() : 0.5f) < 0.5f ? Min : Max;
        }
        const float Alpha = Stream ? Stream->GetFraction() : 0.5f;
        return LerpColor(Min, Max, Alpha);
    }

    case EDistributionType::ConstantCurve:
    case EDistributionType::UniformCurve:
    {
        FLinearColor MinVal = Min;
        FLinearColor MaxVal = Max;

        if (bCanBeBaked && !BakedMin.empty())
        {
            const float NormT = T * static_cast<float>(NUM_BAKED_SAMPLES - 1);
            int32 Idx = static_cast<int32>(NormT);
            if (Idx >= NUM_BAKED_SAMPLES - 1) Idx = NUM_BAKED_SAMPLES - 2;
            const float Frac = NormT - static_cast<float>(Idx);
            MinVal = LerpColor(BakedMin[Idx], BakedMin[Idx + 1], Frac);
            MaxVal = LerpColor(BakedMax[Idx], BakedMax[Idx + 1], Frac);
        }
        else
        {
            MinVal = EvalLinearColorCurveWithOptions(MinCurve, T, bIsLooped, LoopKeyOffset);
            MaxVal = EvalLinearColorCurveWithOptions(MaxCurve, T, bIsLooped, LoopKeyOffset);
        }

        if (Type == EDistributionType::ConstantCurve)
        {
            return MinVal;
        }
        if (bUseExtremes)
        {
            return (Stream ? Stream->GetFraction() : 0.5f) < 0.5f ? MinVal : MaxVal;
        }
        const float Alpha = Stream ? Stream->GetFraction() : 0.5f;
        return LerpColor(MinVal, MaxVal, Alpha);
    }
    }
    return Min;
}

FRawDistributionLinearColor FRawDistributionLinearColor::MakeConstant(const FLinearColor& Value)
{
    FRawDistributionLinearColor D;
    D.Type = EDistributionType::Constant;
    D.Min  = Value;
    D.Max  = Value;
    return D;
}

FRawDistributionLinearColor FRawDistributionLinearColor::MakeUniform(const FLinearColor& InMin,
                                                                       const FLinearColor& InMax)
{
    FRawDistributionLinearColor D;
    D.Type = EDistributionType::Uniform;
    D.Min  = InMin;
    D.Max  = InMax;
    return D;
}

// ─────────────────────────────────────────────────────────────────────────────
// Serialization — Curve 타입 전용 (UDistribution* Serialize에서 사용)
// ─────────────────────────────────────────────────────────────────────────────

FArchive& operator<<(FArchive& Ar, FRandomStream& S)
{
    Ar << S.InitialSeed;
    if (Ar.IsLoading()) S.Reset();
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FParticleFloatCurve& C)
{
    uint32 KeyCount = static_cast<uint32>(C.Keys.size());
    Ar << KeyCount;
    if (Ar.IsLoading())
    {
        C.Keys.resize(KeyCount);
    }

    for (FFloatCurveKey& Key : C.Keys)
    {
        Ar << Key;
    }

    if (Ar.IsLoading())
    {
        C.SortKeys();
        C.AutoSetTangents();
    }
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FVectorCurve& C)
{
    Ar << C.X << C.Y << C.Z;
    return Ar;
}

FArchive& operator<<(FArchive& Ar, FLinearColorCurve& C)
{
    Ar << C.R << C.G << C.B << C.A;
    return Ar;
}
