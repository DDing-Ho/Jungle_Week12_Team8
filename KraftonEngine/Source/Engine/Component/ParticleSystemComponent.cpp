#include "ParticleSystemComponent.h"
#include "Particles/Runtime/ParticleEmitterInstance.h"
#include "Particles/Rendering/ParticleRenderData.h"
#include "Render/Proxy/ParticleSceneProxy.h"
#include "Particles/Assets/ParticleSystemAssetManager.h"
#include "Math/Matrix.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/StaticMeshAsset.h"

#include <algorithm>
#include <cmath>
#include <Platform/Paths.h>

namespace
{
	void ExpandBounds(FBoundingBox& Bounds, const FVector& Min, const FVector& Max)
	{
		Bounds.Expand(Min);
		Bounds.Expand(Max);
	}

	void ExpandSpriteParticleBounds(FBoundingBox& Bounds, const FBaseParticle& Particle)
	{
		const float HalfWidth = std::abs(Particle.Size.X) * 0.5f;
		const float HalfHeight = std::abs(Particle.Size.Y) * 0.5f;
		const float Radius = std::sqrt(HalfWidth * HalfWidth + HalfHeight * HalfHeight);
		const FVector Extent(Radius, Radius, Radius);

		ExpandBounds(Bounds, Particle.Location - Extent, Particle.Location + Extent);
	}

	bool ExpandMeshParticleBounds(FBoundingBox& Bounds, const FBaseParticle& Particle, UStaticMesh* Mesh)
	{
		if (!Mesh)
		{
			return false;
		}

		FStaticMesh* Asset = Mesh->GetStaticMeshAsset();
		if (!Asset)
		{
			return false;
		}

		if (!Asset->bBoundsValid)
		{
			Asset->CacheBounds();
		}
		if (!Asset->bBoundsValid)
		{
			return false;
		}

		const float Rotation = Particle.RotationRate * Particle.RelativeTime * Particle.Lifetime;
		const FMatrix ParticleTransform =
			FMatrix::MakeScaleMatrix(Particle.Size)
			* FMatrix::MakeRotationZ(Rotation)
			* FMatrix::MakeTranslationMatrix(Particle.Location);

		FBoundingBox LocalBounds(
			Asset->BoundsCenter - Asset->BoundsExtent,
			Asset->BoundsCenter + Asset->BoundsExtent);

		FVector Corners[8];
		LocalBounds.GetCorners(Corners);
		for (const FVector& Corner : Corners)
		{
			Bounds.Expand(ParticleTransform.TransformPositionWithW(Corner));
		}
		return true;
	}

	UParticleModuleTypeDataBase* GetEmitterTypeData(const FParticleEmitterInstance* Instance)
	{
		return Instance && Instance->CurrentLODLevel
			? Instance->CurrentLODLevel->GetTypeDataModule()
			: nullptr;
	}

	void ExpandEmitterBounds(FBoundingBox& Bounds, const FParticleEmitterInstance* Instance)
	{
		if (!Instance ||
			Instance->ActiveParticles <= 0 ||
			!Instance->ParticleData ||
			!Instance->ParticleIndices ||
			Instance->ParticleStride <= 0)
		{
			return;
		}

		UParticleModuleTypeDataBase* TypeData = GetEmitterTypeData(Instance);
		UParticleModuleTypeDataMesh* MeshTypeData = Cast<UParticleModuleTypeDataMesh>(TypeData);
		UStaticMesh* Mesh = MeshTypeData ? MeshTypeData->GetMesh() : nullptr;

		for (int32 ParticleIndex = 0; ParticleIndex < Instance->ActiveParticles; ++ParticleIndex)
		{
			const uint16 RealIndex = Instance->ParticleIndices[ParticleIndex];
			const uint8* ParticleBase = Instance->ParticleData + Instance->ParticleStride * RealIndex;
			const FBaseParticle* Particle = reinterpret_cast<const FBaseParticle*>(ParticleBase);
			if (!Particle)
			{
				continue;
			}

			if (Mesh && ExpandMeshParticleBounds(Bounds, *Particle, Mesh))
			{
				continue;
			}

			ExpandSpriteParticleBounds(Bounds, *Particle);
		}
	}
}

UParticleSystemComponent::UParticleSystemComponent()
{
	ClearEmitterInstances();

	EmitterDelay = 0;
	DeltaTimeTick = 0;
	CustomTimeDilation = 1;

	CurrentLODLevelIndex = 0;
	TotalActiveParticles = 0;

	EmitterMaterials.clear();
	CollisionEvents.clear();
	EmitterRenderData.clear();
}

void UParticleSystemComponent::EndPlay()
{
	DeactivateSystem();
	ClearRenderData();
	ClearEmitterInstances();

	UPrimitiveComponent::EndPlay();
}

void UParticleSystemComponent::Activate()
{
	UPrimitiveComponent::Activate();
	ActivateSystem();
}

void UParticleSystemComponent::Deactivate()
{
	DeactivateSystem();
	ClearRenderData();
	UPrimitiveComponent::Deactivate();
}

void UParticleSystemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UPrimitiveComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Template)
		return;

	if (!IsActive())
	{
		if (!IsActive() && bAutoActivate)
		{
			ActivateSystem();
		}
		else
		{
			return;
		}
	}

	bool bRequiresReset = bResetTriggered;
	bResetTriggered = false;

	if (bRequiresReset){
		ResetSystem();
	}

	DeltaTimeTick = DeltaTime * CustomTimeDilation;
	//AccumLODDistanceCheckTime += DeltaTimeTick;

	//LOD처리
	//if (bCalculateLODLevel == true && AccumLODDistanceCheckTime > Template->LODDistanceCheckTime)
	//{
	//	AccumLODDistanceCheckTime = 0;
	//	FVector EffectLocation = GetWorldLocation();
	//	int32 LODLevel = CalculateLODLevel(EffectLocation);
	//	SetLODLevel(LODLevel);
	//}

	//이벤트 버퍼 정리
	CollisionEvents.clear();
	TotalActiveParticles = 0;

	// 인스턴스 tick
	for (auto instance : EmitterInstances) {
		if (!instance) continue;
		instance->Tick(DeltaTimeTick);
		TotalActiveParticles += instance->ActiveParticles;
	}

	MarkWorldBoundsDirty();

	// RenderData 수집
	BuildRenderData();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSceneProxy(this);
}

void UParticleSystemComponent::UpdateWorldAABB() const
{
	FBoundingBox ParticleBounds;
	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		ExpandEmitterBounds(ParticleBounds, Instance);
	}

	if (!ParticleBounds.IsValid())
	{
		UPrimitiveComponent::UpdateWorldAABB();
		return;
	}

	WorldAABBMinLocation = ParticleBounds.Min;
	WorldAABBMaxLocation = ParticleBounds.Max;
	bWorldAABBDirty = false;
	bHasValidWorldAABB = true;
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	const bool bShouldActivate = bAutoActivate || bIsActive;

	DeactivateSystem();
	ClearEmitterInstances();

	Template = InTemplate;
	TemplateAsset = InTemplate;

	if (Template)
	{
		TemplateAsset.SetPath(FPaths::MakeProjectRelative(Template->GetAssetPathFileName()));
		Template->CacheSystemModuleInfo();
		CreateEmitterInstances();
	}

	if (Template && bShouldActivate)
	{
		ActivateSystem();
	}

	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UParticleSystemComponent::SetLODLevel(int32 InLODLevel)
{
	if (!Template)
		return;

	if (Template->LODDistances.empty())
		return;

	const int32 MaxLOD = static_cast<int32>(Template->LODDistances.size()) - 1;
	const int32 NewLODLevel = std::clamp(InLODLevel, 0, MaxLOD);

	if (CurrentLODLevelIndex == NewLODLevel)
		return;

	CurrentLODLevelIndex = NewLODLevel;

	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance)
		{
			//Instance->SetCurrentLODIndex(CurrentLODLevel, true);
		}
	}

	MarkProxyDirty(EDirtyFlag::Mesh);
	MarkWorldBoundsDirty();
}

FParticleEmitterInstance* UParticleSystemComponent::CreateEmitterInstanceForEmitter(UParticleEmitter* Emitter) const
{
	if (!Emitter)
		return nullptr;

	EParticleEmitterType EmitterType = EParticleEmitterType::PET_Sprite;

	UParticleLODLevel* LOD = Emitter->GetLODLevel(CurrentLODLevelIndex);
	if (!LOD)
	{
		LOD = Emitter->GetLODLevel(0);
	}
	if (LOD)
	{
		if (UParticleModuleTypeDataBase* TypeData = LOD->GetTypeDataModule())
		{
			EmitterType = TypeData->GetEmitterType();
		}
	}

	switch (EmitterType)
	{
	case EParticleEmitterType::PET_Sprite:
		return new FParticleSpriteEmitterInstance();

	case EParticleEmitterType::PET_Mesh:
		return new FParticleMeshEmitterInstance();

	case EParticleEmitterType::PET_Beam:
	case EParticleEmitterType::PET_Ribbon:
	default:
		return new FParticleEmitterInstance();
	}
}

void UParticleSystemComponent::CreateEmitterInstances()
{
	if (!Template) {
		return;
	}

	ClearEmitterInstances();

	const TArray<UParticleEmitter*> TemplateEmitters = Template->GetEmitters();
	for (auto Emitter : TemplateEmitters) {
		FParticleEmitterInstance* Instance = CreateEmitterInstanceForEmitter(Emitter);
		if (!Instance)
			continue;

		Instance->CurrentLODLevelIndex = CurrentLODLevelIndex;
		Instance->Init(this, Emitter);

		EmitterInstances.push_back(Instance);
	}
}

void UParticleSystemComponent::ActivateSystem()
{	
	if (IsActive() || !Template) {
		return;
	}

	bIsActive = true;
	SetComponentTickEnabled(true);

	//LOD처리
	//FVector EffectLocation = GetWorldLocation();
	//int32 LODLevel = CalculateLODLevel(EffectLocation);
	//SetLODLevel(LODLevel);
}

void UParticleSystemComponent::DeactivateSystem()
{
	bIsActive = false;
	SetComponentTickEnabled(false);
	ResetSystem();

	TotalActiveParticles = 0;
}

void UParticleSystemComponent::ResetSystem()
{
	for (FParticleEmitterInstance* instance : EmitterInstances) {
		instance->Reset();
	}
	ClearRenderData();
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UParticleSystemComponent::ClearEmitterInstances()
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance)
		{
			delete Instance;
		}
	}

	EmitterInstances.clear();
	TotalActiveParticles = 0;
	ClearRenderData();
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UParticleSystemComponent::ClearRenderData()
{
	for (FDynamicEmitterDataBase* Data : EmitterRenderData)
	{
		delete Data;
	}

	EmitterRenderData.clear();
}

void UParticleSystemComponent::PostEditProperty(const char* PropertyName)
{
	UPrimitiveComponent::PostEditProperty(PropertyName);

	if (strcmp(PropertyName, "Template") == 0)
	{
		if (TemplateAsset.IsNull())
		{
			SetTemplate(nullptr);
			return;
		}

		UParticleSystem* Loaded =
			FParticleSystemAssetManager::Get().Load(TemplateAsset.GetPath().ToString());

		SetTemplate(Loaded);
	}
}

void UParticleSystemComponent::PostDuplicate()
{
	UPrimitiveComponent::PostDuplicate();

	if (!TemplateAsset.IsNull())
	{
		UParticleSystem* Loaded =
			FParticleSystemAssetManager::Get().Load(TemplateAsset.GetPath().ToString());

		SetTemplate(Loaded);
	}
}

void UParticleSystemComponent::BuildRenderData()
{
	ClearRenderData();

	if (EmitterInstances.empty())
		return;

	EmitterRenderData.reserve(EmitterInstances.size());

	for (int32 EmitterIndex = 0; EmitterIndex < static_cast<int32>(EmitterInstances.size()); ++EmitterIndex)
	{
		FParticleEmitterInstance* Instance = EmitterInstances[EmitterIndex];
		if (!Instance || Instance->ActiveParticles <= 0)
			continue;

		FDynamicEmitterDataBase* NewData = Instance->CreateDynamicEmitterData();
		if (!NewData)
			continue;

		NewData->EmitterIndex = EmitterIndex;
		EmitterRenderData.push_back(NewData);
	}
}
