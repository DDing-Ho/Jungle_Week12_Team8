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
#include "ParticleDistribution.generated.h"

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

enum class EParticleKeyInterpMode : uint8
{
    Constant,
    Linear,
    Cubic,
};

enum class EParticleCurveTangentMode : uint8
{
    Auto,
    User,
    Break,
};

enum class EParticleLockedAxesMode : uint8
{
    None,
    XY,
    XZ,
    YZ,
    XYZ,
};

enum EParticleMirrorFlags : uint8
{
    PMF_None = 0,
    PMF_X    = 1 << 0,
    PMF_Y    = 1 << 1,
    PMF_Z    = 1 << 2,
};

/**
 * 커브의 키프레임 하나.
 * - InterpMode 가 Constant / Linear 이면 Time, Value 위주로 사용
 * - Cubic + Auto:  Arrive/Leave tangent 를 AutoSetTangents() 가 채워줌
 * - Cubic + User:  Arrive/Leave tangent 를 같은 값으로 유지
 * - Cubic + Break: Arrive/Leave tangent 를 독립적으로 설정
 */
struct FFloatCurveKey
{
    float Time           = 0.f;
    float Value          = 0.f;
    float ArriveTangent  = 0.f; // 이 키에 들어오는 기울기 (dV/dT)
    float LeaveTangent   = 0.f; // 이 키에서 나가는 기울기 (dV/dT)
    EParticleKeyInterpMode InterpMode = EParticleKeyInterpMode::Linear;
    EParticleCurveTangentMode TangentMode = EParticleCurveTangentMode::Auto;
};

struct FParticleFloatCurve
{
    TArray<FFloatCurveKey> Keys;

    float Eval(float T) const;

    void AddKey(float Time, float Value,
                EParticleKeyInterpMode InterpMode = EParticleKeyInterpMode::Linear);
    void SortKeys();
    void AutoSetTangents();
    void ComputeAutoTangents() { AutoSetTangents(); }

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
    void SetInterpMode(EParticleKeyInterpMode Mode)
    {
        auto ApplyMode = [Mode](FParticleFloatCurve& Curve)
        {
            for (FFloatCurveKey& Key : Curve.Keys)
            {
                Key.InterpMode = Mode;
                if (Mode == EParticleKeyInterpMode::Cubic)
                {
                    Key.TangentMode = EParticleCurveTangentMode::Auto;
                }
            }
            Curve.AutoSetTangents();
        };

        ApplyMode(R);
        ApplyMode(G);
        ApplyMode(B);
        ApplyMode(A);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Distribution — 값 샘플링 방식
// ─────────────────────────────────────────────────────────────────────────────

UENUM()
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
    float             Min  = 0.f;
    float             Max  = 0.f;
    bool              bIsLooped = false;
    float             LoopKeyOffset = 0.f;
    bool              bUseExtremes = false;
    bool              bCanBeBaked = true;

    FParticleFloatCurve MinCurve;
    FParticleFloatCurve MaxCurve;

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
    FVector           Min       = FVector::ZeroVector;
    FVector           Max       = FVector::ZeroVector;
    bool              bIsLooped = false;
    float             LoopKeyOffset = 0.f;
    bool              bUseExtremes = false;
    bool              bCanBeBaked = true;
    EParticleLockedAxesMode LockedAxesMode = EParticleLockedAxesMode::None;
    uint8             MirrorFlags = PMF_None;

    FVectorCurve      MinCurve;
    FVectorCurve      MaxCurve;

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
    bool              bIsLooped = false;
    float             LoopKeyOffset = 0.f;
    bool              bUseExtremes = false;
    bool              bCanBeBaked = true;

    FLinearColorCurve MinCurve;
    FLinearColorCurve MaxCurve;

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
