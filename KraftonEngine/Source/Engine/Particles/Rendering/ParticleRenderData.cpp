#include "ParticleRenderData.h"
#include "Particles/Runtime/ParticleRuntimeTypes.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr int32 RotationModuleRotationOffset = 0;
	constexpr int32 RotationModuleMeshRotationOffset = sizeof(float);
	constexpr int32 RotationModuleDataSize = sizeof(float) + sizeof(FVector);
	constexpr float RibbonEpsilon = 1.0e-4f;

	const uint8* GetModuleData(const FDynamicEmitterReplayDataBase& Source, const FBaseParticle* Particle, int32 ModuleOffset, int32 RequiredBytes)
	{
		if (!Particle || ModuleOffset == INDEX_NONE || ModuleOffset < 0)
			return nullptr;

		if (ModuleOffset + RequiredBytes > Source.ParticleStride)
			return nullptr;

		return reinterpret_cast<const uint8*>(Particle) + ModuleOffset;
	}

	float ReadRotationModuleValue(const FDynamicEmitterReplayDataBase& Source, const FBaseParticle* Particle)
	{
		const uint8* ModuleData = GetModuleData(Source, Particle, Source.RotationModuleOffset, RotationModuleDataSize);
		return ModuleData ? *reinterpret_cast<const float*>(ModuleData + RotationModuleRotationOffset) : 0.0f;
	}

	FVector ReadRotationModuleMeshValue(const FDynamicEmitterReplayDataBase& Source, const FBaseParticle* Particle)
	{
		const uint8* ModuleData = GetModuleData(Source, Particle, Source.RotationModuleOffset, RotationModuleDataSize);
		return ModuleData ? *reinterpret_cast<const FVector*>(ModuleData + RotationModuleMeshRotationOffset) : FVector::ZeroVector;
	}

	FVector SafeNormalized(const FVector& Value, const FVector& Fallback)
	{
		return Value.IsNearlyZero(RibbonEpsilon) ? Fallback : Value.Normalized();
	}

	FVector SelectRibbonAxis(const FDynamicRibbonEmitterReplayData& Source, const FParticleVertexBuildContext& Ctx)
	{
		switch (Source.RenderAxis)
		{
		case EParticleRibbonRenderAxis::WorldUp:  return FVector::UpVector;
		case EParticleRibbonRenderAxis::SourceUp: return SafeNormalized(Source.SourceUp, FVector::UpVector);
		case EParticleRibbonRenderAxis::CameraUp:
		default:                                 return SafeNormalized(Ctx.CamUp, FVector::UpVector);
		}
	}

	FVector BuildRibbonSideVector(
		const FDynamicRibbonEmitterReplayData& Source,
		const FParticleVertexBuildContext& Ctx,
		const FVector& Tangent,
		int32 SheetIndex,
		int32 SheetCount)
	{
		const FVector Axis = SelectRibbonAxis(Source, Ctx);
		FVector Side = SafeNormalized(Axis.Cross(Tangent), SafeNormalized(Ctx.CamRight, FVector::RightVector));
		if (Side.IsNearlyZero(RibbonEpsilon))
		{
			Side = SafeNormalized(Ctx.CamRight.Cross(Tangent), FVector::RightVector);
		}

		if (SheetCount <= 1)
		{
			return Side;
		}

		FVector Binormal = SafeNormalized(Tangent.Cross(Side), Axis);
		const float Angle = 3.1415926535f * static_cast<float>(SheetIndex) / static_cast<float>(SheetCount);
		return SafeNormalized(Side * std::cos(Angle) + Binormal * std::sin(Angle), Side);
	}

	FVector4 LerpColor(const FVector4& A, const FVector4& B, float Alpha)
	{
		const float InvAlpha = 1.0f - Alpha;
		return FVector4(
			A.X * InvAlpha + B.X * Alpha,
			A.Y * InvAlpha + B.Y * Alpha,
			A.Z * InvAlpha + B.Z * Alpha,
			A.W * InvAlpha + B.W * Alpha);
	}

	struct FRibbonBuildPoint
	{
		FVector Position;
		FVector4 Color;
		float Width = 1.0f;
		uint32 SpawnId = 0;
		uint32 SourceSpawnId = 0;
	};

	struct FRibbonSamplePoint
	{
		FVector Position;
		FVector4 Color;
		float Width = 1.0f;
		float Distance = 0.0f;
	};
}

// ============================================================
// FDynamicSpriteEmitterData::GatherRenderData
// 활성 파티클마다 per-instance 데이터 1개를 OutInstances에 append.
// 셰이더 측에서 unit quad를 DrawIndexedInstanced로 확장한다.
// ============================================================
void FDynamicSpriteEmitterData::GatherRenderData(
    const FParticleVertexBuildContext& Ctx,
    TArray<FSpriteParticleInstanceVertex>& OutInstances,
    TArray<uint32>&                        /*OutIndices*/) const
{
    if (!Source.IsValid()) return;

    const int32  Stride  = Source.ParticleStride;
    const uint8* RawData = Source.DataContainer.ParticleData;
    if (!RawData || !Source.DataContainer.ParticleIndices || Stride <= 0) return;

    auto AppendParticleInstance = [&](uint16 ParticleIdx)
    {
        const FBaseParticle* P           = reinterpret_cast<const FBaseParticle*>(RawData + Stride * ParticleIdx);

        const float Rotation = ReadRotationModuleValue(Source, P) + P->RotationRate * P->RelativeTime * P->Lifetime;

        const FVector Position(
            P->Location.X * Source.Scale.X,
            P->Location.Y * Source.Scale.Y,
            P->Location.Z * Source.Scale.Z);

        const FVector4 Color(
            P->Color.R / 255.f,
            P->Color.G / 255.f,
            P->Color.B / 255.f,
            P->Color.A / 255.f);

        FVector4 UVRegionA(0.0f, 0.0f, 1.0f, 1.0f);
        FVector4 UVRegionB(0.0f, 0.0f, 1.0f, 1.0f);
        float SubUVLerp = 0.0f;
        if (Source.bUseSubUV)
        {
            const int32 Columns = (std::max)(1, Source.SubUVHorizontalCount);
            const int32 Rows = (std::max)(1, Source.SubUVVerticalCount);
            const int32 TotalFrames = Columns * Rows;
            const float FrameWidth = 1.0f / static_cast<float>(Columns);
            const float FrameHeight = 1.0f / static_cast<float>(Rows);

            auto FrameToRegion = [&](float InFrame)
            {
                const int32 FrameIndex = (std::clamp)(static_cast<int32>(std::floor(InFrame)), 0, TotalFrames - 1);
                const int32 Column = FrameIndex % Columns;
                const int32 Row = FrameIndex / Columns;
                return FVector4(
                    static_cast<float>(Column) * FrameWidth,
                    static_cast<float>(Row) * FrameHeight,
                    FrameWidth,
                    FrameHeight);
            };

            UVRegionA = FrameToRegion(P->SubUVFrameA);
            UVRegionB = FrameToRegion(P->SubUVFrameB);
            SubUVLerp = (std::clamp)(P->SubUVLerp, 0.0f, 1.0f);
        }

        FSpriteParticleInstanceVertex Instance;
        Instance.Position = Position;
        Instance.Rotation = Rotation;
        Instance.Size = FVector4(P->Size.X * Source.Scale.X, P->Size.Y * Source.Scale.Y, 0.0f, 0.0f);
        Instance.Color = Color;
        Instance.UVRegionA = UVRegionA;
        Instance.UVRegionB = UVRegionB;
        Instance.SubUVLerp = SubUVLerp;
        OutInstances.push_back(Instance);
    };

    if (Source.SortMode == EParticleSortMode::PSM_None)
    {
        for (int32 i = 0; i < Source.ActiveParticleCount; ++i)
        {
            AppendParticleInstance(Source.DataContainer.ParticleIndices[i]);
        }
        return;
    }

    struct FParticleSortEntry
    {
        uint16 ParticleIdx = 0;
        float  DistanceSq = 0.0f;
        float  RelativeTime = 0.0f;
        int32  OriginalOrder = 0;
    };

    TArray<FParticleSortEntry> SortEntries;
    SortEntries.reserve(Source.ActiveParticleCount);

    for (int32 i = 0; i < Source.ActiveParticleCount; ++i)
    {
        const uint16 ParticleIdx = Source.DataContainer.ParticleIndices[i];
        const FBaseParticle* P = reinterpret_cast<const FBaseParticle*>(RawData + Stride * ParticleIdx);
        const FVector Position(
            P->Location.X * Source.Scale.X,
            P->Location.Y * Source.Scale.Y,
            P->Location.Z * Source.Scale.Z);

        SortEntries.push_back({
            ParticleIdx,
            FVector::DistSquared(Position, Ctx.CameraPosition),
            P->RelativeTime,
            i
        });
    }

    if (Source.SortMode == EParticleSortMode::PSM_Age)
    {
        std::ranges::stable_sort(SortEntries,
                                 [](const FParticleSortEntry& A, const FParticleSortEntry& B)
                                 {
	                                 if (A.RelativeTime != B.RelativeTime)
	                                 {
		                                 return A.RelativeTime > B.RelativeTime;
	                                 }
	                                 return A.OriginalOrder < B.OriginalOrder;
                                 });
    }
    else
    {
        std::ranges::stable_sort(SortEntries,
                                 [](const FParticleSortEntry& A, const FParticleSortEntry& B)
                                 {
	                                 if (A.DistanceSq != B.DistanceSq)
	                                 {
		                                 return A.DistanceSq > B.DistanceSq;
	                                 }
	                                 return A.OriginalOrder < B.OriginalOrder;
                                 });
    }

    for (const FParticleSortEntry& Entry : SortEntries)
    {
        AppendParticleInstance(Entry.ParticleIdx);
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
    // Mesh Particle uses the mesh-instance overload below.
}

void FDynamicMeshEmitterData::GatherRenderData(
    const FParticleVertexBuildContext& /*Ctx*/,
    TArray<FMeshParticleInstanceVertex>& OutInstances) const
{
    if (!Source.IsValid() || !Source.Mesh) return;

    const int32 Stride = Source.ParticleStride;
    const uint8* RawData = Source.DataContainer.ParticleData;
    if (!RawData || Stride <= 0) return;

    OutInstances.reserve(OutInstances.size() + Source.ActiveParticleCount);

    for (int32 i = 0; i < Source.ActiveParticleCount; ++i)
    {
        const uint16 ParticleIdx = Source.DataContainer.ParticleIndices[i];
        const FBaseParticle* P = reinterpret_cast<const FBaseParticle*>(RawData + Stride * ParticleIdx);

        const FVector Position(
            P->Location.X * Source.Scale.X,
            P->Location.Y * Source.Scale.Y,
            P->Location.Z * Source.Scale.Z);

        const FVector Scale(
            P->Size.X * Source.Scale.X,
            P->Size.Y * Source.Scale.Y,
            P->Size.Z * Source.Scale.Z);

        const float Rotation = ReadRotationModuleValue(Source, P) + P->RotationRate * P->RelativeTime * P->Lifetime;
        FVector MeshRotation = ReadRotationModuleMeshValue(Source, P);
        MeshRotation.Z += Rotation * 57.295779513f;

        FMeshParticleInstanceVertex Instance;
        Instance.Transform = FMatrix::MakeScaleMatrix(Scale)
            * FMatrix::MakeRotationEuler(MeshRotation)
            * FMatrix::MakeTranslationMatrix(Position);
        Instance.Color = FVector4(
            P->Color.R / 255.0f,
            P->Color.G / 255.0f,
            P->Color.B / 255.0f,
            P->Color.A / 255.0f);

        OutInstances.push_back(Instance);
    }
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

// ============================================================
// FDynamicRibbonEmitterData::GatherRenderData
// Ribbon Particle: 파티클 경로를 따라 strip 생성
// ============================================================
void FDynamicRibbonEmitterData::GatherRibbonRenderData(
    const FParticleVertexBuildContext& Ctx,
    TArray<FRibbonParticleVertex>& OutVertices,
    TArray<uint32>& OutIndices) const
{
    if (!Source.IsValid() || !Source.bRenderGeometry)
        return;

    const int32 Stride = Source.ParticleStride;
    const uint8* RawData = Source.DataContainer.ParticleData;
    if (!RawData || !Source.DataContainer.ParticleIndices || Stride <= 0)
        return;

    TArray<FRibbonBuildPoint> Points;
    Points.reserve(Source.ActiveParticleCount);

    for (int32 i = 0; i < Source.ActiveParticleCount; ++i)
    {
        const uint16 ParticleIdx = Source.DataContainer.ParticleIndices[i];
        const FBaseParticle* P = reinterpret_cast<const FBaseParticle*>(RawData + Stride * ParticleIdx);
        if (!P)
            continue;

        FRibbonBuildPoint Point;
        Point.Position = FVector(
            P->Location.X * Source.Scale.X,
            P->Location.Y * Source.Scale.Y,
            P->Location.Z * Source.Scale.Z);
        Point.Color = FVector4(
            P->Color.R / 255.0f,
            P->Color.G / 255.0f,
            P->Color.B / 255.0f,
            P->Color.A / 255.0f);
        Point.Width = (std::max)(0.001f, std::abs(P->Size.X * Source.Scale.X));
        Point.SpawnId = P->SpawnId;
        Point.SourceSpawnId = Source.bUseSourceEmitter ? P->SourceSpawnId : 0u;
        Points.push_back(Point);
    }

    if (Points.size() < 2)
        return;

    std::stable_sort(Points.begin(), Points.end(),
        [](const FRibbonBuildPoint& A, const FRibbonBuildPoint& B)
        {
            if (A.SourceSpawnId != B.SourceSpawnId)
                return A.SourceSpawnId < B.SourceSpawnId;
            return A.SpawnId < B.SpawnId;
        });

    const int32 MaxTrailCount = (std::max)(1, Source.MaxTrailCount);
    const int32 MaxParticlesPerTrail = (std::max)(2, Source.MaxParticleInTrailCount);
    const int32 SheetCount = (std::max)(1, Source.SheetsPerTrail);
    int32 TrailCount = 0;

    int32 GroupBegin = 0;
    while (GroupBegin < static_cast<int32>(Points.size()) && TrailCount < MaxTrailCount)
    {
        int32 GroupEnd = GroupBegin + 1;
        while (GroupEnd < static_cast<int32>(Points.size()) &&
               Points[GroupEnd].SourceSpawnId == Points[GroupBegin].SourceSpawnId)
        {
            ++GroupEnd;
        }

        const int32 GroupCount = GroupEnd - GroupBegin;
        if (GroupCount >= 2)
        {
            const int32 TrailBegin = GroupBegin + (std::max)(0, GroupCount - MaxParticlesPerTrail);
            TArray<FRibbonSamplePoint> Samples;
            Samples.reserve(GroupEnd - TrailBegin);

            float AccumulatedDistance = 0.0f;
            for (int32 PointIndex = TrailBegin; PointIndex < GroupEnd; ++PointIndex)
            {
                const FRibbonBuildPoint& Current = Points[PointIndex];

                if (Samples.empty())
                {
                    Samples.push_back({ Current.Position, Current.Color, Current.Width, 0.0f });
                    continue;
                }

                FRibbonSamplePoint Prev = Samples.back();
                const float SegmentLength = FVector::Distance(Prev.Position, Current.Position);
                const float StepSize = Source.DistanceTessellationStepSize;
                const int32 SegmentSteps = StepSize > RibbonEpsilon
                    ? (std::max)(1, static_cast<int32>(std::ceil(SegmentLength / StepSize)))
                    : 1;

                for (int32 Step = 1; Step <= SegmentSteps; ++Step)
                {
                    const float Alpha = static_cast<float>(Step) / static_cast<float>(SegmentSteps);
                    const FVector Pos = FVector::Lerp(Prev.Position, Current.Position, Alpha);
                    const FVector4 Color = LerpColor(Prev.Color, Current.Color, Alpha);
                    const float Width = Prev.Width * (1.0f - Alpha) + Current.Width * Alpha;
                    const float StepDistance = SegmentLength / static_cast<float>(SegmentSteps);
                    AccumulatedDistance += StepDistance;
                    Samples.push_back({ Pos, Color, Width, AccumulatedDistance });
                }
            }

            if (Samples.size() >= 2)
            {
                for (int32 SheetIndex = 0; SheetIndex < SheetCount; ++SheetIndex)
                {
                    const uint32 VertexBase = static_cast<uint32>(OutVertices.size());
                    for (int32 SampleIndex = 0; SampleIndex < static_cast<int32>(Samples.size()); ++SampleIndex)
                    {
                        const FRibbonSamplePoint& Sample = Samples[SampleIndex];
                        const FVector Prev = Samples[(std::max)(0, SampleIndex - 1)].Position;
                        const FVector Next = Samples[(std::min)(static_cast<int32>(Samples.size()) - 1, SampleIndex + 1)].Position;
                        const FVector Tangent = SafeNormalized(Next - Prev, FVector::ForwardVector);
                        const FVector Side = BuildRibbonSideVector(Source, Ctx, Tangent, SheetIndex, SheetCount);
                        const float HalfWidth = Sample.Width * 0.5f;
                        const float U = Source.TilingDistance > RibbonEpsilon
                            ? Sample.Distance / Source.TilingDistance
                            : Sample.Distance;

                        OutVertices.push_back({ Sample.Position - Side * HalfWidth, FVector2(U, 1.0f), Sample.Color });
                        OutVertices.push_back({ Sample.Position + Side * HalfWidth, FVector2(U, 0.0f), Sample.Color });
                    }

                    const int32 SegmentCount = static_cast<int32>(Samples.size()) - 1;
                    for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
                    {
                        const uint32 I0 = VertexBase + static_cast<uint32>(SegmentIndex * 2);
                        const uint32 I1 = I0 + 1;
                        const uint32 I2 = I0 + 2;
                        const uint32 I3 = I0 + 3;
                        OutIndices.push_back(I0);
                        OutIndices.push_back(I1);
                        OutIndices.push_back(I2);
                        OutIndices.push_back(I1);
                        OutIndices.push_back(I3);
                        OutIndices.push_back(I2);
                    }
                }

                ++TrailCount;
            }
        }

        GroupBegin = GroupEnd;
    }
}
