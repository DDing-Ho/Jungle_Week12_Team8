/**
 * @file ParticleVertexTypes.h
 *  @brief Particle 렌더링용 Vertex / Instance 타입 정의.
 *
 * 포함 타입:
 * - FSpriteParticleQuadVertex:     Sprite Particle 공용 unit quad vertex
 * - FSpriteParticleInstanceVertex: Sprite Particle GPU 인스턴스 데이터
 * - FMeshParticleInstanceVertex:   Mesh   Particle GPU 인스턴스 데이터
 */

#pragma once

#include "../Runtime/ParticleRuntimeTypes.h"
#include "Math/Matrix.h"
#include "Math/Vector.h"

/** Sprite Particle 공용 unit quad vertex
 *  - 모든 Sprite Particle이 slot 0에서 공유한다.
 */
struct FSpriteParticleQuadVertex
{
    FVector  Position;         // normalized quad corner (-0.5..0.5)
    FVector2 TexCoord;         // particle texture uv
};

/** Sprite Particle 렌더링용 per-instance 데이터
 *  - 파티클당 1개 인스턴스 데이터를 생성한다.
 *  - Size.xy는 크기, Size.zw는 현재 미사용이다.
 */
struct FSpriteParticleInstanceVertex
{
    FVector  Position;         // world-space position
    float    Rotation;         // billboard rotation in radians
    FVector4 Size;             // xy: width/height, zw: unused
    FVector4 Color;            // rgba: normalized [0,1]
    FVector4 UVRegionA;        // xy: 첫 번째 atlas frame offset, zw: frame size
    FVector4 UVRegionB;        // xy: 두 번째 atlas frame offset, zw: frame size
    float    SubUVLerp;        // UVRegionA -> UVRegionB blend 비율
};

/** Mesh Particle 인스턴싱용 per-instance 데이터
 *  - 하나의 Static/Skeletal Mesh를 공유하고,
 *  - 인스턴스마다 Transform/Color만 다르게 적용한다.
 */
struct FMeshParticleInstanceVertex
{
    FMatrix  Transform; // local-to-world transform, scale 포함
    FVector4 Color;     // rgba: normalized [0,1]
};

/** Ribbon Particle 렌더링용 동적 vertex */
struct FRibbonParticleVertex
{
    FVector  Position; // world-space strip vertex
    FVector2 TexCoord; // trail-length uv
    FVector4 Color;    // rgba: normalized [0,1]
};
