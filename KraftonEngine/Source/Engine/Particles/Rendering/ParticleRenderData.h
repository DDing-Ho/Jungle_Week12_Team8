/**
 * @file ParticleRenderData.h
 * @brief Particle 렌더링 전달 데이터 정의.
 *
 * 포함 타입:
 * - FParticleVertexBuildContext: 카메라 정보 전달용 렌더 컨텍스트
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

/** UpdatePerViewport → GatherRenderData 로 전달되는 카메라 정보 */
struct FParticleVertexBuildContext
{
    FVector CameraPosition;
    FVector CamRight;
    FVector CamUp;
    FVector CamForward;
};

/** 렌더링에 전달할 Emitter 공통 ReplayData */
struct FDynamicEmitterReplayDataBase
{
    EDynamicEmitterType eEmitterType = EDynamicEmitterType::DET_Sprite; // Emitter 렌더링 타입
    int32 ActiveParticleCount = 0;                                      // 활성 Particle 수
    int32 ParticleStride = 0;                                           // Particle 메모리 간격
    int32 RotationModuleOffset = INDEX_NONE;                            // Rotation module payload offset
    FParticleDataContainer DataContainer;                               // 렌더링용 Particle 데이터
    FVector Scale = FVector(1.0f, 1.0f, 1.0f);                           // Emitter 스케일
    EParticleSortMode SortMode = EParticleSortMode::PSM_None;           // 정렬 방식
    int32 TranslucencySortPriority = 0;                                  // Emitter 단위 반투명 정렬 우선순위

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
    bool  bUseSubUV = false;        // Texture atlas 사용 여부
    int32 SubUVHorizontalCount = 1;  // Texture atlas 가로 프레임 수
    int32 SubUVVerticalCount = 1;    // Texture atlas 세로 프레임 수
    EParticleSubUVInterpMethod SubUVInterpolationMethod = EParticleSubUVInterpMethod::PSUVIM_None;
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
    float TextureTiling = 1.0f; // Beam Texture 반복 비율
    int32 PayloadOffset = sizeof(FBaseParticle); // FBeamParticlePayload 위치
};

/** Ribbon Emitter 렌더링용 ReplayData */
struct FDynamicRibbonEmitterReplayData : public FDynamicEmitterReplayDataBase
{
    int32 SheetsPerTrail = 1;
    int32 MaxTrailCount = 1;
    int32 MaxParticleInTrailCount = 256;
    EParticleRibbonRenderAxis RenderAxis = EParticleRibbonRenderAxis::CameraUp;
    float TilingDistance = 100.0f;
    float DistanceTessellationStepSize = 0.0f;
    bool bRenderGeometry = true;
    bool bUseSourceEmitter = false;
    FVector SourceUp = FVector::UpVector;
};

/** 렌더 패스에 전달되는 동적 Emitter 데이터 기반 구조 */
struct FDynamicEmitterDataBase
{
    int32 EmitterIndex = INDEX_NONE; // ParticleSystem 내부 Emitter 인덱스

    virtual const FDynamicEmitterReplayDataBase &GetSource() const = 0;
    virtual void BuildRenderData()
    {
    }
    virtual ~FDynamicEmitterDataBase() = default;

    // ============================================================
    // GPU 렌더 데이터 조립 — 타입별 서브클래스가 오버라이드
    // Sprite는 OutInstances에 파티클당 인스턴스 1개만 append한다.
    // Mesh/Beam/Ribbon은 별도 렌더링 경로에서 확장할 예정이다.
    // ============================================================
    virtual void GatherRenderData(const FParticleVertexBuildContext &Ctx,
                                      TArray<FSpriteParticleInstanceVertex> &OutInstances,
                                      TArray<uint32> &OutIndices) const
    {
    }

    virtual void GatherRenderData(const FParticleVertexBuildContext &Ctx,
                                      TArray<FMeshParticleInstanceVertex> &OutInstances) const
    {
    }

    virtual void GatherRibbonRenderData(const FParticleVertexBuildContext& Ctx,
                                        TArray<FRibbonParticleVertex>& OutVertices,
                                        TArray<uint32>& OutIndices) const
    {
    }
};

/** Sprite Particle 렌더링 데이터 */
struct FDynamicSpriteEmitterData : public FDynamicEmitterDataBase
{
    FDynamicSpriteEmitterReplayData Source; // Sprite ReplayData

    ~FDynamicSpriteEmitterData() override
    {
        Source.DataContainer.Release();
    }

    const FDynamicEmitterReplayDataBase &GetSource() const override
    {
        return Source;
    }
    void GatherRenderData(const FParticleVertexBuildContext &Ctx,
                              TArray<FSpriteParticleInstanceVertex> &OutInstances,
                              TArray<uint32> &OutIndices) const override;
};

/** Mesh Particle 렌더링 데이터 */
struct FDynamicMeshEmitterData : public FDynamicEmitterDataBase
{
    FDynamicMeshEmitterReplayData Source; // Mesh ReplayData

    ~FDynamicMeshEmitterData() override
    {
        Source.DataContainer.Release();
    }

    const FDynamicEmitterReplayDataBase &GetSource() const override
    {
        return Source;
    }
    // Mesh Particle: 별도 인스턴싱 패스
    void GatherRenderData(const FParticleVertexBuildContext &Ctx,
                              TArray<FSpriteParticleInstanceVertex> &OutInstances,
                              TArray<uint32> &OutIndices) const override;
    void GatherRenderData(const FParticleVertexBuildContext &Ctx,
                              TArray<FMeshParticleInstanceVertex> &OutInstances) const override;
};

/** Beam Emitter 렌더링 데이터 */
struct FDynamicBeamEmitterData : public FDynamicEmitterDataBase
{
    FDynamicBeamEmitterReplayData Source; // Beam ReplayData

    ~FDynamicBeamEmitterData() override
    {
        Source.DataContainer.Release();
    }

    const FDynamicEmitterReplayDataBase &GetSource() const override
    {
        return Source;
    }
    // Source → Target 사이 빔 quad 생성
    void GatherRenderData(const FParticleVertexBuildContext &Ctx,
                              TArray<FSpriteParticleInstanceVertex> &OutInstances,
                              TArray<uint32> &OutIndices) const override;
};

/** Ribbon Emitter 렌더링 데이터 */
struct FDynamicRibbonEmitterData : public FDynamicEmitterDataBase
{
    FDynamicRibbonEmitterReplayData Source; // Ribbon ReplayData

    ~FDynamicRibbonEmitterData() override
    {
        Source.DataContainer.Release();
    }

    const FDynamicEmitterReplayDataBase &GetSource() const override
    {
        return Source;
    }
    // 파티클 경로 따라 ribbon strip 생성
    void GatherRibbonRenderData(const FParticleVertexBuildContext& Ctx,
                                TArray<FRibbonParticleVertex>& OutVertices,
                                TArray<uint32>& OutIndices) const override;
};
