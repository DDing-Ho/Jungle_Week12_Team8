/**
 * @file ParticleMotionModules.h
 * @brief Particle 운동 / 회전 / 힘 관련 Module 정의.
 */

#pragma once

#include "ParticleCoreModules.h"
#include "ParticleMotionModules.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleRotation
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleRotation : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleRotation)

    virtual EParticleModuleType GetModuleType() const override
    {
        return EParticleModuleType::PMT_Rotation;
    }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override
    {
        return EParticleModuleUpdatePhase::PMUP_Spawn;
    }
    virtual EParticleModuleClass GetModuleClass() const override
    {
        return EParticleModuleClass::Rotation;
    }
    virtual bool SupportsRandomSeed() const override
    {
        return true;
    }
    virtual void Serialize(FArchive &Ar) override;
    virtual void CacheModuleValues() override;
    virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) const override;
    virtual void Spawn(FParticleEmitterInstance *Owner, FBaseParticle &Particle, float SpawnTime, int32 ModuleOffset = INDEX_NONE) override;

    UDistributionFloat* GetRotationDist() const
    {
        return RotationDist;
    }

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Rotation", Type=Distribution, Class=UDistributionFloat)
    UDistributionFloat* RotationDist = nullptr;
    FRawDistributionFloat RawRotation = FRawDistributionFloat::MakeConstant(0.f);
    UPROPERTY(Edit, Category="Particle", DisplayName="Initial Mesh Rotation")
    FRotator InitialMeshRotation; // Mesh Particle 초기 회전값
};

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleRotationRate
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleRotationRate : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleRotationRate)

    virtual EParticleModuleType GetModuleType() const override
    {
        return EParticleModuleType::PMT_RotationRate;
    }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override
    {
        return EParticleModuleUpdatePhase::PMUP_Spawn;
    }
    virtual EParticleModuleClass GetModuleClass() const override
    {
        return EParticleModuleClass::RotationRate;
    }
    virtual bool SupportsRandomSeed() const override
    {
        return true;
    }
    virtual void Serialize(FArchive &Ar) override;
    virtual void CacheModuleValues() override;
    virtual void Spawn(FParticleEmitterInstance *Owner, FBaseParticle &Particle, float SpawnTime, int32 ModuleOffset = INDEX_NONE) override;

    UDistributionFloat* GetRotationRateDist() const
    {
        return RotationRateDist;
    }

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Rotation Rate", Type=Distribution, Class=UDistributionFloat)
    UDistributionFloat* RotationRateDist = nullptr;
    FRawDistributionFloat RawRotationRate = FRawDistributionFloat::MakeConstant(0.f);
    UPROPERTY(Edit, Category="Particle", DisplayName="Mesh Rotation Rate")
    FVector MeshRotationRate; // Mesh Particle 회전 속도
};

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleAcceleration
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleAcceleration : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleAcceleration)

    virtual EParticleModuleType GetModuleType() const override
    {
        return EParticleModuleType::PMT_Acceleration;
    }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override
    {
        return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate;
    }
    virtual EParticleModuleClass GetModuleClass() const override
    {
        return EParticleModuleClass::Acceleration;
    }
    virtual bool SupportsRandomSeed() const override
    {
        return true;
    }
    virtual void Serialize(FArchive &Ar) override;
    virtual void CacheModuleValues() override;
    virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) const override;
    virtual void Spawn(FParticleEmitterInstance *Owner, FBaseParticle &Particle, float SpawnTime, int32 ModuleOffset = INDEX_NONE) override;
    virtual void Update(
        FParticleEmitterInstance *Owner,
        float DeltaTime,
        int32 ModuleOffset = INDEX_NONE,
        TArray<FParticleEventData>* OutEventQueue = nullptr) override;

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Acceleration", Type=Distribution, Class=UDistributionVector)
    UDistributionVector* AccelerationDist = nullptr;
    FRawDistributionVector RawAcceleration = FRawDistributionVector::MakeConstant(FVector::ZeroVector);
    UPROPERTY(Edit, Category="Particle", DisplayName="Drag", Min=0.0, Max=1000.0, Speed=0.01)
    float   Drag = 0.0f;                             // 속도 감쇠 계수
};

// ─────────────────────────────────────────────────────────────────────────────
// Beam Modules
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleBeamSource : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleBeamSource)

    virtual EParticleModuleType GetModuleType() const override { return EParticleModuleType::PMT_Custom; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::BeamSource; }
    virtual void Serialize(FArchive& Ar) override;

    const FVector& GetSource() const { return Source; }
    void SetSource(const FVector& InSource) { Source = InSource; }

  private:
    UPROPERTY(Edit, Category="Beam", DisplayName="Source")
    FVector Source = FVector::ZeroVector;
};

UCLASS()
class UParticleModuleBeamTarget : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleBeamTarget)

    virtual EParticleModuleType GetModuleType() const override { return EParticleModuleType::PMT_Custom; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::BeamTarget; }
    virtual void Serialize(FArchive& Ar) override;

    const FVector& GetTarget() const { return Target; }
    void SetTarget(const FVector& InTarget) { Target = InTarget; }

  private:
    UPROPERTY(Edit, Category="Beam", DisplayName="Target")
    FVector Target = FVector(100.0f, 0.0f, 0.0f);
};

UCLASS()
class UParticleModuleBeamShape : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleBeamShape)

    virtual EParticleModuleType GetModuleType() const override { return EParticleModuleType::PMT_Custom; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::BeamShape; }
    virtual void Serialize(FArchive& Ar) override;

    bool ShouldOverrideBeamPoints() const { return bOverrideBeamPoints; }
    void BuildBeamPoints(const FVector& Source, const FVector& Target, int32 SegmentCount, TArray<FVector>& OutPoints) const;

  private:
    UPROPERTY(Edit, Category="Beam", DisplayName="Shape Mode", Type=Enum, Enum=StaticEnum_EParticleBeamShapeMode())
    EParticleBeamShapeMode ShapeMode = EParticleBeamShapeMode::Arc;
    UPROPERTY(Edit, Category="Beam", DisplayName="Override Beam Points")
    bool bOverrideBeamPoints = false;
    UPROPERTY(Edit, Category="Beam", DisplayName="Shape Offset")
    FVector ShapeOffset = FVector(0.0f, 0.0f, 50.0f);
    UPROPERTY(Edit, Category="Beam", DisplayName="Sine Frequency", Min=0.0, Max=1000.0, Speed=0.1)
    float SineFrequency = 1.0f;
};

UCLASS()
class UParticleModuleBeamNoise : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleBeamNoise)

    virtual EParticleModuleType GetModuleType() const override { return EParticleModuleType::PMT_Custom; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::BeamNoise; }
    virtual void Serialize(FArchive& Ar) override;

    void ApplyNoise(float EmitterTime, TArray<FVector>& InOutPoints) const;

  private:
    UPROPERTY(Edit, Category="Beam", DisplayName="Noise Amplitude")
    FVector NoiseAmplitude = FVector(0.0f, 0.0f, 25.0f);
    UPROPERTY(Edit, Category="Beam", DisplayName="Frequency", Min=0.0, Max=1000.0, Speed=0.1)
    float Frequency = 2.0f;
    UPROPERTY(Edit, Category="Beam", DisplayName="Speed", Min=-1000.0, Max=1000.0, Speed=0.1)
    float Speed = 1.0f;
    UPROPERTY(Edit, Category="Beam", DisplayName="Phase", Min=-100000.0, Max=100000.0, Speed=0.1)
    float Phase = 0.0f;
};
