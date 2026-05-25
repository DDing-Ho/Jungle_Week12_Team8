#include "ParticleRenderData.h"
#include "Particles/Runtime/ParticleRuntimeTypes.h"

// ============================================================
// FDynamicSpriteEmitterData::GatherRenderData
// 활성 파티클마다 per-instance 데이터 1개를 OutInstances에 append.
// 셰이더 측에서 unit quad를 DrawIndexedInstanced로 확장한다.
// ============================================================
void FDynamicSpriteEmitterData::GatherRenderData(
    const FParticleVertexBuildContext& /*Ctx*/,
    TArray<FSpriteParticleInstanceVertex>& OutInstances,
    TArray<uint32>&                        /*OutIndices*/) const
{
    if (!Source.IsValid()) return;

    const int32  Stride  = Source.ParticleStride;
    const uint8* RawData = Source.DataContainer.ParticleData;
    if (!RawData || Stride <= 0) return;

    for (int32 i = 0; i < Source.ActiveParticleCount; ++i)
    {
        const uint16         ParticleIdx = Source.DataContainer.ParticleIndices[i];
        const FBaseParticle* P           = reinterpret_cast<const FBaseParticle*>(RawData + Stride * ParticleIdx);

        // FBaseParticle에 누적 Rotation 필드 없음 → RotationRate * elapsed 근사
        const float Rotation = P->RotationRate * P->RelativeTime * P->Lifetime;

        const FVector Position(
            P->Location.X * Source.Scale.X,
            P->Location.Y * Source.Scale.Y,
            P->Location.Z * Source.Scale.Z);

        const FVector4 Color(
            P->Color.R / 255.f,
            P->Color.G / 255.f,
            P->Color.B / 255.f,
            P->Color.A / 255.f);

        FSpriteParticleInstanceVertex Instance;
        Instance.Position = Position;
        Instance.Rotation = Rotation;
        Instance.Size = FVector4(P->Size.X * Source.Scale.X, P->Size.Y * Source.Scale.Y, 0.0f, 0.0f);
        Instance.Color = Color;
        OutInstances.push_back(Instance);
    }
}

// TODO
// ============================================================
// FDynamicMeshEmitterData::GatherRenderData
// Mesh Particle: StaticMesh 인스턴스 행렬 기반 렌더링
// ============================================================
void FDynamicMeshEmitterData::GatherRenderData(
    const FParticleVertexBuildContext& /*Ctx*/,
    TArray<FSpriteParticleInstanceVertex>& /*OutInstances*/,
    TArray<uint32>&                        /*OutIndices*/) const
{
    // 미구현 — Mesh Particle은 FMeshParticleInstanceVertex 기반 별도 인스턴싱 패스가 필요
}

// TODO
// ============================================================
// FDynamicBeamEmitterData::GatherRenderData
// Beam Particle: Source → Target 사이 ribbon strip 생성
// ============================================================
void FDynamicBeamEmitterData::GatherRenderData(
    const FParticleVertexBuildContext& /*Ctx*/,
    TArray<FSpriteParticleInstanceVertex>& /*OutInstances*/,
    TArray<uint32>&                        /*OutIndices*/) const
{
    // 미구현 — Beam 경로 분할 및 width 처리 필요
}

// TODO
// ============================================================
// FDynamicRibbonEmitterData::GatherRenderData
// Ribbon Particle: 파티클 경로를 따라 strip 생성
// ============================================================
void FDynamicRibbonEmitterData::GatherRenderData(
    const FParticleVertexBuildContext& /*Ctx*/,
    TArray<FSpriteParticleInstanceVertex>& /*OutInstances*/,
    TArray<uint32>&                        /*OutIndices*/) const
{
    // 미구현 — 파티클 순서 보존 및 width 처리 필요
}
