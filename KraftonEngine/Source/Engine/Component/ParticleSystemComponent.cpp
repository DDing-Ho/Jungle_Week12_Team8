#include "ParticleSystemComponent.h"
#include "Particles/Runtime/ParticleEmitterInstance.h"
#include "Particles/Rendering/ParticleRenderData.h"

void UParticleSystemComponent::InitializeComponent()
{
}

void UParticleSystemComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	if (Template == nullptr || Template->GetEmitters().size() == 0)
	{
		// Disable our tick here, will be enabled when activating
		SetComponentTickEnabled(false);
		return;
	}

	if ((IsActive() == false) && (bAutoActivate == false))
	{
		// Disable our tick here, will be enabled when activating
		SetComponentTickEnabled(false);
		return;
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

	// 인스턴스 tick
	for (auto instance : EmitterInstances) {
		if (!instance) continue;
		instance->Tick(DeltaTimeTick);
		TotalActiveParticles += instance->ActiveParticles;
	}

	// RenderData 수집
	// BuildRenderData();
	MarkRenderStateDirty();
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* InTemplate)
{
	if (!InTemplate || Template == InTemplate) {
		return;
	}

	DeactivateSystem();

	Template = InTemplate;
	CreateEmitterInstances();
}

void UParticleSystemComponent::SetLODLevel(int32 InLODLevel)
{
	if (!Template)
		return;

	const int32 MaxLOD = Template->LODDistances.size() - 1;
	if (MaxLOD < 0)
		return;

	const int32 NewLODLevel = FMath::Clamp(InLODLevel, 0, MaxLOD);

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

	MarkRenderStateDirty();
}

void UParticleSystemComponent::CreateEmitterInstances()
{
	if (!Template) {
		return;
	}

	const TArray<UParticleEmitter*> TemplateEmitters = Template->GetEmitters();
	for (auto Emitter : TemplateEmitters) {
		FParticleEmitterInstance* Instance = new FParticleEmitterInstance();
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
}

void UParticleSystemComponent::ResetSystem()
{
	for (FParticleEmitterInstance* instance : EmitterInstances) {
		instance->Reset();
	}
}

void UParticleSystemComponent::SendRenderDynamicData()
{
	if (!SceneProxy)
		return;

	FDynamicEmitterReplayDataBase* NewDynamicData = new FDynamicEmitterReplayDataBase();

	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance)
			continue;

		Instance->CreateDynamicEmitterData(*NewDynamicData);
	}
}