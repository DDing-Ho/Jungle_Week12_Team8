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

/** Emitter 렌더링 타입을 결정하는 TypeData 기반 클래스 */
class UParticleModuleTypeDataBase : public UParticleModule
{
  public:
    virtual EParticleEmitterType GetEmitterType() const = 0;
};

/** Sprite Emitter TypeData */
class UParticleModuleTypeDataSprite : public UParticleModuleTypeDataBase
{
  public:
    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Sprite; }
};

/** Mesh Particle Emitter TypeData */
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
  public:
    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Mesh; }

    UStaticMesh *GetMesh() const { return Mesh; }
    void         SetMesh(UStaticMesh *InMesh) { Mesh = InMesh; }

  private:
    UStaticMesh *Mesh = nullptr; // Mesh Particle에 사용할 StaticMesh
};

/** Beam Emitter TypeData */
class UParticleModuleTypeDataBeam : public UParticleModuleTypeDataBase
{
  public:
    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Beam; }

  private:
    FVector Source = FVector::ZeroVector; // Beam 시작점
    FVector Target = FVector::ZeroVector; // Beam 끝점
    float   Width = 1.0f;                 // Beam 두께
    float   TextureTiling = 1.0f;         // Beam Texture 반복 비율
};

/** Ribbon / Trail Emitter TypeData */
class UParticleModuleTypeDataRibbon : public UParticleModuleTypeDataBase
{
  public:
    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Ribbon; }

  private:
    float Width = 1.0f;         // Ribbon 폭
    float Lifetime = 1.0f;      // Ribbon Trail 유지 시간
    float TextureTiling = 1.0f; // Ribbon Texture 반복 비율
};
