/**
 * @file ParticleRenderExpressionModules.h
 * @brief Particle 렌더링 표현 확장 Module 정의.
 *
 * 포함 클래스:
 * - UParticleModuleSubUVBase: Cascade SubUV 공통 base
 * - UParticleModuleSubImageIndex: RelativeTime 기반 SubImage Index
 * - UParticleModuleSubUVMovie: FPS 기반 SubUV Movie
 * - UParticleModuleLight: Particle 기반 Light 효과
 * - UParticleModuleVectorField: Vector Field 기반 이동
 * - UParticleModuleCamera: 카메라 기준 보정
 * - UParticleModuleParameter: Material 등 외부 Parameter 전달
 */

#pragma once

#include "ParticleEventModules.h"
#include "ParticleRenderExpressionModules.generated.h"

/** Cascade SubUV 공통 base. Add Module 메뉴에는 노출하지 않는다. */
UCLASS()
class UParticleModuleSubUVBase : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleSubUVBase)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_SubUV; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate; }
    virtual void Serialize(FArchive& Ar) override;

    bool UsesRealTime() const { return bUseRealTime; }

  protected:
    UParticleModuleRequired* GetRequiredModule(FParticleEmitterInstance* Owner) const;
    EParticleSubUVInterpMethod GetInterpolationMethod(FParticleEmitterInstance* Owner) const;
    int32 GetSubImageCount(FParticleEmitterInstance* Owner) const;
    int32 ClampFrameIndex(FParticleEmitterInstance* Owner, int32 InFrame) const;
    int32 WrapFrameIndex(FParticleEmitterInstance* Owner, int32 InFrame) const;
    float WrapFrameValue(FParticleEmitterInstance* Owner, float InFrame) const;
    float GetDeltaTime(FParticleEmitterInstance* Owner, float DeltaTime) const;
    void SetParticleSubUVFrames(FBaseParticle& Particle, float FrameA, float FrameB, float Lerp) const;
    void SetParticleSequentialSubUV(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float ImageIndex, bool bBlend, bool bLoop) const;
    float SelectRandomFrame(FParticleEmitterInstance* Owner) const;

    UPROPERTY(Edit, Category="Realtime", DisplayName="Use Real Time")
    bool bUseRealTime = false;
};

/** RelativeTime 기반 SubImage Index 모듈 */
UCLASS()
class UParticleModuleSubImageIndex : public UParticleModuleSubUVBase
{
  public:
    GENERATED_BODY(UParticleModuleSubImageIndex)

    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::SubImageIndex; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void CacheModuleValues() override;
    virtual void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;
    virtual void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

    UDistributionFloat* GetSubImageIndexDist() const { return SubImageIndexDist; }

  private:
    void InitializeDefaultSubImageIndexDistribution();
    void ApplySubImageIndex(FParticleEmitterInstance* Owner, FBaseParticle& Particle);
    void InitializeRandomSubImage(FParticleEmitterInstance* Owner, FBaseParticle& Particle);
    void ApplyRandomSubImage(FParticleEmitterInstance* Owner, FBaseParticle& Particle);

    UDistributionFloat* SubImageIndexDist = nullptr;
    FRawDistributionFloat RawSubImageIndex = FRawDistributionFloat::MakeConstant(0.0f);
};

/** FPS 기반 SubUV Movie 모듈 */
UCLASS()
class UParticleModuleSubUVMovie : public UParticleModuleSubUVBase
{
  public:
    GENERATED_BODY(UParticleModuleSubUVMovie)

    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::SubUVMovie; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void CacheModuleValues() override;
    virtual void Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime) override;
    virtual void Update(FParticleEmitterInstance* Owner, float DeltaTime) override;

    UDistributionFloat* GetFrameRateDist() const { return FrameRateDist; }

  private:
    void InitializeDefaultFrameRateDistribution();
    int32 ResolveStartingFrame(FParticleEmitterInstance* Owner);
    void ApplyMovieFrame(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float DeltaTime);

    UDistributionFloat* FrameRateDist = nullptr;
    FRawDistributionFloat RawFrameRate = FRawDistributionFloat::MakeConstant(30.0f);

    UPROPERTY(Edit, Category="Flipbook", DisplayName="Starting Frame", Min=0, Max=100000, Speed=1.0)
    int32 StartingFrame = 1;
    UPROPERTY(Edit, Category="Flipbook", DisplayName="Use Emitter Time")
    bool bUseEmitterTime = false;
};

/** Particle 위치 기반 Light 효과 모듈 */
UCLASS()
class UParticleModuleLight : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleLight)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Light; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Light; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    FColor LightColor = FColor::White(); // Particle Light 색상
    UPROPERTY(Edit, Category="Particle", DisplayName="Intensity", Min=0.0, Max=100000.0, Speed=0.1)
    float  Intensity = 1.0f;           // Particle Light 밝기
    UPROPERTY(Edit, Category="Particle", DisplayName="Radius", Min=0.0, Max=100000.0, Speed=0.1)
    float  Radius = 100.0f;            // Particle Light 영향 반경
};

/** 3D Vector Field 기반 Particle 이동 모듈 */
UCLASS()
class UParticleModuleVectorField : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleVectorField)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_VectorField; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::VectorField; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    UObject *VectorFieldAsset = nullptr; // Vector Field Asset 참조
    UPROPERTY(Edit, Category="Particle", DisplayName="Intensity", Speed=0.1)
    float    Intensity = 1.0f;           // Vector Field 영향 강도
};

/** 카메라 기준 Particle 위치 보정 모듈 */
UCLASS()
class UParticleModuleCamera : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleCamera)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Camera; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Camera; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Camera Offset", Speed=0.1)
    float CameraOffset = 0.0f; // 카메라 방향 기준 위치 보정값
};

/** Material 등 외부 시스템에 값을 전달하는 모듈 */
UCLASS()
class UParticleModuleParameter : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleParameter)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_Parameter; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_Update; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::Parameter; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Parameter Name")
    FName    ParameterName;  // 외부로 전달할 Parameter 이름
    UPROPERTY(Edit, Category="Particle", DisplayName="Parameter Value")
    FVector4 ParameterValue; // Material 등에 전달할 Parameter 값
};
