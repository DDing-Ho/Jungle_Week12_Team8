/**
 * @file ParticleDistribution.cpp
 * @brief FRandomStream, FFloatCurve, FRawDistributionFloat/Vector/LinearColor 런타임 구현.
 */

#include "Particles/Common/ParticleDistribution.h"
#include <algorithm>

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
// FFloatCurve
// ─────────────────────────────────────────────────────────────────────────────

void FFloatCurve::AddKey(float Time, float Value, float ArriveTangent, float LeaveTangent)
{
    FFloatCurveKey Key;
    Key.Time           = Time;
    Key.Value          = Value;
    Key.ArriveTangent  = ArriveTangent;
    Key.LeaveTangent   = (InterpMode == ECurveInterpMode::CurveUser) ? ArriveTangent : LeaveTangent;

    Keys.push_back(Key);
    std::sort(Keys.begin(), Keys.end(),
        [](const FFloatCurveKey& A, const FFloatCurveKey& B) { return A.Time < B.Time; });

    if (InterpMode == ECurveInterpMode::CurveAuto)
        ComputeAutoTangents();
}

void FFloatCurve::ComputeAutoTangents()
{
    const int32 N = static_cast<int32>(Keys.size());
    if (N < 2) return;

    for (int32 i = 0; i < N; ++i)
    {
        float Tangent;
        if (i == 0)
            Tangent = (Keys[1].Value - Keys[0].Value) / (Keys[1].Time - Keys[0].Time);
        else if (i == N - 1)
            Tangent = (Keys[N-1].Value - Keys[N-2].Value) / (Keys[N-1].Time - Keys[N-2].Time);
        else
            Tangent = (Keys[i+1].Value - Keys[i-1].Value) / (Keys[i+1].Time - Keys[i-1].Time);

        Keys[i].ArriveTangent = Tangent;
        Keys[i].LeaveTangent  = Tangent;
    }
}

float FFloatCurve::HermiteInterp(float s, float Dt,
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

float FFloatCurve::Eval(float T) const
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

    switch (InterpMode)
    {
    case ECurveInterpMode::Constant:
        return A.Value;
    case ECurveInterpMode::Linear:
        return A.Value + s * (B.Value - A.Value);
    case ECurveInterpMode::CurveAuto:
    case ECurveInterpMode::CurveUser:
    case ECurveInterpMode::CurveBreak:
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
        const float Alpha = Stream ? Stream->GetFraction() : 0.5f;
        return Min + (Max - Min) * Alpha;
    }

    case EDistributionType::ConstantCurve:
    case EDistributionType::UniformCurve:
    {
        if (BakedMin.empty()) return Min;

        const float NormT = T * static_cast<float>(NUM_BAKED_SAMPLES - 1);
        int32 Idx = static_cast<int32>(NormT);
        if (Idx >= NUM_BAKED_SAMPLES - 1) Idx = NUM_BAKED_SAMPLES - 2;
        const float Frac = NormT - static_cast<float>(Idx);

        const float MinVal = BakedMin[Idx] + Frac * (BakedMin[Idx + 1] - BakedMin[Idx]);
        if (Type == EDistributionType::ConstantCurve)
            return MinVal;

        const float MaxVal = BakedMax[Idx] + Frac * (BakedMax[Idx + 1] - BakedMax[Idx]);
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
        return Min;

    case EDistributionType::Uniform:
    {
        if (bLockAxes)
        {
            const float Alpha = Stream ? Stream->GetFraction() : 0.5f;
            const float S = Min.X + (Max.X - Min.X) * Alpha;
            return FVector(S, S, S);
        }
        return Stream ? Stream->VRandRange(Min, Max)
                      : FVector(Min.X + (Max.X - Min.X) * 0.5f,
                                Min.Y + (Max.Y - Min.Y) * 0.5f,
                                Min.Z + (Max.Z - Min.Z) * 0.5f);
    }

    case EDistributionType::ConstantCurve:
    case EDistributionType::UniformCurve:
    {
        if (BakedMin.empty()) return Min;

        const float NormT = T * static_cast<float>(NUM_BAKED_SAMPLES - 1);
        int32 Idx = static_cast<int32>(NormT);
        if (Idx >= NUM_BAKED_SAMPLES - 1) Idx = NUM_BAKED_SAMPLES - 2;
        const float Frac = NormT - static_cast<float>(Idx);

        const FVector MinVal = BakedMin[Idx] + (BakedMin[Idx + 1] - BakedMin[Idx]) * Frac;
        if (Type == EDistributionType::ConstantCurve)
            return MinVal;

        const FVector MaxVal = BakedMax[Idx] + (BakedMax[Idx + 1] - BakedMax[Idx]) * Frac;
        if (bLockAxes)
        {
            const float Alpha = Stream ? Stream->GetFraction() : 0.5f;
            const float S = MinVal.X + (MaxVal.X - MinVal.X) * Alpha;
            return FVector(S, S, S);
        }
        return Stream ? Stream->VRandRange(MinVal, MaxVal)
                      : FVector(MinVal.X + (MaxVal.X - MinVal.X) * 0.5f,
                                MinVal.Y + (MaxVal.Y - MinVal.Y) * 0.5f,
                                MinVal.Z + (MaxVal.Z - MinVal.Z) * 0.5f);
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
        const float Alpha = Stream ? Stream->GetFraction() : 0.5f;
        return LerpColor(Min, Max, Alpha);
    }

    case EDistributionType::ConstantCurve:
    case EDistributionType::UniformCurve:
    {
        if (BakedMin.empty()) return Min;

        const float NormT = T * static_cast<float>(NUM_BAKED_SAMPLES - 1);
        int32 Idx = static_cast<int32>(NormT);
        if (Idx >= NUM_BAKED_SAMPLES - 1) Idx = NUM_BAKED_SAMPLES - 2;
        const float Frac = NormT - static_cast<float>(Idx);

        const FLinearColor MinVal = LerpColor(BakedMin[Idx], BakedMin[Idx + 1], Frac);
        if (Type == EDistributionType::ConstantCurve)
            return MinVal;

        const FLinearColor MaxVal = LerpColor(BakedMax[Idx], BakedMax[Idx + 1], Frac);
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

FArchive& operator<<(FArchive& Ar, FFloatCurve& C)
{
    uint8 Mode = static_cast<uint8>(C.InterpMode);
    Ar << Mode;
    if (Ar.IsLoading()) C.InterpMode = static_cast<ECurveInterpMode>(Mode);
    Ar << C.Keys;
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
