#include "ParticleEmitterInstance.h"
#include "Particles/Common/ParticleHelper.h"
#include "Core/EngineTypes.h"
#include "Math/Vector.h"

#include <utility>
#include <Core/Log.h>

void FParticleEmitterInstance::Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate)
{
	if(!InComponent || !InTemplate) return;

	OwnerComponent = InComponent;
	EmitterTemplate = InTemplate;
	CurrentLODLevel = InTemplate->GetLODLevel(CurrentLODLevelIndex);

	MaxActiveParticles = EmitterTemplate->GetMaxActiveParticles();

	PayloadOffset = sizeof(FBaseParticle);

	ParticleSize = std::max<size_t>(
		static_cast<size_t>(EmitterTemplate->GetParticleSize()),sizeof(FBaseParticle));

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
}

void FParticleEmitterInstance::Tick(float DeltaTime)
{
	if (!CurrentLODLevel)
		return;

	for (UParticleModule* Module : CurrentLODLevel->GetUpdateModules())
	{
		if (Module && Module->IsEnabled())
			Module->Update(this, DeltaTime);
	}


	ProcessEvents();
}

void FParticleEmitterInstance::SpawnParticles(int32 Count, float StartTime, float Increment, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	for (int32 i = 0; i < Count; ++i)
	{
		if (ActiveParticles >= MaxActiveParticles)
			break;

		const int32 ActiveIndex = ActiveParticles;

		DECLARE_PARTICLE_PTR(ActiveIndex);

		PreSpawn(Particle, InitialLocation, InitialVelocity);

		const float SpawnTime = StartTime + Increment * i;

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
	if (Index < ActiveParticles)
	{
		int32 KillIndex = ParticleIndices[Index];

		for (int32 i = Index; i < ActiveParticles - 1; i++)
		{
			ParticleIndices[i] = ParticleIndices[i + 1];
		}
		ParticleIndices[ActiveParticles - 1] = KillIndex;
		ActiveParticles--;
	}
}

void FParticleEmitterInstance::KillAllParticles()
{
	if (ActiveParticles > 0)
	{
		for (int32 i = ActiveParticles - 1; i >= 0; i--)
		{
			const int32	CurrentIndex = ParticleIndices[i];
			if (CurrentIndex < MaxActiveParticles)
			{
				const uint8* ParticleBase = ParticleData + CurrentIndex * ParticleStride;
				FBaseParticle& Particle = *((FBaseParticle*)ParticleBase);

				if (Particle.RelativeTime > 1.0f)
				{
					// Move it to the 'back' of the list
					ParticleIndices[i] = ParticleIndices[ActiveParticles - 1];
					ParticleIndices[ActiveParticles - 1] = CurrentIndex;
					ActiveParticles--;
				}
			}
		}
	}
}

void FParticleEmitterInstance::Reset()
{
	EmitterTime = 0;
	LastDeltaTime = 0;
	KillAllParticles();
}

FDynamicEmitterDataBase* FParticleEmitterInstance::CreateDynamicEmitterData(FDynamicEmitterReplayDataBase& Data)
{
	return nullptr;
}

void FParticleEmitterInstance::PreSpawn(FBaseParticle& Particle, const FVector& InitialLocation, const FVector& InitialVelocity)
{
	Particle.Location = InitialLocation;
	Particle.Velocity = InitialVelocity;
	Particle.BaseVelocity = InitialVelocity;
	Particle.RelativeTime = 0.0f;
	Particle.Lifetime = 1.0f;
	Particle.Color = FColor::White();
	Particle.Size = FVector(1.0f, 1.0f, 1.0f);

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

void FParticleEmitterInstance::ProcessEvents()
{
	
}
