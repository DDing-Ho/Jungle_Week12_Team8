#include "ParticleSystemComponent.h"
#include "Particles/Runtime/ParticleEmitterInstance.h"
#include "Particles/Rendering/ParticleRenderData.h"
#include "Particles/Assets/ParticleTypeData.h"
#include "Render/Proxy/ParticleSceneProxy.h"
#include "Particles/Assets/ParticleSystemAssetManager.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Core/Log.h"
#include "Profiling/Stats.h"
#include "Profiling/Timer.h"
#include "Runtime/Engine.h"

#include <algorithm>
#include <cmath>
#include <Platform/Paths.h>

namespace
{
	const FBaseParticle* GetActiveParticle(const FParticleEmitterInstance* Instance, int32 ActiveIndex)
	{
		if (!Instance || !Instance->ParticleData || !Instance->ParticleIndices)
			return nullptr;

		if (Instance->ParticleStride <= 0 || ActiveIndex < 0 || ActiveIndex >= Instance->ActiveParticles)
			return nullptr;

		const uint16 ParticleIndex = Instance->ParticleIndices[ActiveIndex];
		return reinterpret_cast<const FBaseParticle*>(
			Instance->ParticleData + Instance->ParticleStride * ParticleIndex);
	}

	EParticleEmitterType GetEmitterType(const FParticleEmitterInstance* Instance)
	{
		if (!Instance || !Instance->CurrentLODLevel)
			return EParticleEmitterType::PET_Sprite;

		const UParticleModuleTypeDataBase* TypeData = Instance->CurrentLODLevel->GetTypeDataModule();
		return TypeData ? TypeData->GetEmitterType() : EParticleEmitterType::PET_Sprite;
	}

	void ExpandBoundsBySpriteParticle(FBoundingBox& Bounds, const FBaseParticle& Particle)
	{
		const float HalfDiagonal = 0.5f * std::sqrt(
			Particle.Size.X * Particle.Size.X + Particle.Size.Y * Particle.Size.Y);
		const FVector Extent(HalfDiagonal, HalfDiagonal, HalfDiagonal);

		Bounds.Expand(Particle.Location - Extent);
		Bounds.Expand(Particle.Location + Extent);
	}

	void ExpandBoundsByTransformedBox(
		FBoundingBox& Bounds,
		const FVector& LocalCenter,
		const FVector& LocalExtent,
		const FMatrix& LocalToWorld)
	{
		const FBoundingBox LocalBounds(LocalCenter - LocalExtent, LocalCenter + LocalExtent);
		FVector Corners[8];
		LocalBounds.GetCorners(Corners);

		for (const FVector& Corner : Corners)
		{
			Bounds.Expand(LocalToWorld.TransformPositionWithW(Corner));
		}
	}

	bool ExpandBoundsByMeshEmitter(FBoundingBox& Bounds, const FParticleEmitterInstance* Instance)
	{
		if (!Instance || !Instance->CurrentLODLevel)
			return false;

		UParticleModuleTypeDataBase* TypeData = Instance->CurrentLODLevel->GetTypeDataModule();
		UParticleModuleTypeDataMesh* MeshTypeData = Cast<UParticleModuleTypeDataMesh>(TypeData);
		if (!MeshTypeData || !MeshTypeData->GetMesh())
			return false;

		FStaticMesh* MeshAsset = MeshTypeData->GetMesh()->GetStaticMeshAsset();
		if (!MeshAsset || MeshAsset->Vertices.empty())
			return false;

		if (!MeshAsset->bBoundsValid)
		{
			MeshAsset->CacheBounds();
		}

		if (!MeshAsset->bBoundsValid)
			return false;

		bool bExpanded = false;
		for (int32 ActiveIndex = 0; ActiveIndex < Instance->ActiveParticles; ++ActiveIndex)
		{
			const FBaseParticle* Particle = GetActiveParticle(Instance, ActiveIndex);
			if (!Particle)
				continue;

			const float Rotation = Particle->RotationRate * Particle->RelativeTime * Particle->Lifetime;
			const FMatrix ParticleToWorld =
				FMatrix::MakeScaleMatrix(Particle->Size)
				* FMatrix::MakeRotationZ(Rotation)
				* FMatrix::MakeTranslationMatrix(Particle->Location);

			ExpandBoundsByTransformedBox(
				Bounds,
				MeshAsset->BoundsCenter,
				MeshAsset->BoundsExtent,
				ParticleToWorld);
			bExpanded = true;
		}

		return bExpanded;
	}

	bool ExpandBoundsBySpriteEmitter(FBoundingBox& Bounds, const FParticleEmitterInstance* Instance)
	{
		if (!Instance)
			return false;

		bool bExpanded = false;
		for (int32 ActiveIndex = 0; ActiveIndex < Instance->ActiveParticles; ++ActiveIndex)
		{
			const FBaseParticle* Particle = GetActiveParticle(Instance, ActiveIndex);
			if (!Particle)
				continue;

			ExpandBoundsBySpriteParticle(Bounds, *Particle);
			bExpanded = true;
		}

		return bExpanded;
	}
}

UParticleSystemComponent::UParticleSystemComponent()
{
	ClearEmitterInstances(false);

	EmitterDelay = 0;
	DeltaTimeTick = 0;
	CustomTimeDilation = 1;

	CurrentLODLevelIndex = 0;
	TotalActiveParticles = 0;
	AccumLODDistanceCheckTime = 0.0f;
	bCalculateLODLevel = true;

	EmitterMaterials.clear();
	FrameEventQueue.clear();
	EmitterRenderData.clear();
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	bIsActive = false;
	ClearEmitterInstances(false);
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
	SCOPE_STAT_CAT("ParticleSystemComponent::Tick", "Particles");

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

	const float RawDeltaTime = (GEngine && GEngine->GetTimer()) ? GEngine->GetTimer()->GetRawDeltaTime() : DeltaTime;
	DeltaTimeTick = DeltaTime * CustomTimeDilation;

	// LOD 자동 거리 검사 (Automatic 모드, 레벨 뷰포트에서만 — EditorPreview는 수동 선택 우선)
	UWorld* OwnerWorld = GetOwner() ? GetOwner()->GetWorld() : nullptr;
	const bool bIsPreviewWorld = OwnerWorld && OwnerWorld->GetWorldType() == EWorldType::EditorPreview;
	if (bCalculateLODLevel && !bIsPreviewWorld && Template->GetLODMethod() == EParticleSystemLODMethod::Automatic)
	{
		AccumLODDistanceCheckTime += DeltaTimeTick;
		if (AccumLODDistanceCheckTime >= Template->LODDistanceCheckTime)
		{
			AccumLODDistanceCheckTime = 0.0f;
			const FVector EffectLocation = GetWorldLocation();
			const int32 NewLOD = CalculateLODLevel(EffectLocation);
			SetLODLevel(NewLOD);
		}
	}

	// EmitterDelay 카운트다운 — 0 이하가 될 때까지 Emitter tick 스킵
	if (EmitterDelay > 0.0f)
	{
		EmitterDelay -= DeltaTimeTick;
		if (EmitterDelay > 0.0f)
			return;
		EmitterDelay = 0.0f;
	}

	//이벤트 버퍼 정리
	FrameEventQueue.clear();
	TotalActiveParticles  = 0;
	TotalSpawnedThisFrame = 0;
	TotalKilledThisFrame  = 0;
	SystemElapsedTime    += DeltaTime;

	{
		SCOPE_STAT_CAT("ParticleEmitters::Simulate", "Particles");

		for (auto instance : EmitterInstances) {
			if (!instance) continue;
			instance->Tick(DeltaTimeTick, FrameEventQueue, RawDeltaTime);
			TotalSpawnedThisFrame += instance->SpawnedThisFrame;
			TotalKilledThisFrame  += instance->KilledThisFrame;
		}
	}

#if defined(_DEBUG)
	if (bDebugEventTrace && !FrameEventQueue.empty())
	{
		UE_LOG("[ParticleEvent][Component %p] queued %zu event(s) this frame.", this, FrameEventQueue.size());
	}
#endif

	{
		SCOPE_STAT_CAT("ParticleEmitters::ProcessEvents", "Particles");

		for (auto instance : EmitterInstances) {
			if (!instance) continue;
			instance->ProcessEvents(FrameEventQueue);
			TotalActiveParticles += instance->ActiveParticles;
		}
	}

#if defined(_DEBUG)
	if (bDebugEventTrace && !FrameEventQueue.empty())
	{
		UE_LOG("[ParticleEvent][Component %p] finished dispatch. activeParticles=%d", this, TotalActiveParticles);
	}
#endif

	// RenderData 수집
	MarkWorldBoundsDirty();
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
	bool bHasParticleBounds = false;

	for (const FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance || Instance->ActiveParticles <= 0)
			continue;

		switch (GetEmitterType(Instance))
		{
		case EParticleEmitterType::PET_Mesh:
			bHasParticleBounds |= ExpandBoundsByMeshEmitter(ParticleBounds, Instance);
			break;

		case EParticleEmitterType::PET_Sprite:
			bHasParticleBounds |= ExpandBoundsBySpriteEmitter(ParticleBounds, Instance);
			break;

		case EParticleEmitterType::PET_Beam:
		case EParticleEmitterType::PET_Ribbon:
		default:
			break;
		}
	}

	if (!bHasParticleBounds || !ParticleBounds.IsValid())
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
	if (!Template || InLODLevel < 0)
		return;

	// 에미터들이 실제로 가진 LOD 레벨 수 기준으로 상한 결정
	int32 MaxLOD = Template->LODDistances.empty()
		? 0
		: static_cast<int32>(Template->LODDistances.size()) - 1;
	for (const UParticleEmitter* Emitter : Template->GetEmitters())
	{
		if (Emitter)
			MaxLOD = std::min(MaxLOD, Emitter->GetLODLevelCount() - 1);
	}
	MaxLOD = std::max(0, MaxLOD);
	const int32 NewLODLevel = std::clamp(InLODLevel, 0, MaxLOD);

	if (CurrentLODLevelIndex == NewLODLevel)
		return;

	CurrentLODLevelIndex = NewLODLevel;

	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance)
			Instance->SetCurrentLODIndex(CurrentLODLevelIndex, false);
	}

	MarkProxyDirty(EDirtyFlag::Mesh);
}

int32 UParticleSystemComponent::CalculateLODLevel(const FVector& EffectLocation) const
{
	if (!Template || Template->LODDistances.empty())
		return 0;

	UWorld* World = GetOwner() ? GetOwner()->GetWorld() : nullptr;
	if (!World)
		return 0;

	FMinimalViewInfo POV;
	if (!World->GetActivePOV(POV))
		return 0;

	const float Dist = (EffectLocation - POV.Location).Length();

	// LODDistances[i]가 LOD i의 시작 거리
	// 거리 조건에 맞는 가장 높은(멀리 있는) LOD 인덱스 반환
	const int32 NumLODs = static_cast<int32>(Template->LODDistances.size());
	int32 BestLOD = 0;
	for (int32 i = 1; i < NumLODs; ++i)
	{
		if (Dist >= Template->LODDistances[i])
			BestLOD = i;
		else
			break;
	}

	return BestLOD;
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
		return new FParticleBeamEmitterInstance();

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

		// 에미터가 해당 LOD 레벨을 가지고 있지 않으면 0으로 폴백
		const int32 SafeLODIndex = Emitter->GetLODLevel(CurrentLODLevelIndex) ? CurrentLODLevelIndex : 0;
		Instance->CurrentLODLevelIndex = SafeLODIndex;
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
	SystemElapsedTime     = 0.0f;
	TotalSpawnedThisFrame = 0;
	TotalKilledThisFrame  = 0;

	// Asset의 Delay 설정 적용
	if (Template->GetUseDelayRange())
	{
		float Lo = Template->GetDelayLow();
		float Hi = Template->GetDelay();
		if (Lo > Hi) std::swap(Lo, Hi);
		EmitterDelay = Lo + (static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * (Hi - Lo);
	}
	else
	{
		EmitterDelay = Template->GetDelay();
	}

	// LOD 초기화
	AccumLODDistanceCheckTime = 0.0f;
	const EParticleSystemLODMethod LODMethod = Template->GetLODMethod();
	bCalculateLODLevel = (LODMethod == EParticleSystemLODMethod::Automatic);

	if (LODMethod == EParticleSystemLODMethod::Automatic ||
		LODMethod == EParticleSystemLODMethod::ActivateAutomatic)
	{
		const FVector EffectLocation = GetWorldLocation();
		const int32 InitLOD = CalculateLODLevel(EffectLocation);
		SetLODLevel(InitLOD);
	}
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
	FrameEventQueue.clear();
	TotalActiveParticles = 0;
	ClearRenderData();
	MarkWorldBoundsDirty();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

void UParticleSystemComponent::ClearEmitterInstances(bool bNotifyRender)
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance)
		{
			delete Instance;
		}
	}

	EmitterInstances.clear();
	FrameEventQueue.clear();
	TotalActiveParticles = 0;
	ClearRenderData();
	if (bNotifyRender)
	{
		MarkWorldBoundsDirty();
		MarkProxyDirty(EDirtyFlag::Mesh);
	}
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
	SCOPE_STAT_CAT("ParticleSystemComponent::BuildRenderData", "Particles");

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
