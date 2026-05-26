#include "ParticleEmitterInstance.h"
#include "Particles/Common/ParticleHelper.h"
#include "Core/EngineTypes.h"
#include "Component/ParticleSystemComponent.h"
#include "Math/Vector.h"

#include <utility>
#include <Core/Log.h>

FParticleEmitterInstance::~FParticleEmitterInstance()
{
	delete[] ParticleData;
	ParticleData = nullptr;

	delete[] ParticleIndices;
	ParticleIndices = nullptr;

	delete[] InstanceData;
	InstanceData = nullptr;
	
	BurstFired.clear();
}

void FParticleEmitterInstance::Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate)
{
	if(!InComponent || !InTemplate) return;

	OwnerComponent = InComponent;
	EmitterTemplate = InTemplate;
	CurrentLODLevel = InTemplate->GetLODLevel(CurrentLODLevelIndex);

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

	// 각 모듈의 RandomSeedInfo를 바탕으로 모듈 전용 스트림 초기화
	for (UParticleModule* Module : CurrentLODLevel->GetModules())
	{
		if (Module)
			Module->InitializeStream();
	}
}

void FParticleEmitterInstance::Tick(float DeltaTime)
{
	if (!CurrentLODLevel)
		return;

	if(!bEnabled) return;

	// 이미터 시간 갱신
	EmitterTime += DeltaTime;

	//life타임 없는 애들 삭제 하는 로직
	KillExpiredParticles();

	ResetParticleParameters(DeltaTime);

	for (UParticleModule* Module : CurrentLODLevel->GetUpdateModules())
	{
		if (Module && Module->IsEnabled()) {
			Module->Update(this, DeltaTime);
		}
	}

	//시간되었으면 spawn하는 로직
	Tick_SpawnParticles(DeltaTime);

	if(!bFirstTime){
		//if (ActiveParticles > 0) UpdateBoundingBox(DeltaTime); 
		// 원래는 해당 로직 안에서 파티클 이동 진행
		BEGIN_UPDATE_LOOP
			Particle.Location += Particle.Velocity * DeltaTime;
		END_UPDATE_LOOP
	}

	ProcessEvents();
}

//StartTime : 현재 프레임에서 첫 번째 파티클이 태어날 때까지 프레임 단위 시간
void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity)
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
			if (Module)
			{
				Module->Spawn(this, Particle, SpawnTime);
			}
		}

		PostSpawn(Particle, SpawnTime);
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
}

void FParticleEmitterInstance::KillAllParticles()
{
	ActiveParticles = 0;
}

void FParticleEmitterInstance::Reset()
{
	EmitterTime = 0;
	LastDeltaTime = 0;
	LoopCount = 0;
	ParticleCounter = 0;
	ActiveParticles = 0;
	bEnabled = true;
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

void FParticleEmitterInstance::Tick_SpawnParticles(float DeltaTime)
{
	if (!CurrentLODLevel || !bEnabled)
		return;

	if (EmitterTime < 0.0f)
		return;

	//Burst는 일단 미구현
	float SpawnRate = 0.0f;
	int32 BurstCount = 0;

	for (UParticleModule* Module : CurrentLODLevel->GetSpawnModules())
	{
		if (!Module || !Module->IsEnabled())
			continue;

		if (Module->GetModuleType() == EParticleModuleType::PMT_Spawn)
		{
			UParticleModuleSpawn* SpawnModule = static_cast<UParticleModuleSpawn*>(Module);
			SpawnRate += std::max(0.0f, SpawnModule->GetSpawnRate());

			if (bFirstTime)
			{
				//BurstCount += SpawnModule->GetBurstCount();
			}
		}
	}

	const float NewLeftover = SpawnFraction + DeltaTime * SpawnRate;
	int32 SpawnCount = static_cast<int32>(floor(NewLeftover));
	float NewSpawnFraction = NewLeftover - SpawnCount;

	const int32 AvailableSlots = MaxActiveParticles - ActiveParticles;
	const int32 TotalSpawnCount = std::min(SpawnCount + BurstCount, AvailableSlots);

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
			FVector::ZeroVector
		);
	}

	bFirstTime = false;
	SpawnFraction = NewSpawnFraction;
}

FDynamicEmitterDataBase* FParticleEmitterInstance::CreateDynamicEmitterData()
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
	Source.Scale = FVector(1.0f, 1.0f, 1.0f);

	UParticleModuleRequired* RequiredModule = CurrentLODLevel->GetRequiredModule();
	Source.RequiredModule = RequiredModule;

	if (RequiredModule)
	{
		Source.Material = RequiredModule->GetMaterial();
		Source.SortMode = RequiredModule->GetSortMode();
	}

	Source.DataContainer.Allocate(ParticleStride, MaxActiveParticles);

	memcpy(
		Source.DataContainer.ParticleData,
		ParticleData,
		ParticleStride * MaxActiveParticles
	);

	memcpy(
		Source.DataContainer.ParticleIndices,
		ParticleIndices,
		sizeof(uint16) * ActiveParticles
	);

	return Data;
}

void FParticleEmitterInstance::PreSpawn(FBaseParticle& Particle, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	std::memset(&Particle, 0, ParticleStride);

	Particle.Location = InitialLocation;
	Particle.Velocity = InitialVelocity;
	Particle.BaseVelocity = InitialVelocity;
	
	//TODO: 모듈 생성시 아래 초기화 없앨것
	//현재 모듈이 없어서 초기화 불가능해서 일단 여기서 진행 
	Particle.RelativeTime = 0.0f;
	Particle.Lifetime = 1.0f;
	Particle.Color = FColor::White();
	Particle.BaseColor = Particle.Color;
	Particle.Size = FVector(1.0f, 1.0f, 1.0f);
	Particle.BaseSize = Particle.Size;
	Particle.RotationRate = 0.0f;
	Particle.BaseRotationRate = Particle.RotationRate;

	for (UParticleModule* Module : CurrentLODLevel->GetSpawnModules())
	{
		if (Module && Module->IsEnabled())
			Module->PreSpawn(this, Particle);
	}
}

void FParticleEmitterInstance::PostSpawn(FBaseParticle& Particle, float SpawnTime)
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
}

void FParticleEmitterInstance::KillExpiredParticles()
{
	BEGIN_UPDATE_LOOP
	if (Particle.RelativeTime >= 1.0f)
	{
		KillParticle(Index);
	}
	END_UPDATE_LOOP
}

void FParticleEmitterInstance::ProcessEvents()
{
	
}

FBaseParticle& FParticleEmitterInstance::GetParticle(int32 index)
{
	return *reinterpret_cast<FBaseParticle*>(ParticleData + ParticleStride * ParticleIndices[index]);
}
