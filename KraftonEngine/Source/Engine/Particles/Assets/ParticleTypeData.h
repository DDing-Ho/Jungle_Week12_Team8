/**
 * @file ParticleTypeData.h
 * @brief Emitter 렌더링 타입을 결정하는 TypeData 정의.
 *
 * 포함 클래스:
 * - UParticleModuleTypeDataBase: TypeData base class
 * - UParticleModuleTypeDataSprite: Sprite Emitter 타입
 * - UParticleModuleTypeDataMesh: Mesh Emitter 타입
 * - UParticleModuleTypeDataBeam: Beam Emitter 타입
 * - UParticleModuleTypeDataRibbon: Ribbon / Trail Emitter 타입
 */

#pragma once

#include "../Modules/ParticleRenderExpressionModules.h"
#include "ParticleTypeData.generated.h"

/** Emitter 렌더링 타입을 결정하는 TypeData 기반 클래스 */
UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataBase)

    virtual EParticleEmitterType GetEmitterType() const = 0;
    virtual EParticleModuleClass GetModuleClass() const override = 0;
    virtual void Serialize(FArchive& Ar) override = 0;
};

/** Sprite Emitter TypeData */
UCLASS()
class UParticleModuleTypeDataSprite : public UParticleModuleTypeDataBase
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataSprite)

    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Sprite; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::TypeDataSprite; }
    virtual void Serialize(FArchive& Ar) override;
};

/** Mesh Particle Emitter TypeData */
UCLASS()
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataMesh)

    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Mesh; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::TypeDataMesh; }
    virtual void Serialize(FArchive& Ar) override;

    UStaticMesh *GetMesh() const { return Mesh; }
    void         SetMesh(UStaticMesh *InMesh) { Mesh = InMesh; }

  private:
    UStaticMesh *Mesh = nullptr;      // Mesh Particle에 사용할 StaticMesh
    FString      MeshPath;            // Serialize 시 경로 저장용
};

/** Beam Emitter TypeData */
UCLASS()
class UParticleModuleTypeDataBeam : public UParticleModuleTypeDataBase
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataBeam)

    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Beam; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::TypeDataBeam; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    FVector Source = FVector::ZeroVector; // Beam 시작점
    FVector Target = FVector::ZeroVector; // Beam 끝점
    float   Width = 1.0f;                 // Beam 두께
    float   TextureTiling = 1.0f;         // Beam Texture 반복 비율
};

/** Ribbon / Trail Emitter TypeData */
UCLASS()
class UParticleModuleTypeDataRibbon : public UParticleModuleTypeDataBase
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataRibbon)

    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Ribbon; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::TypeDataRibbon; }
    virtual void Serialize(FArchive& Ar) override;

  private:
    float Width = 1.0f;         // Ribbon 폭
    float Lifetime = 1.0f;      // Ribbon Trail 유지 시간
    float TextureTiling = 1.0f; // Ribbon Texture 반복 비율
};
