/**
 * @file ParticleVertexTypes.h
 * @brief Particle 렌더링용 Vertex 타입 정의.
 *
 * 포함 타입:
 * - FParticleSpriteVertex: Sprite Particle 렌더링 Vertex
 * - FMeshParticleInstanceVertex: Mesh Particle Instance Vertex
 */

#pragma once

#include "../Runtime/ParticleRuntimeTypes.h"
#include "Math/Matrix.h"

/** Sprite Particle 렌더링용 동적 Vertex */
struct FParticleSpriteVertex
{
    FVector   Position; // Vertex 위치
    FVector2  UV;       // Texture 좌표
    FColor    Color;    // Vertex 색상
};

/** Mesh Particle 렌더링용 Instance Vertex */
struct FMeshParticleInstanceVertex
{
    FMatrix    Transform; // Mesh Instance Transform
    FColor     Color;     // Instance 색상
    FVector    Size;      // Instance 크기
};
