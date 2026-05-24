/**
 * @file ParticleRenderExpressionModules.h
 * @brief Particle 렌더링 표현 확장 Module 정의.
 *
 * 포함 클래스:
 * - UParticleModuleSubUV: Texture Atlas / Flipbook 표현
 * - UParticleModuleLight: Particle 기반 Light 효과
 * - UParticleModuleVectorField: Vector Field 기반 이동
 * - UParticleModuleCamera: 카메라 기준 보정
 * - UParticleModuleParameter: Material 등 외부 Parameter 전달
 */

#pragma once

#include "ParticleEventModules.h"
#include "ParticleRenderExpressionModules.generated.h"

/** Texture Atlas 기반 Flipbook 애니메이션 모듈 */
UCLASS()
class UParticleModuleSubUV : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleSubUV)

    virtual EParticleModuleType        GetModuleType() const override { return EParticleModuleType::PMT_SubUV; }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override { return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate; }
    virtual EParticleModuleClass       GetModuleClass() const override { return EParticleModuleClass::SubUV; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    int32 HorizontalCount = 1;       // Texture Atlas 가로 프레임 수
    int32 VerticalCount = 1;         // Texture Atlas 세로 프레임 수
    int32 StartFrame = 0;            // 시작 SubUV 프레임
    int32 EndFrame = 0;              // 종료 SubUV 프레임
    bool  bUseSubImageIndex = false; // 직접 프레임 인덱스 사용 여부
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
    float  Intensity = 1.0f;           // Particle Light 밝기
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
    FName    ParameterName;  // 외부로 전달할 Parameter 이름
    FVector4 ParameterValue; // Material 등에 전달할 Parameter 값
};
