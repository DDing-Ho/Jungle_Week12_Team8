/**
 * @file ParticleDistribution.h
 * @brief Particle 랜덤 분포 및 커브 기반 값 타입 정의.
 *
 * 포함 타입:
 * - FRandomStream: Seed 기반 재현 가능한 의사난수 스트림 (LCG)
 * - FFloatCurve / FVectorCurve: 키프레임 보간 커브
 * - FRawDistributionFloat / FRawDistributionVector: 런타임 Distribution (Baked 테이블 포함)
 */

#pragma once

#include "Core/CoreTypes.h"
#include "Core/EngineTypes.h"
#include "Math/Vector.h"
#include "Serialization/Archive.h"

// ─────────────────────────────────────────────────────────────────────────────
// FRandomStream — Seed 기반 재현 가능한 의사난수 스트림 (LCG)
// ─────────────────────────────────────────────────────────────────────────────

struct FRandomStream
{
    int32 InitialSeed = 0;

    FRandomStream() { Reset(); }
    explicit FRandomStream(int32 InSeed) : InitialSeed(InSeed) { Reset(); }

    void Initialize(int32 InSeed) { InitialSeed = InSeed; Reset(); }
    void Reset() { State = static_cast<uint32>(InitialSeed); }

    float   GetFraction() const;
    float   FRandRange(float InMin, float InMax) const;
    FVector VRandRange(const FVector& InMin, const FVector& InMax) const;

private:
    mutable uint32 State = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Curve — 키프레임 보간
// ─────────────────────────────────────────────────────────────────────────────

enum class EParticleCurveInterpMode : uint8
{
    Linear,      // 직선 보간
    Constant,    // 계단식 (다음 키까지 값 유지)
    CurveAuto,   // Cubic — 탄젠트 자동 계산 (Catmull-Rom)
    CurveUser,   // Cubic — 탄젠트 수동 입력, In=Out 대칭
    CurveBreak,  // Cubic — In/Out 탄젠트 완전 독립
};

/**
 * 커브의 키프레임 하나.
 * - Linear / Constant: Time, Value 만 사용
 * - CurveAuto:         ArriveTangent / LeaveTangent 를 ComputeAutoTangents() 가 채워줌
 * - CurveUser:         LeaveTangent 를 설정하면 ArriveTangent 도 같은 값으로 미러
 * - CurveBreak:        ArriveTangent / LeaveTangent 를 독립적으로 설정
 */
struct FFloatCurveKey
{
    float Time           = 0.f;
    float Value          = 0.f;
    float ArriveTangent  = 0.f; // 이 키에 들어오는 기울기 (dV/dT)
    float LeaveTangent   = 0.f; // 이 키에서 나가는 기울기 (dV/dT)
};

struct FParticleFloatCurve
{
    TArray<FFloatCurveKey> Keys;
    EParticleCurveInterpMode InterpMode = EParticleCurveInterpMode::Linear;

    float Eval(float T) const;

    // ArriveTangent / LeaveTangent 는 CurveUser / CurveBreak 에서만 의미 있음.
    // CurveAuto 는 ComputeAutoTangents() 가 자동으로 채우므로 전달 불필요.
    void AddKey(float Time, float Value,
                float ArriveTangent = 0.f, float LeaveTangent = 0.f);

    // CurveAuto 모드에서 모든 키의 탄젠트를 Catmull-Rom으로 재계산
    void ComputeAutoTangents();

private:
    // 구간 [A, B] 에서 s∈[0,1] 위치의 Cubic Hermite 값
    static float HermiteInterp(float s, float Dt,
                                float P0, float M0,
                                float P1, float M1);
};

struct FVectorCurve
{
    FParticleFloatCurve X, Y, Z;

    FVector Eval(float T) const { return FVector(X.Eval(T), Y.Eval(T), Z.Eval(T)); }

    void AddKey(float Time, const FVector& Value)
    {
        X.AddKey(Time, Value.X);
        Y.AddKey(Time, Value.Y);
        Z.AddKey(Time, Value.Z);
    }
};

/** RGBA 채널별 독립 커브 — Color 모듈 전용 */
struct FLinearColorCurve
{
    FParticleFloatCurve R, G, B, A;

    FLinearColor Eval(float T) const
    {
        return FLinearColor(R.Eval(T), G.Eval(T), B.Eval(T), A.Eval(T));
    }

    void AddKey(float Time, const FLinearColor& Value)
    {
        R.AddKey(Time, Value.R);
        G.AddKey(Time, Value.G);
        B.AddKey(Time, Value.B);
        A.AddKey(Time, Value.A);
    }

    // 모든 채널의 InterpMode를 한 번에 설정
    void SetInterpMode(EParticleCurveInterpMode Mode)
    {
        R.InterpMode = G.InterpMode = B.InterpMode = A.InterpMode = Mode;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Distribution — 값 샘플링 방식
// ─────────────────────────────────────────────────────────────────────────────

enum class EDistributionType : uint8
{
    Constant,       // 고정값 (Min 사용)
    Uniform,        // [Min, Max] 균등 랜덤
    ConstantCurve,  // 파티클 수명(T)에 따른 커브값
    UniformCurve,   // 수명에 따른 커브 범위에서 랜덤
};

// 언리얼 Cascade와 같은 수치 사용
static constexpr int32 NUM_BAKED_SAMPLES = 256; // 커브를 256등분으로 굽기

// ── Float Distribution (런타임 전용 — 커브 없음, Baked 테이블만) ──────────────

struct FRawDistributionFloat
{
    EDistributionType Type = EDistributionType::Constant;
    float             Min  = 5.f;
    float             Max  = 10.f;

    // 런타임 Baked 테이블 (Curve 타입일 때 사용, 직렬화하지 않음)
    TArray<float> BakedMin;
    TArray<float> BakedMax;

    // T: 파티클 RelativeTime(0~1), Stream: nullptr이면 중간값
    float GetValue(float T, FRandomStream* Stream = nullptr) const;

    static FRawDistributionFloat MakeConstant(float Value);
    static FRawDistributionFloat MakeUniform(float InMin, float InMax);
};

// ── Vector Distribution (런타임 전용) ────────────────────────────────────────

struct FRawDistributionVector
{
    EDistributionType Type      = EDistributionType::Constant;
    FVector           Min       = FVector(0.f, 0.f, 5.f); //FVector::ZeroVector;
    FVector           Max       = FVector(0.f, 0.f, 10.f); //FVector::ZeroVector;
    bool              bLockAxes = false;

    TArray<FVector> BakedMin;
    TArray<FVector> BakedMax;

    FVector GetValue(float T, FRandomStream* Stream = nullptr) const;

    static FRawDistributionVector MakeConstant(const FVector& Value);
    static FRawDistributionVector MakeUniform(const FVector& InMin, const FVector& InMax);
};

// ── LinearColor Distribution (런타임 전용) ───────────────────────────────────

struct FRawDistributionLinearColor
{
    EDistributionType Type = EDistributionType::Constant;
    FLinearColor      Min  = FLinearColor::White();
    FLinearColor      Max  = FLinearColor::White();

    // 런타임 Baked 테이블 (직렬화하지 않음)
    TArray<FLinearColor> BakedMin;
    TArray<FLinearColor> BakedMax;

    FLinearColor GetValue(float T, FRandomStream* Stream = nullptr) const;

    static FRawDistributionLinearColor MakeConstant(const FLinearColor& Value);
    static FRawDistributionLinearColor MakeUniform(const FLinearColor& InMin,
                                                    const FLinearColor& InMax);
};

// ─────────────────────────────────────────────────────────────────────────────
// Serialization
// ─────────────────────────────────────────────────────────────────────────────

// Curve 타입 직렬화 (UDistribution* Serialize에서 사용)
FArchive& operator<<(FArchive& Ar, FRandomStream& S);
FArchive& operator<<(FArchive& Ar, FParticleFloatCurve& C);
FArchive& operator<<(FArchive& Ar, FVectorCurve& C);
FArchive& operator<<(FArchive& Ar, FLinearColorCurve& C);
