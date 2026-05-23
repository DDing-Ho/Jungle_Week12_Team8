/**
 * @file ParticleRenderData.h
 * @brief Particle 렌더링 전달 데이터 정의.
 *
 * 포함 타입:
 * - FDynamicEmitterReplayDataBase: Emitter 렌더링 스냅샷 base
 * - FDynamicSpriteEmitterReplayDataBase: Sprite Emitter ReplayData
 * - FDynamicEmitterDataBase: 렌더 패스 전달용 Dynamic Emitter Data base
 * - FDynamicSpriteEmitterDataBase: Sprite Emitter 렌더링 base
 * - FDynamicSpriteEmitterData: Sprite Emitter 렌더링 데이터
 * - FDynamicMeshEmitterData: Mesh Emitter 렌더링 데이터
 * - FDynamicBeamEmitterData: Beam Emitter 렌더링 데이터
 * - FDynamicRibbonEmitterData: Ribbon Emitter 렌더링 데이터
 */

#pragma once

#include "ParticleVertexTypes.h"

/** 렌더링에 전달할 Emitter 공통 ReplayData */
struct FDynamicEmitterReplayDataBase
{
    EDynamicEmitterType    eEmitterType = EDynamicEmitterType::Sprite; // Emitter 렌더링 타입
    int32                  ActiveParticleCount = 0;                    // 활성 Particle 수
    int32                  ParticleStride = 0;                         // Particle 메모리 간격
    FParticleDataContainer DataContainer;                              // 렌더링용 Particle 데이터
    FVector3f              Scale = FVector3f(1.0f);                    // Emitter 스케일
    int32                  SortMode = 0;                               // 정렬 방식

    bool IsValid() const { return ActiveParticleCount > 0 && DataContainer.ParticleData != nullptr; }
};

/** Sprite Emitter 렌더링용 ReplayData */
struct FDynamicSpriteEmitterReplayDataBase : public FDynamicEmitterReplayDataBase
{
    UMaterialInterface      *MaterialInterface = nullptr; // Sprite Material
    UParticleModuleRequired *RequiredModule = nullptr;    // Required Module 참조
};

/** 렌더 패스에 전달되는 동적 Emitter 데이터 기반 구조 */
struct FDynamicEmitterDataBase
{
    int32 EmitterIndex = INDEX_NONE; // ParticleSystem 내부 Emitter 인덱스

    virtual const FDynamicEmitterReplayDataBase &GetSource() const = 0;
    virtual void                                 BuildRenderData() {}
    virtual int32                                GetDynamicVertexStride() const = 0;
    virtual ~FDynamicEmitterDataBase() = default;
};

/** Sprite Emitter 동적 렌더 데이터 기반 구조 */
struct FDynamicSpriteEmitterDataBase : public FDynamicEmitterDataBase
{
    virtual void  SortSpriteParticles(const FVector &ViewLocation) {} // Sprite Particle 정렬
    virtual int32 GetDynamicVertexStride() const override = 0;
};

/** Sprite Particle 렌더링 데이터 */
struct FDynamicSpriteEmitterData : public FDynamicSpriteEmitterDataBase
{
    FDynamicSpriteEmitterReplayDataBase Source; // Sprite ReplayData

    virtual const FDynamicEmitterReplayDataBase &GetSource() const override { return Source; }
    virtual int32                                GetDynamicVertexStride() const override { return sizeof(FParticleSpriteVertex); }
};

/** Mesh Particle 렌더링 데이터 */
struct FDynamicMeshEmitterData : public FDynamicSpriteEmitterData
{
    virtual int32 GetDynamicVertexStride() const override { return sizeof(FMeshParticleInstanceVertex); }
};

/** Beam Emitter 렌더링 데이터 */
struct FDynamicBeamEmitterData : public FDynamicSpriteEmitterData
{
    virtual int32 GetDynamicVertexStride() const override { return sizeof(FParticleSpriteVertex); }
};

/** Ribbon Emitter 렌더링 데이터 */
struct FDynamicRibbonEmitterData : public FDynamicSpriteEmitterData
{
    virtual int32 GetDynamicVertexStride() const override { return sizeof(FParticleSpriteVertex); }
};
