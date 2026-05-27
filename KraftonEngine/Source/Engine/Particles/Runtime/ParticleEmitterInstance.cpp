#include "ParticleEmitterInstance.h"
#include "Particles/Common/ParticleHelper.h"
#include "Particles/Modules/ParticleEventModules.h"
#include "Particles/Modules/ParticleRenderExpressionModules.h"
#include "Core/EngineTypes.h"
#include "Component/ParticleSystemComponent.h"
#include "Math/Vector.h"
#include "Mesh/StaticMesh.h"

#include <utility>
#include <Core/Log.h>

namespace
{
#if defined(_DEBUG)
	const char* ParticleEventTypeToString(EParticleEventType EventType)
	{
		switch (EventType)
		{
		case EParticleEventType::PEET_Spawn:     return "Spawn";
		case EParticleEventType::PEET_Death:     return "Death";
		case EParticleEventType::PEET_Collision: return "Collision";
		// case EParticleEventType::PEET_Burst:     return "Burst";
		case EParticleEventType::PEET_Custom:    return "Custom";
		default:                                 return "Unknown";
		}
	}
#endif

	int32 GetModulePayloadOffset(const UParticleEmitter* EmitterTemplate, const UParticleModule* Module)
	{
		return EmitterTemplate ? EmitterTemplate->GetModulePayloadOffset(Module) : INDEX_NONE;
	}

	int32 FindModulePayloadOffset(const UParticleEmitter* EmitterTemplate, const UParticleLODLevel* LODLevel, EParticleModuleClass ModuleClass)
	{
		if (!EmitterTemplate || !LODLevel)
			return INDEX_NONE;

		for (UParticleModule* Module : LODLevel->GetModules())
		{
			if (Module && Module->GetModuleClass() == ModuleClass)
				return EmitterTemplate->GetModulePayloadOffset(Module);
		}

		return INDEX_NONE;
	}
}

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	delete[] ParticleData;
	ParticleData = nullptr;

	delete[] ParticleIndices;
	ParticleIndices = nullptr;

	delete[] InstanceData;
	InstanceData = nullptr;
}

void FParticleEmitterInstance::Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate)
{
	if(!InComponent || !InTemplate) return;

	OwnerComponent = InComponent;
	EmitterTemplate = InTemplate;
	EmitterTemplate->CacheEmitterModuleInfo();
	CurrentLODLevel = InTemplate->GetLODLevel(CurrentLODLevelIndex);
	if (!CurrentLODLevel)
	{
		// 요청한 LOD 레벨이 없으면 LOD 0으로 폴백
		CurrentLODLevelIndex = 0;
		CurrentLODLevel = InTemplate->GetLODLevel(0);
	}
	if (!CurrentLODLevel)
		return;

	MaxActiveParticles = EmitterTemplate->GetMaxActiveParticles();

	PayloadOffset = sizeof(FBaseParticle);

	ParticleSize = static_cast<int32>(std::max<size_t>(
		static_cast<size_t>(EmitterTemplate->GetParticleSize()), sizeof(FBaseParticle)));

	ParticleStride = ParticleSize;

	ParticleData = new uint8[ParticleStride * MaxActiveParticles];
	ParticleIndices = new uint16[MaxActiveParticles];

	memset(ParticleData, 0, ParticleStride * MaxActiveParticles);
	for (int32 i = 0; i < MaxActiveParticles; ++i)
	{
		ParticleIndices[i] = static_cast<uint16>(i);
	}

	ActiveParticles = 0;
	ParticleCounter = 0;
	SpawnFraction = 0.0f;
	bFirstTime = true;
	bEnabled = true;
	LoopCount = 0;
	EmitterTime = 0.0f;
	LastDeltaTime = 0.0f;
	RealDeltaTime = 0.0f;

	// 각 모듈의 RandomSeedInfo를 바탕으로 모듈 전용 스트림 초기화
	for (UParticleModule* Module : CurrentLODLevel->GetModules())
	{
		if (Module)
			Module->InitializeStream();
	}
}

void FParticleEmitterInstance::Tick(float DeltaTime, TArray<FParticleEventData>& OutEventQueue, float InRealDeltaTime)
{
	if (!CurrentLODLevel)
		return;

	if(!bEnabled) return;

	RealDeltaTime = InRealDeltaTime >= 0.0f ? InRealDeltaTime : DeltaTime;

	SpawnedThisFrame = 0;
	KilledThisFrame  = 0;

	// 이미터 시간 갱신
	EmitterTime += DeltaTime;

	//life타임 없는 애들 삭제 하는 로직
	KillExpiredParticles(OutEventQueue);

	ResetParticleParameters(DeltaTime);

	for (UParticleModule* Module : CurrentLODLevel->GetUpdateModules())
	{
		if (Module && Module->IsEnabled())
			Module->Update(this, DeltaTime, GetModulePayloadOffset(EmitterTemplate, Module), &OutEventQueue);
	}

	//시간되었으면 spawn하는 로직
	Tick_SpawnParticles(DeltaTime, &OutEventQueue);

	if(!bFirstTime){
		//if (ActiveParticles > 0) UpdateBoundingBox(DeltaTime);
		// 원래는 해당 로직 안에서 파티클 이동 진행
		BEGIN_PARTICLE_UPDATE_LOOP
			Particle.Location += Particle.Velocity * DeltaTime;
		END_PARTICLE_UPDATE_LOOP
	}
}

//StartTime : 현재 프레임에서 첫 번째 파티클이 태어날 때까지 프레임 단위 시간
void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity, TArray<FParticleEventData>* OutEventQueue)
{
	for (int32 i = 0; i < Count; ++i)
	{
		if (ActiveParticles >= MaxActiveParticles)
			break;
		const int32 ActiveIndex = ActiveParticles;

		DECLARE_PARTICLE_PTR(ActiveIndex);

		PreSpawn(Particle, InitialLocation, InitialVelocity);	

		const float SpawnTime = StartTime - Increment * i;

		for (UParticleModule* Module : CurrentLODLevel->GetSpawnModules())
		{
			if (Module && Module->IsEnabled())
			{
				Module->Spawn(this, Particle, SpawnTime, GetModulePayloadOffset(EmitterTemplate, Module));
			}
		}

		PostSpawn(Particle, SpawnTime, OutEventQueue);
		++SpawnedThisFrame;
	}
}

void FParticleEmitterInstance::KillParticle(int32 Index)
{
	if (Index < 0 || Index >= ActiveParticles)
		return;

	const int32 LastActiveIndex = ActiveParticles - 1;

	if (Index != LastActiveIndex)
	{
		const int32 DeadParticleSlot = ParticleIndices[Index];

		ParticleIndices[Index] = ParticleIndices[LastActiveIndex];
		ParticleIndices[LastActiveIndex] = DeadParticleSlot;
	}

	ActiveParticles--;
	++KilledThisFrame;
}

void FParticleEmitterInstance::KillParticleWithEvents(int32 Index, TArray<FParticleEventData>* OutEventQueue)
{
	if (Index < 0 || Index >= ActiveParticles)
		return;

	if (OutEventQueue && ParticleData && ParticleIndices && ParticleStride > 0)
	{
		const int32 RealIndex = ParticleIndices[Index];
		const FBaseParticle& Particle =
			*reinterpret_cast<const FBaseParticle*>(ParticleData + ParticleStride * RealIndex);
		PublishParticleEvents(EParticleEventType::PEET_Death, Particle, Index, OutEventQueue);
	}

	KillParticle(Index);
}

void FParticleEmitterInstance::KillAllParticles(TArray<FParticleEventData>* OutEventQueue)
{
	for (int32 Index = ActiveParticles - 1; Index >= 0; --Index)
	{
		KillParticleWithEvents(Index, OutEventQueue);
	}
}

void FParticleEmitterInstance::SetCurrentLODIndex(int32 NewIndex, bool bFullReset)
{
	if (!EmitterTemplate) return;
	if (NewIndex < 0) return;

	UParticleLODLevel* NewLODLevel = EmitterTemplate->GetLODLevel(NewIndex);
	if (!NewLODLevel || !NewLODLevel->IsEnabled()) return;

	CurrentLODLevelIndex = NewIndex;
	CurrentLODLevel = NewLODLevel;

	if (bFullReset)
	{
		KillAllParticles();
		EmitterTime = 0.0f;
		SpawnFraction = 0.0f;
		bFirstTime = true;
	}
}

void FParticleEmitterInstance::Reset()
{
	EmitterTime = 0;
	LastDeltaTime = 0;
	RealDeltaTime = 0;
	LoopCount = 0;
	ParticleCounter = 0;
	ActiveParticles = 0;
	SpawnFraction = 0.0f;
	bFirstTime = true;
	bEnabled = true;
	ReceivedEvents.clear();

	if (CurrentLODLevel)
	{
		for (UParticleModule* Module : CurrentLODLevel->GetModules())
		{
			if (Module)
			{
				Module->InitializeStream();
			}
		}
	}
}

void FParticleEmitterInstance::ResetParticleParameters(float DeltaTime)
{
	if (!CurrentLODLevel)
		return;

	if (!EmitterTemplate)
		return;

	if (!ParticleData)
		return;

	for (int32 ParticleIndex = 0; ParticleIndex < ActiveParticles; ++ParticleIndex)
	{
		const int32 RealIndex = ParticleIndices[ParticleIndex];

		uint8* ParticleBase =
			ParticleData + ParticleStride * RealIndex;

		FBaseParticle* Particle =
			reinterpret_cast<FBaseParticle*>(ParticleBase);

		// 모듈 업데이트 전에 기본 상태로 리셋
		Particle->Velocity = Particle->BaseVelocity;
		Particle->Size = Particle->BaseSize;
		Particle->RotationRate = Particle->BaseRotationRate;
		Particle->Color = Particle->BaseColor;

		if (Particle->Lifetime > 0.0f)
		{
			Particle->RelativeTime += DeltaTime / Particle->Lifetime;
		}
	}
}

void FParticleEmitterInstance::Tick_SpawnParticles(float DeltaTime, TArray<FParticleEventData>* OutEventQueue)
{
	if (!CurrentLODLevel || !bEnabled)
		return;

	if (EmitterTime < 0.0f)
		return;

	float SpawnRate = 0.0f;

	for (UParticleModule* Module : CurrentLODLevel->GetSpawnModules())
	{
		if (!Module || !Module->IsEnabled())
			continue;

		if (Module->GetModuleType() == EParticleModuleType::PMT_Spawn)
		{
			UParticleModuleSpawn* SpawnModule = static_cast<UParticleModuleSpawn*>(Module);
			SpawnRate += std::max(0.0f, SpawnModule->GetSpawnRate(EmitterTime));
		}
	}

	const float NewLeftover = SpawnFraction + DeltaTime * SpawnRate;
	int32 SpawnCount = static_cast<int32>(floor(NewLeftover));
	float NewSpawnFraction = NewLeftover - SpawnCount;

	const int32 AvailableSlots = MaxActiveParticles - ActiveParticles;
	const int32 TotalSpawnCount = std::min(SpawnCount, AvailableSlots);

	if (TotalSpawnCount > 0)
	{
		const float Increment = SpawnRate > 0.0f ? 1.0f / SpawnRate : 0.0f;
		const float StartTime = DeltaTime + SpawnFraction * Increment - Increment;

		const FVector InitialLocation = OwnerComponent
			? OwnerComponent->GetWorldLocation()
			: FVector::ZeroVector;

		SpawnParticles(
			TotalSpawnCount,
			StartTime,
			Increment,
			InitialLocation,
			FVector::ZeroVector,
			OutEventQueue
		);
	}

	bFirstTime = false;
	SpawnFraction = NewSpawnFraction;
}

FDynamicEmitterDataBase* FParticleEmitterInstance::CreateDynamicEmitterData()
{
	return nullptr;
}

void FParticleEmitterInstance::PreSpawn(FBaseParticle& Particle, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	std::memset(&Particle, 0, ParticleStride);

	Particle.Location = InitialLocation;
	Particle.Velocity = InitialVelocity;
	Particle.BaseVelocity = InitialVelocity;
	
	//TODO: 모듈 생성시 아래 초기화 없앨것
	//현재 모듈이 없어서 초기화 불가능해서 일단 여기서 진행 
	Particle.Color = FColor::White();
	Particle.BaseColor = Particle.Color;
	Particle.Size = FVector(1.0f, 1.0f, 1.0f);
	Particle.BaseSize = Particle.Size;
	Particle.RotationRate = 0.0f;
	Particle.BaseRotationRate = Particle.RotationRate;

	for (UParticleModule* Module : CurrentLODLevel->GetSpawnModules())
	{
		if (Module && Module->IsEnabled())
			Module->PreSpawn(this, Particle, GetModulePayloadOffset(EmitterTemplate, Module));
	}
}

void FParticleEmitterInstance::PostSpawn(FBaseParticle& Particle, float SpawnTime, TArray<FParticleEventData>* OutEventQueue)
{
	/*if (CurrentLODLevel->GetRequiredModule()->bUseLocalSpace == false)
	{
		if (FVector::DistSquared(OldLocation, Location) > 1.f)
		{
			Particle->Location += InterpolationPercentage * (OldLocation - Location);
		}
	}*/
	// Offset caused by any velocity
	Particle.Location += FVector(Particle.Velocity) * SpawnTime;

	++ActiveParticles;
	++ParticleCounter;
	PublishParticleEvents(EParticleEventType::PEET_Spawn, Particle, ActiveParticles - 1, OutEventQueue);
}

void FParticleEmitterInstance::KillExpiredParticles(TArray<FParticleEventData>& OutEventQueue)
{
	BEGIN_PARTICLE_UPDATE_LOOP
	if (Particle.Lifetime > 0.0f && Particle.RelativeTime >= 1.0f)
	{
		KillParticleWithEvents(Index, &OutEventQueue);
	}
	END_PARTICLE_UPDATE_LOOP
}

void FParticleEmitterInstance::ProcessEvents(TArray<FParticleEventData>& EventQueue)
{
	ReceivedEvents.clear();

	if (!CurrentLODLevel || EventQueue.empty())
		return;

	for (const FParticleEventData& EventData : EventQueue)
	{
		if (HasReceiverFor(EventData))
		{
			ReceivedEvents.push_back(EventData);
		}
	}

#if defined(_DEBUG)
	if (OwnerComponent && OwnerComponent->IsEventTraceEnabled() && !ReceivedEvents.empty())
	{
		UE_LOG("[ParticleEvent][Emitter %d] received %zu / %zu event(s).",
			ResolveEmitterIndex(),
			ReceivedEvents.size(),
			EventQueue.size());
	}
#endif

	ProcessReceivedEvents(&EventQueue);
}

bool FParticleEmitterInstance::HasReceiverFor(const FParticleEventData& Event) const
{
	if (!CurrentLODLevel)
		return false;

	for (UParticleModule* Module : CurrentLODLevel->GetUpdateModules())
	{
		if (!Module || !Module->IsEnabled())
			continue;

		const EParticleModuleClass ModClass = Module->GetModuleClass();
		if (ModClass != EParticleModuleClass::EventReceiverSpawn &&
			ModClass != EParticleModuleClass::EventReceiverKillAll)
			continue;

		const UParticleModuleEventReceiverBase* Receiver = static_cast<const UParticleModuleEventReceiverBase*>(Module);
		if (Receiver->MatchesEvent(Event))
			return true;
	}

	return false;
}

void FParticleEmitterInstance::ProcessReceivedEvents(TArray<FParticleEventData>* OutEventQueue)
{
	if (!CurrentLODLevel || ReceivedEvents.empty())
		return;

	for (UParticleModule* Module : CurrentLODLevel->GetUpdateModules())
	{
		if (!Module || !Module->IsEnabled())
			continue;

		const EParticleModuleClass ModClass = Module->GetModuleClass();
		if (ModClass != EParticleModuleClass::EventReceiverSpawn &&
			ModClass != EParticleModuleClass::EventReceiverKillAll)
			continue;

		UParticleModuleEventReceiverBase* Receiver = static_cast<UParticleModuleEventReceiverBase*>(Module);
		for (const FParticleEventData& EventData : ReceivedEvents)
		{
			if (Receiver->MatchesEvent(EventData))
			{
#if defined(_DEBUG)
				if (OwnerComponent && OwnerComponent->IsEventTraceEnabled())
				{
					UE_LOG("[ParticleEvent][Emitter %d] handling %s event name=%s sourceEmitter=%d sourceParticle=%d",
						ResolveEmitterIndex(),
						ParticleEventTypeToString(EventData.Type),
						EventData.EventName.ToString().c_str(),
						EventData.SourceEmitterIndex,
						EventData.SourceParticleIndex);
				}
#endif

				Receiver->ProcessEvent(*this, EventData, OutEventQueue);
			}
		}
	}
}

FBaseParticle& FParticleEmitterInstance::GetParticle(int32 index)
{
	return *reinterpret_cast<FBaseParticle*>(ParticleData + ParticleStride * ParticleIndices[index]);
}

void FParticleEmitterInstance::PublishParticleEvents(EParticleEventType EventType, const FBaseParticle& Particle, int32 ParticleIndex, TArray<FParticleEventData>* OutEventQueue, const FVector& EventNormal)
{
	if (!CurrentLODLevel || !OutEventQueue)
		return;

	const int32 SourceEmitterIndex = ResolveEmitterIndex();

	for (UParticleModule* Module : CurrentLODLevel->GetModules())
	{
		if (!Module || !Module->IsEnabled() || Module->GetModuleClass() != EParticleModuleClass::EventGenerator)
			continue;

		UParticleModuleEventGenerator* GeneratorModule = static_cast<UParticleModuleEventGenerator*>(Module);
		for (const FParticleEventGeneratorEntry& GeneratedEvent : GeneratorModule->GetGeneratedEvents())
		{
			if (GeneratedEvent.Type != EventType)
				continue;

			FParticleEventData EventData;
			EventData.Type = EventType;
			EventData.EventName = GeneratedEvent.EventName;
			EventData.Location = Particle.Location;
			EventData.Velocity = Particle.Velocity;
			EventData.Normal = EventNormal;
			EventData.SourceEmitterIndex = SourceEmitterIndex;
			EventData.SourceParticleIndex = ParticleIndex;
			OutEventQueue->push_back(EventData);

#if defined(_DEBUG)
			if (OwnerComponent && OwnerComponent->IsEventTraceEnabled())
			{
				UE_LOG("[ParticleEvent][Emitter %d] published %s event name=%s particle=%d loc=(%.2f, %.2f, %.2f) vel=(%.2f, %.2f, %.2f)",
					SourceEmitterIndex,
					ParticleEventTypeToString(EventType),
					EventData.EventName.ToString().c_str(),
					ParticleIndex,
					EventData.Location.X, EventData.Location.Y, EventData.Location.Z,
					EventData.Velocity.X, EventData.Velocity.Y, EventData.Velocity.Z);
			}
#endif
		}
	}
}

int32 FParticleEmitterInstance::ResolveEmitterIndex() const
{
	if (!OwnerComponent)
		return INDEX_NONE;

	const TArray<FParticleEmitterInstance*>& Instances = OwnerComponent->GetEmitterInstances();
	for (int32 Index = 0; Index < static_cast<int32>(Instances.size()); ++Index)
	{
		if (Instances[Index] == this)
			return Index;
	}

	return INDEX_NONE;
}

// Sprite Paticle============================================================
FDynamicEmitterDataBase* FParticleSpriteEmitterInstance::CreateDynamicEmitterData()
{
	if (ActiveParticles <= 0)
		return nullptr;

	if (!ParticleData || !ParticleIndices)
		return nullptr;

	if (!CurrentLODLevel)
		return nullptr;

	FDynamicSpriteEmitterData* Data = new FDynamicSpriteEmitterData();

	FDynamicSpriteEmitterReplayData& Source = Data->Source;

	Source.eEmitterType = EDynamicEmitterType::DET_Sprite;
	Source.ActiveParticleCount = ActiveParticles;
	Source.ParticleStride = ParticleStride;
	Source.RotationModuleOffset = FindModulePayloadOffset(EmitterTemplate, CurrentLODLevel, EParticleModuleClass::Rotation);
	Source.Scale = FVector(1.0f, 1.0f, 1.0f);

	UParticleModuleRequired* RequiredModule = CurrentLODLevel->GetRequiredModule();
	Source.RequiredModule = RequiredModule;

	if (RequiredModule)
	{
		Source.Material = RequiredModule->GetMaterial();
		Source.SortMode = RequiredModule->GetSortMode();
		Source.TranslucencySortPriority = RequiredModule->GetTranslucencySortPriority();
		Source.SubUVInterpolationMethod = RequiredModule->GetSubUVInterpolationMethod();
		Source.SubUVHorizontalCount = RequiredModule->GetSubImagesHorizontal();
		Source.SubUVVerticalCount = RequiredModule->GetSubImagesVertical();
	}

	for (UParticleModule* Module : CurrentLODLevel->GetModules())
	{
		UParticleModuleSubUVBase* SubUVModule = Cast<UParticleModuleSubUVBase>(Module);
		if (SubUVModule && SubUVModule->IsEnabled())
		{
			Source.bUseSubUV = RequiredModule
				&& RequiredModule->GetSubUVInterpolationMethod() != EParticleSubUVInterpMethod::PSUVIM_None;
			break;
		}
	}

	Source.DataContainer.Allocate(ParticleStride, ActiveParticles);

	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		uint16 SrcIndex = ParticleIndices[i];

		memcpy(
			Source.DataContainer.ParticleData + ParticleStride * i,
			ParticleData + ParticleStride * SrcIndex,
			ParticleStride
		);

		Source.DataContainer.ParticleIndices[i] = i;
	}

	return Data;
}



// Mesh Paticle============================================================
void FParticleMeshEmitterInstance::Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate)
{
	FParticleEmitterInstance::Init(InComponent, InTemplate);

	MeshTypeData = nullptr;
	if (CurrentLODLevel)
	{
		MeshTypeData = Cast<UParticleModuleTypeDataMesh>(CurrentLODLevel->GetTypeDataModule());
	}
}

void FParticleMeshEmitterInstance::Tick(float DeltaTime, TArray<FParticleEventData>& OutEventQueue, float InRealDeltaTime)
{
	FParticleEmitterInstance::Tick(DeltaTime, OutEventQueue, InRealDeltaTime);

	//MeshRotation및 payload계산
}

FDynamicEmitterDataBase* FParticleMeshEmitterInstance::CreateDynamicEmitterData()
{
	if (ActiveParticles <= 0)
		return nullptr;

	if (!ParticleData || !ParticleIndices)
		return nullptr;

	if (!CurrentLODLevel)
		return nullptr;

	if (!MeshTypeData || !MeshTypeData->GetMesh())
	{
		UE_LOG("MeshTypeData or Mesh is null, cannot create Mesh Emitter Data.");
		return nullptr;
	}

	FDynamicMeshEmitterData* Data = new FDynamicMeshEmitterData();

	FDynamicMeshEmitterReplayData& Source = Data->Source;

	Source.eEmitterType = EDynamicEmitterType::DET_Mesh;
	Source.ActiveParticleCount = ActiveParticles;
	Source.ParticleStride = ParticleStride;
	Source.RotationModuleOffset = FindModulePayloadOffset(EmitterTemplate, CurrentLODLevel, EParticleModuleClass::Rotation);
	Source.Scale = FVector(1.0f, 1.0f, 1.0f);
	Source.Mesh = MeshTypeData->GetMesh();

	UParticleModuleRequired* RequiredModule = CurrentLODLevel->GetRequiredModule();
	Source.RequiredModule = RequiredModule;

	if (RequiredModule)
	{
		Source.Material = RequiredModule->GetMaterial();
		Source.SortMode = RequiredModule->GetSortMode();
		Source.TranslucencySortPriority = RequiredModule->GetTranslucencySortPriority();
	}

	if (!Source.Material && Source.Mesh)
	{
		const TArray<FStaticMaterial>& StaticMaterials = Source.Mesh->GetStaticMaterials();
		if (!StaticMaterials.empty())
		{
			Source.Material = StaticMaterials[0].MaterialInterface;
		}
	}

	Source.DataContainer.Allocate(ParticleStride, ActiveParticles);

	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		uint16 SrcIndex = ParticleIndices[i];

		memcpy(
			Source.DataContainer.ParticleData + ParticleStride * i,
			ParticleData + ParticleStride * SrcIndex,
			ParticleStride
		);

		Source.DataContainer.ParticleIndices[i] = i;
	}

	return Data;
}

void FParticleBeamEmitterInstance::Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate)
{
	FParticleEmitterInstance::Init(InComponent, InTemplate);

	BeamTypeData = nullptr;
	if (CurrentLODLevel)
	{
		BeamTypeData = Cast<UParticleModuleTypeDataBeam>(CurrentLODLevel->GetTypeDataModule());
	}
}

FDynamicEmitterDataBase* FParticleBeamEmitterInstance::CreateDynamicEmitterData()
{
	if (ActiveParticles <= 0)
		return nullptr;

	if (!ParticleData || !ParticleIndices)
		return nullptr;

	if (!CurrentLODLevel)
		return nullptr;

	if (!BeamTypeData)
	{
		UE_LOG("BeamTypeData is null, cannot create Beam Emitter Data.");
		return nullptr;
	}

	FDynamicBeamEmitterData* Data = new FDynamicBeamEmitterData();
	FDynamicBeamEmitterReplayData& Source = Data->Source;

	Source.eEmitterType = EDynamicEmitterType::DET_Beam;
	Source.ActiveParticleCount = ActiveParticles;
	Source.ParticleStride = ParticleStride;
	Source.RotationModuleOffset = FindModulePayloadOffset(EmitterTemplate, CurrentLODLevel, EParticleModuleClass::Rotation);
	Source.Scale = FVector(1.0f, 1.0f, 1.0f);
	Source.Source = BeamTypeData->GetSource();
	Source.Target = BeamTypeData->GetTarget();
	Source.Width = BeamTypeData->GetWidth();
	Source.TextureTiling = BeamTypeData->GetTextureTiling();
	const int32 BeamPayloadOffset = GetModulePayloadOffset(EmitterTemplate, BeamTypeData);
	Source.PayloadOffset = BeamPayloadOffset != INDEX_NONE ? BeamPayloadOffset : PayloadOffset;

	UParticleModuleRequired* RequiredModule = CurrentLODLevel->GetRequiredModule();
	Source.RequiredModule = RequiredModule;

	if (RequiredModule)
	{
		Source.Material = RequiredModule->GetMaterial();
		Source.SortMode = RequiredModule->GetSortMode();
		Source.TranslucencySortPriority = RequiredModule->GetTranslucencySortPriority();
	}

	Source.DataContainer.Allocate(ParticleStride, ActiveParticles);

	for (int32 i = 0; i < ActiveParticles; ++i)
	{
		uint16 SrcIndex = ParticleIndices[i];

		memcpy(
			Source.DataContainer.ParticleData + ParticleStride * i,
			ParticleData + ParticleStride * SrcIndex,
			ParticleStride
		);

		Source.DataContainer.ParticleIndices[i] = i;
	}

	return Data;
}

void FParticleBeamEmitterInstance::PreSpawn(FBaseParticle& Particle, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	FParticleEmitterInstance::PreSpawn(Particle, InitialLocation, InitialVelocity);

	if (!BeamTypeData)
		return;

	const int32 BeamPayloadOffset = GetModulePayloadOffset(EmitterTemplate, BeamTypeData);
	if (BeamPayloadOffset == INDEX_NONE)
		return;

	FBeamParticlePayload* Payload =
		reinterpret_cast<FBeamParticlePayload*>(
			reinterpret_cast<uint8*>(&Particle) + BeamPayloadOffset);

	if (!Payload)
		return;

	Payload->Source = BeamTypeData->GetSource();
	Payload->Target = BeamTypeData->GetTarget();
	Payload->Width = BeamTypeData->GetWidth();
	Payload->TextureTiling = BeamTypeData->GetTextureTiling();
}
