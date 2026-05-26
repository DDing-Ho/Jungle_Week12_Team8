#include "Render/Proxy/ParticleSceneProxy.h"
#include "Component/ParticleSystemComponent.h"
#include "Particles/Rendering/ParticleRenderData.h"
#include "Particles/Common/ParticleTypes.h"
#include "Render/Types/FrameContext.h"
#include "Render/Command/DrawCommand.h"
#include "Render/Shader/ShaderManager.h"
#include "Materials/Material.h"
#include "Object/ObjectFactory.h"
#include "Engine/Runtime/Engine.h"
#include "Render/Pipeline/Renderer.h"
#include "Math/Matrix.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace
{
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
		// SectionDraws 범위는 UpdatePerViewport에서 vertex 조립 후 확정
		// 여기서는 proxy material만 SectionDraws와 순서를 맞추기 위해 보관
	}
}

// ============================================================
// UpdatePerViewport — GatherRenderData 위임 + SectionDraws 확정
// ============================================================
void FParticleSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	StagedInstances.clear();
	SectionDraws.clear();

	if (!bVisible || CachedEmitters.empty()) return;

	const FParticleVertexBuildContext Ctx
	{
		Frame.CameraRight,
		Frame.CameraUp,
		Frame.CameraForward
	};
	TArray<uint32> IgnoredIndices;

	for (int32 i = 0; i < static_cast<int32>(CachedEmitters.size()); ++i)
	{
		const FCachedEmitter& E = CachedEmitters[i];
		const int32 MaterialIndex = E.MaterialIndex;
		if (!E.Data ||
			MaterialIndex < 0 ||
			MaterialIndex >= static_cast<int32>(ParticleMaterials.size()) ||
			!ParticleMaterials[MaterialIndex])
		{
			continue;
		}

		const FDynamicEmitterReplayDataBase& Source = E.Data->GetSource();
		if (Source.eEmitterType != EDynamicEmitterType::DET_Sprite)
			continue;

		const uint32 FirstInstance = static_cast<uint32>(StagedInstances.size());

		IgnoredIndices.clear();
		E.Data->GatherRenderData(Ctx, StagedInstances, IgnoredIndices);

		const uint32 InstanceCount = static_cast<uint32>(StagedInstances.size()) - FirstInstance;
		if (InstanceCount > 0)
			SectionDraws.push_back({ ParticleMaterials[MaterialIndex], 0, 6, FirstInstance, InstanceCount });
	}
}

// ============================================================
// PrepareDrawBuffer — staging 버퍼를 GPU 동적 VB/IB에 업로드
// ============================================================
bool FParticleSceneProxy::PrepareDrawBuffer(ID3D11Device* Device, ID3D11DeviceContext* Context,
	FDrawCommandBuffer& OutBuffer) const
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

	const uint32 InstanceCount = static_cast<uint32>(StagedInstances.size());
	if (InstanceVB.GetStride() == 0)
		InstanceVB.Create(Device, InstanceCount, sizeof(FSpriteParticleInstanceVertex));
	else
		InstanceVB.EnsureCapacity(Device, InstanceCount);

	if (!InstanceVB.Update(Context, StagedInstances.data(), InstanceCount)) return false;

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
	OutBuffer.InstanceCount = InstanceCount;

	return true;
}

UParticleSystemComponent* FParticleSceneProxy::GetParticleComponent() const
{
	return static_cast<UParticleSystemComponent*>(GetOwner());
}
