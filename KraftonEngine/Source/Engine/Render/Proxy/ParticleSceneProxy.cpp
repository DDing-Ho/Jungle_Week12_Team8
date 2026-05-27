#include "Render/Proxy/ParticleSceneProxy.h"
#include "Component/ParticleSystemComponent.h"
#include "Particles/Rendering/ParticleRenderData.h"
#include "Particles/Common/ParticleTypes.h"
#include "Particles/Runtime/ParticleRuntimeTypes.h"
#include "Render/Types/FrameContext.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Shader/ShaderManager.h"
#include "Materials/Material.h"
#include "Object/ObjectFactory.h"
#include "Engine/Runtime/Engine.h"
#include "Render/Pipeline/Renderer.h"
#include "Math/Matrix.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"

#include <d3d11.h>
#include <algorithm>
#include <wrl/client.h>

namespace
{
	float CalculateEmitterDistanceSquared(const FDynamicEmitterReplayDataBase& Source, const FVector& CameraPosition)
	{
		if (!Source.IsValid() || !Source.DataContainer.ParticleIndices || Source.ParticleStride <= 0)
		{
			return 0.0f;
		}

		FVector Center = FVector::ZeroVector;
		const uint8* RawData = Source.DataContainer.ParticleData;
		int32 ValidParticleCount = 0;

		for (int32 i = 0; i < Source.ActiveParticleCount; ++i)
		{
			const uint16 ParticleIdx = Source.DataContainer.ParticleIndices[i];
			const FBaseParticle* Particle = reinterpret_cast<const FBaseParticle*>(
				RawData + Source.ParticleStride * ParticleIdx);

			Center += FVector(
				Particle->Location.X * Source.Scale.X,
				Particle->Location.Y * Source.Scale.Y,
				Particle->Location.Z * Source.Scale.Z);
			++ValidParticleCount;
		}

		if (ValidParticleCount <= 0)
		{
			return 0.0f;
		}

		Center /= static_cast<float>(ValidParticleCount);
		return FVector::DistSquared(Center, CameraPosition);
	}

	ID3D11ShaderResourceView* GetParticleFallbackWhiteSRV()
	{
		static Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> WhiteSRV;
		if (WhiteSRV)
		{
			return WhiteSRV.Get();
		}

		ID3D11Device* Device = GEngine ? GEngine->GetRenderer().GetFD3DDevice().GetDevice() : nullptr;
		if (!Device)
		{
			return nullptr;
		}

		D3D11_TEXTURE2D_DESC TextureDesc = {};
		TextureDesc.Width = 1;
		TextureDesc.Height = 1;
		TextureDesc.MipLevels = 1;
		TextureDesc.ArraySize = 1;
		TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		TextureDesc.SampleDesc.Count = 1;
		TextureDesc.Usage = D3D11_USAGE_IMMUTABLE;
		TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		const uint32 WhitePixel = 0xffffffff;
		D3D11_SUBRESOURCE_DATA InitialData = {};
		InitialData.pSysMem = &WhitePixel;
		InitialData.SysMemPitch = sizeof(WhitePixel);

		Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
		if (FAILED(Device->CreateTexture2D(&TextureDesc, &InitialData, Texture.GetAddressOf())))
		{
			return nullptr;
		}

		if (FAILED(Device->CreateShaderResourceView(Texture.Get(), nullptr, WhiteSRV.GetAddressOf())))
		{
			WhiteSRV.Reset();
			return nullptr;
		}

		return WhiteSRV.Get();
	}

}

// ============================================================
// FParticleSceneProxy — 생성 / 소멸
// ============================================================

FParticleSceneProxy::FParticleSceneProxy(UParticleSystemComponent* InComponent)
	: FPrimitiveSceneProxy(static_cast<UPrimitiveComponent*>(InComponent))
{
	ProxyFlags |= EPrimitiveProxyFlags::PerViewportUpdate;
	ProxyFlags |= EPrimitiveProxyFlags::Particle;
	ProxyFlags &= ~EPrimitiveProxyFlags::ShowAABB;
	ProxyFlags &= ~EPrimitiveProxyFlags::SupportsOutline;

	// 파티클 위치는 world-space 직접 전달 → Model = Identity
	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(FMatrix::Identity);
	MarkPerObjectCBDirty();
}

FParticleSceneProxy::~FParticleSceneProxy()
{
	ReleaseParticleMaterials();
	QuadVB.Release();
	QuadIB.Release();
	InstanceVB.Release();
	MeshInstanceVB.Release();
	RibbonVB.Release();
	RibbonIB.Release();
}

// ============================================================
// 내부 헬퍼
// ============================================================

FShader* FParticleSceneProxy::SelectParticleShader(EDynamicEmitterType Type)
{
	auto& SM = FShaderManager::Get();
	switch (Type)
	{
	case EDynamicEmitterType::DET_Sprite:  return SM.GetOrCreate(EShaderPath::ParticleSprite);
	case EDynamicEmitterType::DET_Mesh:    return SM.GetOrCreate(EShaderPath::ParticleMesh);
	case EDynamicEmitterType::DET_Beam:    return SM.GetOrCreate(EShaderPath::ParticleBeam);
	case EDynamicEmitterType::DET_Ribbon:  return SM.GetOrCreate(EShaderPath::ParticleRibbon);
	default:                               return nullptr;
	}
}

UMaterial* FParticleSceneProxy::CreateParticleMaterial(FShader* Shader)
{
	return UMaterial::CreateTransient(
		ERenderPass::AlphaBlend,
		EBlendState::AlphaBlend,
		EDepthStencilState::Default,
		ERasterizerState::SolidNoCull, // 파티클은 양면
		Shader);
}

void FParticleSceneProxy::ReleaseParticleMaterials()
{
	for (UMaterial* Mat : ParticleMaterials)
	{
		if (Mat) GUObjectArray.DestroyObject(Mat);
	}
	ParticleMaterials.clear();
}

// ============================================================
// UpdateMesh — EmitterRenderData를 캐싱 + proxy 소유 material 갱신
// (함수명은 FPrimitiveSceneProxy 인터페이스 레거시; Mesh 전용이 아님)
// ============================================================
void FParticleSceneProxy::UpdateMesh()
{
	UParticleSystemComponent* Comp = GetParticleComponent();
	CachedEmitters.clear();
	DrawSections.clear();
	SectionDraws.clear();

	if (!Comp->IsParticleVisible() || !Comp->IsActive())
	{
		bVisible = false;
		return;
	}
	bVisible = true;

	const TArray<FDynamicEmitterDataBase*>& RenderData = Comp->GetEmitterRenderData();

	// emitter 수가 바뀌면 material 캐시 전체 재생성
	if (ParticleMaterials.size() != RenderData.size())
	{
		ReleaseParticleMaterials();
		ParticleMaterials.resize(RenderData.size(), nullptr);
	}

	CachedEmitters.reserve(RenderData.size());

	for (int32 i = 0; i < static_cast<int32>(RenderData.size()); ++i)
	{
		const FDynamicEmitterDataBase* Data = RenderData[i];
		if (!Data) continue;

		const FDynamicEmitterReplayDataBase& Source = Data->GetSource();
		if (!Source.IsValid()) continue;

		// emitter 타입 → shader permutation 선택
		FShader* Shader = SelectParticleShader(Source.eEmitterType);
		if (!Shader) continue; // Mesh 등 미지원 타입은 건너뜀

		// proxy 소유 material 생성 (최초 1회)
		if (!ParticleMaterials[i])
			ParticleMaterials[i] = CreateParticleMaterial(Shader);

		// 텍스처 SRV를 source material에서 매 틱 복사
		ID3D11ShaderResourceView* ParticleSRV = GetParticleFallbackWhiteSRV();
		if (Source.Material)
		{
			const ID3D11ShaderResourceView* const* SrcSRVs = Source.Material->GetCachedSRVs();
			ID3D11ShaderResourceView* SourceSRV = const_cast<ID3D11ShaderResourceView*>(SrcSRVs[(int)EMaterialTextureSlot::Diffuse]);
			if (SourceSRV)
			{
				ParticleSRV = SourceSRV;
			}
		}
		ParticleMaterials[i]->SetCachedSRV(EMaterialTextureSlot::Diffuse, ParticleSRV);

		CachedEmitters.push_back({ Data, i });
	}
}

// ============================================================
// UpdatePerViewport — GatherRenderData 위임 + particle draw section 확정
// ============================================================
void FParticleSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	StagedInstances.clear();
	StagedMeshInstances.clear();
	StagedRibbonVertices.clear();
	StagedRibbonIndices.clear();
	DrawSections.clear();
	SectionDraws.clear();
	bSpriteInstanceBufferDirty = true;
	bMeshInstanceBufferDirty = true;
	bRibbonBufferDirty = true;

	if (!bVisible || CachedEmitters.empty()) return;

	const FParticleVertexBuildContext Ctx
	{
		Frame.CameraPosition,
		Frame.CameraRight,
		Frame.CameraUp,
		Frame.CameraForward
	};
	TArray<uint32> IgnoredIndices;

	struct FSortedEmitter
	{
		FCachedEmitter E;
		int32 Priority = 0;
		float DistanceSq = 0.0f;
		int32 OriginalOrder = 0;
	};

	TArray<FSortedEmitter> SortedEmitters;
	SortedEmitters.reserve(CachedEmitters.size());

	for (int32 i = 0; i < static_cast<int32>(CachedEmitters.size()); ++i)
	{
		const FCachedEmitter& E = CachedEmitters[i];
		if (!E.Data)
		{
			continue;
		}

		const FDynamicEmitterReplayDataBase& Source = E.Data->GetSource();

		SortedEmitters.push_back({
			E,
			Source.TranslucencySortPriority,
			CalculateEmitterDistanceSquared(Source, Frame.CameraPosition),
			i
		});
	}

	std::stable_sort(SortedEmitters.begin(), SortedEmitters.end(),
		[](const FSortedEmitter& A, const FSortedEmitter& B)
		{
			if (A.Priority != B.Priority)
			{
				return A.Priority < B.Priority;
			}
			if (A.DistanceSq != B.DistanceSq)
			{
				return A.DistanceSq > B.DistanceSq;
			}
			return A.OriginalOrder < B.OriginalOrder;
		});

	for (int32 SortedIndex = 0; SortedIndex < static_cast<int32>(SortedEmitters.size()); ++SortedIndex)
	{
		const FSortedEmitter& SortedEmitter = SortedEmitters[SortedIndex];
		const FCachedEmitter& E = SortedEmitter.E;
		const int32 MaterialIndex = E.MaterialIndex;
		if (!E.Data ||
			MaterialIndex < 0 ||
			MaterialIndex >= static_cast<int32>(ParticleMaterials.size()) ||
			!ParticleMaterials[MaterialIndex])
		{
			continue;
		}

		const FDynamicEmitterReplayDataBase& Source = E.Data->GetSource();
		const float SortDepth = static_cast<float>(SortedEmitters.size() - static_cast<size_t>(SortedIndex));

		if (Source.eEmitterType == EDynamicEmitterType::DET_Sprite)
		{
			const uint32 FirstInstance = static_cast<uint32>(StagedInstances.size());

			IgnoredIndices.clear();
			E.Data->GatherRenderData(Ctx, StagedInstances, IgnoredIndices);

			const uint32 InstanceCount = static_cast<uint32>(StagedInstances.size()) - FirstInstance;
			if (InstanceCount == 0)
				continue;

			FParticleDrawSection Draw;
			Draw.Type = EParticleDrawSectionType::Sprite;
			Draw.Material = ParticleMaterials[MaterialIndex];
			Draw.FirstIndex = 0;
			Draw.IndexCount = 6;
			Draw.FirstInstance = FirstInstance;
			Draw.InstanceCount = InstanceCount;
			Draw.SortDepth = SortDepth;
			DrawSections.push_back(Draw);
			continue;
		}

		if (Source.eEmitterType == EDynamicEmitterType::DET_Mesh)
		{
			const FDynamicMeshEmitterData* MeshData = static_cast<const FDynamicMeshEmitterData*>(E.Data);
			const FDynamicMeshEmitterReplayData& MeshSource = MeshData->Source;
			if (!MeshSource.Mesh)
				continue;

			FMeshBuffer* MeshBuffer = MeshSource.Mesh->GetLODMeshBuffer(0);
			if (!MeshBuffer || !MeshBuffer->IsValid())
				continue;

			const uint32 FirstInstance = static_cast<uint32>(StagedMeshInstances.size());
			MeshData->GatherRenderData(Ctx, StagedMeshInstances);

			const uint32 InstanceCount = static_cast<uint32>(StagedMeshInstances.size()) - FirstInstance;
			if (InstanceCount == 0)
				continue;

			const TArray<FStaticMeshSection>& Sections = MeshSource.Mesh->GetLODSections(0);
			if (!Sections.empty())
			{
				for (const FStaticMeshSection& Section : Sections)
				{
					FParticleDrawSection Draw;
					Draw.Type = EParticleDrawSectionType::Mesh;
					Draw.Material = ParticleMaterials[MaterialIndex];
					Draw.MeshBuffer = MeshBuffer;
					Draw.FirstIndex = Section.FirstIndex;
					Draw.IndexCount = Section.NumTriangles * 3;
					Draw.FirstInstance = FirstInstance;
					Draw.InstanceCount = InstanceCount;
					Draw.SortDepth = SortDepth;
					DrawSections.push_back(Draw);
				}
			}
			else
			{
				FParticleDrawSection Draw;
				Draw.Type = EParticleDrawSectionType::Mesh;
				Draw.Material = ParticleMaterials[MaterialIndex];
				Draw.MeshBuffer = MeshBuffer;
				Draw.FirstIndex = 0;
				Draw.IndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
				Draw.FirstInstance = FirstInstance;
				Draw.InstanceCount = InstanceCount;
				Draw.SortDepth = SortDepth;
				DrawSections.push_back(Draw);
			}
			continue;
		}

		if (Source.eEmitterType == EDynamicEmitterType::DET_Ribbon)
		{
			const uint32 FirstIndex = static_cast<uint32>(StagedRibbonIndices.size());
			E.Data->GatherRibbonRenderData(Ctx, StagedRibbonVertices, StagedRibbonIndices);

			const uint32 IndexCount = static_cast<uint32>(StagedRibbonIndices.size()) - FirstIndex;
			if (IndexCount == 0)
				continue;

			FParticleDrawSection Draw;
			Draw.Type = EParticleDrawSectionType::Ribbon;
			Draw.Material = ParticleMaterials[MaterialIndex];
			Draw.FirstIndex = FirstIndex;
			Draw.IndexCount = IndexCount;
			Draw.FirstInstance = 0;
			Draw.InstanceCount = 1;
			Draw.SortDepth = SortDepth;
			DrawSections.push_back(Draw);
			continue;
		}

		// Beam은 section 타입만 예약해 둔다. GatherRenderData 구현 후 여기서 추가한다.
	}
}

// ============================================================
// PrepareDrawBuffer — particle은 section별 buffer 경로를 사용한다.
// ============================================================
bool FParticleSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
	FDrawCommandBuffer& OutBuffer) const
{
	return false;
}

// ============================================================
// PrepareDrawBufferForSection — section 타입별 GPU buffer range 구성
// ============================================================
bool FParticleSceneProxy::PrepareDrawBufferForSection(const FParticleDrawSection& Section,
	ID3D11Device* Device, ID3D11DeviceContext* Context, FDrawCommandBuffer& OutBuffer) const
{
	if (Section.InstanceCount == 0)
		return false;

	if (Section.Type == EParticleDrawSectionType::Sprite)
	{
		if (StagedInstances.empty())
			return false;

		if (!QuadVB.GetBuffer())
		{
			static const FSpriteParticleQuadVertex QuadVertices[4] =
			{
				{ FVector(-0.5f, -0.5f, 0.0f), FVector2(0.0f, 1.0f) },
				{ FVector( 0.5f, -0.5f, 0.0f), FVector2(1.0f, 1.0f) },
				{ FVector( 0.5f,  0.5f, 0.0f), FVector2(1.0f, 0.0f) },
				{ FVector(-0.5f,  0.5f, 0.0f), FVector2(0.0f, 0.0f) },
			};
			QuadVB.Create(Device, QuadVertices, 4, sizeof(QuadVertices), sizeof(FSpriteParticleQuadVertex));
		}

		if (!QuadIB.GetBuffer())
		{
			static const uint32 QuadIndices[6] = { 0, 1, 2, 0, 2, 3 };
			QuadIB.Create(Device, QuadIndices, 6, sizeof(QuadIndices));
		}

		if (!QuadVB.GetBuffer() || !QuadIB.GetBuffer())
			return false;

		const uint32 TotalInstanceCount = static_cast<uint32>(StagedInstances.size());
		if (bSpriteInstanceBufferDirty)
		{
			if (InstanceVB.GetStride() == 0)
				InstanceVB.Create(Device, TotalInstanceCount, sizeof(FSpriteParticleInstanceVertex));
			else
				InstanceVB.EnsureCapacity(Device, TotalInstanceCount);

			if (!InstanceVB.Update(Context, StagedInstances.data(), TotalInstanceCount))
				return false;

			bSpriteInstanceBufferDirty = false;
		}

		OutBuffer = {};
		OutBuffer.VB = QuadVB.GetBuffer();
		OutBuffer.VBStride = QuadVB.GetStride();
		OutBuffer.IB = QuadIB.GetBuffer();
		OutBuffer.InstanceVB = InstanceVB.GetBuffer();
		OutBuffer.InstanceVBStride = InstanceVB.GetStride();
		OutBuffer.IndexCount = QuadIB.GetIndexCount();
		OutBuffer.FirstIndex = 0;
		OutBuffer.BaseVertex = 0;
		OutBuffer.StartInstance = 0;
		OutBuffer.InstanceCount = TotalInstanceCount;
		return true;
	}

	if (Section.Type == EParticleDrawSectionType::Mesh)
	{
		if (StagedMeshInstances.empty() || !Section.MeshBuffer || !Section.MeshBuffer->IsValid())
			return false;

		const uint32 TotalInstanceCount = static_cast<uint32>(StagedMeshInstances.size());
		if (bMeshInstanceBufferDirty)
		{
			if (MeshInstanceVB.GetStride() == 0)
				MeshInstanceVB.Create(Device, TotalInstanceCount, sizeof(FMeshParticleInstanceVertex));
			else
				MeshInstanceVB.EnsureCapacity(Device, TotalInstanceCount);

			if (!MeshInstanceVB.Update(Context, StagedMeshInstances.data(), TotalInstanceCount))
				return false;

			bMeshInstanceBufferDirty = false;
		}

		OutBuffer = {};
		OutBuffer.VB = Section.MeshBuffer->GetVertexBuffer().GetBuffer();
		OutBuffer.VBStride = Section.MeshBuffer->GetVertexBuffer().GetStride();
		OutBuffer.IB = Section.MeshBuffer->GetIndexBuffer().GetBuffer();
		OutBuffer.InstanceVB = MeshInstanceVB.GetBuffer();
		OutBuffer.InstanceVBStride = MeshInstanceVB.GetStride();
		OutBuffer.IndexCount = Section.MeshBuffer->GetIndexBuffer().GetIndexCount();
		OutBuffer.FirstIndex = 0;
		OutBuffer.BaseVertex = 0;
		OutBuffer.StartInstance = 0;
		OutBuffer.InstanceCount = TotalInstanceCount;
		return true;
	}

	if (Section.Type == EParticleDrawSectionType::Ribbon)
	{
		if (StagedRibbonVertices.empty() || StagedRibbonIndices.empty())
			return false;

		const uint32 TotalVertexCount = static_cast<uint32>(StagedRibbonVertices.size());
		const uint32 TotalIndexCount = static_cast<uint32>(StagedRibbonIndices.size());
		if (bRibbonBufferDirty)
		{
			if (RibbonVB.GetStride() == 0)
				RibbonVB.Create(Device, TotalVertexCount, sizeof(FRibbonParticleVertex));
			else
				RibbonVB.EnsureCapacity(Device, TotalVertexCount);

			if (RibbonIB.GetMaxCount() == 0)
				RibbonIB.Create(Device, TotalIndexCount);
			else
				RibbonIB.EnsureCapacity(Device, TotalIndexCount);

			if (!RibbonVB.Update(Context, StagedRibbonVertices.data(), TotalVertexCount))
				return false;
			if (!RibbonIB.Update(Context, StagedRibbonIndices.data(), TotalIndexCount))
				return false;

			bRibbonBufferDirty = false;
		}

		OutBuffer = {};
		OutBuffer.VB = RibbonVB.GetBuffer();
		OutBuffer.VBStride = RibbonVB.GetStride();
		OutBuffer.IB = RibbonIB.GetBuffer();
		OutBuffer.IndexCount = TotalIndexCount;
		OutBuffer.FirstIndex = 0;
		OutBuffer.BaseVertex = 0;
		return true;
	}

	return false;
}

UParticleSystemComponent* FParticleSceneProxy::GetParticleComponent() const
{
	return static_cast<UParticleSystemComponent*>(GetOwner());
}
