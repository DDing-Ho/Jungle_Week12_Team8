/**
 * @file ParticleRenderData.h
 * @brief Particle 렌더링 전달 데이터 정의.
 *
 * 포함 타입:
 * - FDynamicEmitterReplayDataBase: Emitter 공통 렌더링 스냅샷
 * - FDynamicSpriteEmitterReplayData: Sprite Emitter ReplayData
 * - FDynamicMeshEmitterReplayData: Mesh Emitter ReplayData
 * - FDynamicBeamEmitterReplayData: Beam Emitter ReplayData
 * - FDynamicRibbonEmitterReplayData: Ribbon Emitter ReplayData
 * - FDynamicEmitterDataBase: 렌더 패스 전달용 Dynamic Emitter Data base
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
    EDynamicEmitterType eEmitterType = EDynamicEmitterType::DET_Sprite; // Emitter 렌더링 타입
    int32 ActiveParticleCount = 0;                                      // 활성 Particle 수
    int32 ParticleStride = 0;                                           // Particle 메모리 간격
    FParticleDataContainer DataContainer;                               // 렌더링용 Particle 데이터
    FVector Scale = FVector(1.0f, 1.0f, 1.0f);                           // Emitter 스케일
    EParticleSortMode SortMode = EParticleSortMode::PSM_None;           // 정렬 방식

    UMaterial *Material = nullptr;                     // Emitter 렌더링 Material
    UParticleModuleRequired *RequiredModule = nullptr; // Required Module 참조

    bool IsValid() const
    {
        return ActiveParticleCount > 0 && DataContainer.ParticleData != nullptr;
    }
};

/** Sprite Emitter 렌더링용 ReplayData */
struct FDynamicSpriteEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    int32 SubImageIndex = 0; // SubUV 프레임 인덱스
};

/** Mesh Emitter 렌더링용 ReplayData */
struct FDynamicMeshEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    UStaticMesh *Mesh = nullptr; // Mesh Particle 렌더링 Mesh
};

/** Beam Emitter 렌더링용 ReplayData */
struct FDynamicBeamEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    FVector Source;     // Beam 시작점
    FVector Target;     // Beam 끝점
    float Width = 1.0f; // Beam 두께
};

/** Ribbon Emitter 렌더링용 ReplayData */
struct FDynamicRibbonEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    float Width = 1.0f; // Ribbon 폭
};

/** 렌더 패스에 전달되는 동적 Emitter 데이터 기반 구조 */
struct FDynamicEmitterDataBase
{
    int32 EmitterIndex = INDEX_NONE; // ParticleSystem 내부 Emitter 인덱스

    virtual const FDynamicEmitterReplayDataBase &GetSource() const = 0;
    virtual void BuildRenderData()
    {
    }
    virtual int32 GetDynamicVertexStride() const = 0;
    virtual ~FDynamicEmitterDataBase() = default;
};

/** Sprite Particle 렌더링 데이터 */
struct FDynamicSpriteEmitterData : public FDynamicEmitterDataBase
{
    FDynamicSpriteEmitterReplayData Source; // Sprite ReplayData

    virtual const FDynamicEmitterReplayDataBase &GetSource() const override
    {
        return Source;
    }
    virtual int32 GetDynamicVertexStride() const override
    {
        return sizeof(FParticleSpriteVertex);
    }
};

/** Mesh Particle 렌더링 데이터 */
struct FDynamicMeshEmitterData : public FDynamicEmitterDataBase
{
    FDynamicMeshEmitterReplayData Source; // Mesh ReplayData

    virtual const FDynamicEmitterReplayDataBase &GetSource() const override
    {
        return Source;
    }
    virtual int32 GetDynamicVertexStride() const override
    {
        return sizeof(FMeshParticleInstanceVertex);
    }
};

/** Beam Emitter 렌더링 데이터 */
struct FDynamicBeamEmitterData : public FDynamicEmitterDataBase
{
    FDynamicBeamEmitterReplayData Source; // Beam ReplayData

    virtual const FDynamicEmitterReplayDataBase &GetSource() const override
    {
        return Source;
    }
    virtual int32 GetDynamicVertexStride() const override
    {
        return sizeof(FParticleSpriteVertex);
    }
};

/** Ribbon Emitter 렌더링 데이터 */
struct FDynamicRibbonEmitterData : public FDynamicEmitterDataBase
{
    FDynamicRibbonEmitterReplayData Source; // Ribbon ReplayData

    virtual const FDynamicEmitterReplayDataBase &GetSource() const override
    {
        return Source;
    }
    virtual int32 GetDynamicVertexStride() const override
    {
        return sizeof(FParticleSpriteVertex);
    }
};
