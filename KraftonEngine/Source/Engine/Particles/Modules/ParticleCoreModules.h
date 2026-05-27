/**
 * @file ParticleCoreModules.h
 * @brief 기본 Particle Module 정의.
 *
 * 각 모듈은 에디터 측 UDistribution* (커브 편집 가능) 과
 * 런타임 측 FRawDistribution (Baked 테이블) 을 함께 보유한다.
 * CacheModuleValues()를 호출하면 UDistribution → FRawDistribution 동기화가 된다.
 */

#pragma once

#include "ParticleModules.h"
#include "Particles/Common/ParticleDistributionTypes.h"
#include "Core/Property/PropertyTypes.h"
#include "ParticleCoreModules.generated.h"
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleRequired
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleRequired : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleRequired)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Required; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Required; }
    virtual void Serialize(FArchive& Ar) override;

    EParticleEmitterType GetEmitterType() const { return EmitterType; }
    void                 SetEmitterType(EParticleEmitterType InType) { EmitterType = InType; }

    UMaterial *GetMaterial() const { return Material; }
    void       SetMaterial(UMaterial *InMaterial);
    const FString& GetMaterialPath() const { return MaterialSlot.Path; }
    void       SetMaterialPath(const FString& InMaterialPath);

    EParticleSortMode GetSortMode() const { return SortMode; }
    void              SetSortMode(EParticleSortMode InSortMode) { SortMode = InSortMode; }
    int32             GetTranslucencySortPriority() const { return TranslucencySortPriority; }
    int32             GetSubImagesHorizontal() const;
    int32             GetSubImagesVertical() const;
    int32             GetSubImageCount() const;
    EParticleSubUVInterpMethod GetSubUVInterpolationMethod() const { return InterpolationMethod; }
    int32             GetRandomImageChanges() const { return RandomImageChanges; }
    float             GetRandomImageTime() const;

    virtual void PostEditProperty(const char* PropertyName) override;

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Emitter Type")
    EParticleEmitterType EmitterType = EParticleEmitterType::PET_Sprite; // 기본 Emitter 타입
    UPROPERTY(Edit, Category="Particle", DisplayName="Material", Type=MaterialSlot)
    FMaterialSlot        MaterialSlot;                                    // Particle 렌더링 Material 경로
    UMaterial           *Material = nullptr;                             // Particle 렌더링 Material
    UPROPERTY(Edit, Category="Particle", DisplayName="Sort Mode")
    EParticleSortMode    SortMode = EParticleSortMode::PSM_None;         // Particle 정렬 방식
    UPROPERTY(Edit, Category="Particle", DisplayName="Translucency Sort Priority", Speed=1.0)
    int32                TranslucencySortPriority = 0;                   // Translucent Pass 정렬 우선순위
    UPROPERTY(Edit, Category="SubUV", DisplayName="Sub Images Horizontal", Min=1, Max=1024, Speed=1.0)
    int32                SubImages_Horizontal = 1;                       // SubUV atlas 가로 프레임 수
    UPROPERTY(Edit, Category="SubUV", DisplayName="Sub Images Vertical", Min=1, Max=1024, Speed=1.0)
    int32                SubImages_Vertical = 1;                         // SubUV atlas 세로 프레임 수
    UPROPERTY(Edit, Category="SubUV", DisplayName="Interpolation Method")
    EParticleSubUVInterpMethod InterpolationMethod = EParticleSubUVInterpMethod::PSUVIM_None;
    UPROPERTY(Edit, Category="SubUV", DisplayName="Random Image Changes", Min=0, Max=1024, Speed=1.0)
    int32                RandomImageChanges = 0;                         // 수명 중 random frame 변경 횟수
};

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleSpawn
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleSpawn : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleSpawn)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Spawn; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Spawn; }
    virtual bool SupportsRandomSeed() const override { return true; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void CacheModuleValues() override;

    float GetSpawnRate(float EmitterTime = 0.0f) const { return RawSpawnRate.GetValue(EmitterTime, const_cast<FRandomStream*>(&ModuleStream)); }
    UDistributionFloat* GetSpawnRateDist() const { return SpawnRateDist; }

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Spawn Rate", Type=Distribution, Class=UDistributionFloat)
    UDistributionFloat* SpawnRateDist = nullptr;
    FRawDistributionFloat RawSpawnRate = FRawDistributionFloat::MakeConstant(10.0f);
};

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleSpawnPerUnit
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleSpawnPerUnit : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleSpawnPerUnit)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_SpawnPerUnit; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::SpawnPerUnit; }
    virtual void Serialize(FArchive& Ar) override;

    float GetUnitDistance() const { return (std::max)(0.001f, UnitDistance); }
    int32 GetMaxSpawnCountPerFrame() const { return (std::max)(1, MaxSpawnCountPerFrame); }
    float GetMaxFrameDistance() const { return (std::max)(0.0f, MaxFrameDistance); }
    bool ShouldIgnoreSpawnRate() const { return bIgnoreSpawnRate; }

  private:
    UPROPERTY(Edit, Category="Spawn Per Unit", DisplayName="Unit Distance", Min=0.001, Max=100000.0, Speed=0.1)
    float UnitDistance = 10.0f;
    UPROPERTY(Edit, Category="Spawn Per Unit", DisplayName="Max Spawn Count Per Frame", Min=1, Max=100000, Speed=1.0)
    int32 MaxSpawnCountPerFrame = 32;
    UPROPERTY(Edit, Category="Spawn Per Unit", DisplayName="Max Frame Distance", Min=0.0, Max=1000000.0, Speed=0.1)
    float MaxFrameDistance = 0.0f;
    UPROPERTY(Edit, Category="Spawn Per Unit", DisplayName="Ignore Spawn Rate")
    bool bIgnoreSpawnRate = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleLifetime
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleLifetime : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleLifetime)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Lifetime; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Lifetime; }
    virtual bool SupportsRandomSeed() const override { return true; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void CacheModuleValues() override;
    virtual void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime, int32 ModuleOffset = INDEX_NONE) override;

    UDistributionFloat* GetLifetimeDist() const { return LifetimeDist; }

  private:
    // 에디터 측: 커브/범위 편집 가능
    UPROPERTY(Edit, Category="Particle", DisplayName="Lifetime", Type=Distribution, Class=UDistributionFloat)
    UDistributionFloat* LifetimeDist = nullptr;
    // 런타임 측: Baked 테이블
    FRawDistributionFloat RawLifetime = FRawDistributionFloat::MakeConstant(1.0f);
};

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleLocation
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleLocation : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleLocation)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Location; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Location; }
    virtual bool SupportsRandomSeed() const override { return true; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void CacheModuleValues() override;
    virtual void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime, int32 ModuleOffset = INDEX_NONE) override;

    UDistributionVector* GetLocationDist() const { return LocationDist; }

  private:
	UPROPERTY(Edit, Category="Particle", DisplayName="Location", Type=Distribution, Class=UDistributionVector)
	UDistributionVector* LocationDist = nullptr;
	FRawDistributionVector RawLocation = FRawDistributionVector::MakeConstant(FVector::ZeroVector);
};

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleVelocity
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleVelocity : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleVelocity)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Velocity; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Velocity; }
    virtual bool SupportsRandomSeed() const override { return true; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void CacheModuleValues() override;
    virtual void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime, int32 ModuleOffset = INDEX_NONE) override;

    UDistributionVector* GetVelocityDist() const { return VelocityDist; }

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Velocity", Type=Distribution, Class=UDistributionVector)
    UDistributionVector* VelocityDist = nullptr;
    FRawDistributionVector RawVelocity = FRawDistributionVector::MakeUniform(FVector(0.f, 0.f, 0.f), FVector(0.f, 0.f, 10.f));
};

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleColor
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleColor : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleColor)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Color; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Color; }
    virtual bool SupportsRandomSeed() const override { return true; }
    virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) const override;
    virtual void Serialize(FArchive& Ar) override;
    virtual void CacheModuleValues() override;
    virtual void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime, int32 ModuleOffset = INDEX_NONE) override;

    UDistributionLinearColor* GetColorDist() const { return ColorDist; }

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Color", Type=Distribution, Class=UDistributionLinearColor)
    UDistributionLinearColor* ColorDist = nullptr;
    FRawDistributionLinearColor RawColor = FRawDistributionLinearColor::MakeConstant(FLinearColor::White());
};

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModuleSize
// ─────────────────────────────────────────────────────────────────────────────

UCLASS()
class UParticleModuleSize : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleSize)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Size; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Spawn; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Size; }
    virtual bool SupportsRandomSeed() const override { return true; }
    virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* TypeData) const override;
    virtual void Serialize(FArchive& Ar) override;
    virtual void CacheModuleValues() override;
    virtual void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime, int32 ModuleOffset = INDEX_NONE) override;

    UDistributionVector* GetSizeDist() const { return SizeDist; }

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Size", Type=Distribution, Class=UDistributionVector)
    UDistributionVector* SizeDist = nullptr;
    FRawDistributionVector RawSize = FRawDistributionVector::MakeConstant(FVector(1.f, 1.f, 1.f));
};
