#include "ParticleSystemComponent.h"
#include "Particles/Runtime/ParticleEmitterInstance.h"
#include "Particles/Rendering/ParticleRenderData.h"
#include "Render/Proxy/ParticleSceneProxy.h"
#include "Particles/Assets/ParticleSystemAssetManager.h"
#include "Core/Log.h"

#include <algorithm>
#include <Platform/Paths.h>

UParticleSystemComponent::UParticleSystemComponent()
{
	ClearEmitterInstances();

	EmitterDelay = 0;
	DeltaTimeTick = 0;
	CustomTimeDilation = 1;

	CurrentLODLevelIndex = 0;
	TotalActiveParticles = 0;

	EmitterMaterials.clear();
	FrameEventQueue.clear();
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
	FrameEventQueue.clear();
	TotalActiveParticles = 0;

	// 인스턴스 tick
	for (auto instance : EmitterInstances) {
		if (!instance) continue;
		instance->Tick(DeltaTimeTick, FrameEventQueue);
	}

#if defined(_DEBUG)
	if (bDebugEventTrace && !FrameEventQueue.empty())
	{
		UE_LOG("[ParticleEvent][Component %p] queued %zu event(s) this frame.", this, FrameEventQueue.size());
	}
#endif

	for (auto instance : EmitterInstances) {
		if (!instance) continue;
		instance->ProcessEvents(FrameEventQueue);
		TotalActiveParticles += instance->ActiveParticles;
	}

#if defined(_DEBUG)
	if (bDebugEventTrace && !FrameEventQueue.empty())
	{
		UE_LOG("[ParticleEvent][Component %p] finished dispatch. activeParticles=%d", this, TotalActiveParticles);
	}
#endif

	// RenderData 수집
	BuildRenderData();
	MarkProxyDirty(EDirtyFlag::Mesh);
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSceneProxy(this);
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
}

void UParticleSystemComponent::CreateEmitterInstances()
{
	if (!Template) {
		return;
	}

	ClearEmitterInstances();

	const TArray<UParticleEmitter*> TemplateEmitters = Template->GetEmitters();
	for (auto Emitter : TemplateEmitters) {
		if (!Emitter)
			continue;

		EParticleEmitterType EmitterType = EParticleEmitterType::PET_Sprite;

		if (UParticleLODLevel* LOD = Emitter->GetLODLevel(0))
		{
			if (UParticleModuleTypeDataBase* TypeData = LOD->GetTypeDataModule())
			{
				EmitterType = TypeData->GetEmitterType();
			}
		}

		FParticleEmitterInstance* Instance = nullptr;

		switch (EmitterType)
		{
		case EParticleEmitterType::PET_Sprite:
			Instance = new FParticleEmitterInstance();
			break;

		case EParticleEmitterType::PET_Mesh:
			Instance = new FParticleMeshEmitterInstance();
			break;

		case EParticleEmitterType::PET_Beam:
		case EParticleEmitterType::PET_Ribbon:
		default:
			Instance = new FParticleEmitterInstance();
			break;
		}

		if (!Instance)
		{
			Instance = new FParticleEmitterInstance();
		}
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
	FrameEventQueue.clear();
	TotalActiveParticles = 0;
	ClearRenderData();
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
	FrameEventQueue.clear();
	TotalActiveParticles = 0;
	ClearRenderData();
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
