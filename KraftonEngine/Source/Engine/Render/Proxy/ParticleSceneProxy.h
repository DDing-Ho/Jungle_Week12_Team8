#pragma once

#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Render/Resource/Buffer.h"
#include "Particles/Rendering/ParticleRenderData.h"

class UParticleSystemComponent;
class UMaterial;

// ============================================================
// FParticleSceneProxy — UParticleSystemComponent 전용 프록시
// ============================================================
// UpdateMesh()      : EmitterRenderData → FCachedEmitter 캐싱
//                     emitter 타입에 맞는 particle shader permutation으로
//                     proxy 소유 UMaterial을 생성/재사용하여 SectionDraws에 등록
// UpdatePerViewport : GatherRenderData() 위임 → staging 버퍼 조립
// PrepareDrawBuffer : 공용 quad VB/IB + particle instance VB 업로드
//
// Emitter 타입별 전용 셰이더:
//   Sprite        → ParticleSprite.hlsl
//   Sprite+SubUV  → ParticleSprite.hlsl (USE_SUBUV)
//   Mesh          → ParticleMesh.hlsl   (FMeshParticleInstanceVertex 인스턴싱)
//   Beam          → ParticleBeam.hlsl
//   Ribbon        → ParticleRibbon.hlsl
class FParticleSceneProxy : public FPrimitiveSceneProxy
{
public:
	FParticleSceneProxy(UParticleSystemComponent* InComponent);
	~FParticleSceneProxy() override;

	void UpdateMesh() override;
	void UpdatePerViewport(const FFrameContext& Frame) override;
	bool PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
		FDrawCommandBuffer& OutBuffer) const override;

private:
	UParticleSystemComponent* GetParticleComponent() const;

	// emitter 타입 → particle shader permutation 선택
	static FShader* SelectParticleShader(EDynamicEmitterType Type);

	// proxy 소유 UMaterial 생성 (shader + render state 고정, SRV만 외부에서 주입)
	static UMaterial* CreateParticleMaterial(FShader* Shader);

	// ParticleMaterials 전부 해제
	void ReleaseParticleMaterials();

	// ============================================================

	// Emitter 캐시 — frame 내 유효한 Data 포인터
	struct FCachedEmitter
	{
		const FDynamicEmitterDataBase* Data = nullptr;
		int32 MaterialIndex = INDEX_NONE;
		// TODO: 필드 추가 예정
		// Depth 기반 sort key, Material 인덱스, frustum culling 결과값
	};
	TArray<FCachedEmitter> CachedEmitters;

	// Proxy 소유 materials — emitter당 1개, shader permutation 고정
	// SRV는 UpdateMesh()마다 source material로부터 갱신
	TArray<UMaterial*> ParticleMaterials;

	// UpdatePerViewport에서 조립한 CPU staging 버퍼
	TArray<FSpriteParticleInstanceVertex> StagedInstances;

	// GPU 동적 버퍼 (mutable for const PrepareDrawBuffer)
	mutable FVertexBuffer        QuadVB;
	mutable FIndexBuffer         QuadIB;
	mutable FDynamicVertexBuffer InstanceVB;
};
