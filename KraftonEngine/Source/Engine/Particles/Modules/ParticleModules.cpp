/**
 * @file ParticleModules.cpp
 * @brief 모든 Particle Module 및 TypeData Serialize 구현.
 */

#include "Particles/Assets/ParticleTypeData.h"
#include "Serialization/Archive.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/MeshManager.h"

// ─────────────────────────────────────────
// UParticleModule (base)
// ─────────────────────────────────────────

void UParticleModule::Serialize(FArchive& Ar)
{
    Ar << bEnabled;
}

// ─────────────────────────────────────────
// Core Modules
// ─────────────────────────────────────────

void UParticleModuleRequired::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    int32 TypeInt = static_cast<int32>(EmitterType);
    Ar << TypeInt;
    if (Ar.IsLoading()) EmitterType = static_cast<EParticleEmitterType>(TypeInt);

    FString MatPath = (Ar.IsSaving() && Material) ? Material->GetAssetPathFileName() : FString();
    Ar << MatPath;
    if (Ar.IsLoading() && !MatPath.empty())
        Material = FMaterialManager::Get().GetOrCreateMaterial(MatPath);

    int32 SortInt = static_cast<int32>(SortMode);
    Ar << SortInt;
    if (Ar.IsLoading()) SortMode = static_cast<EParticleSortMode>(SortInt);

    Ar << TranslucencySortPriority;
}

void UParticleModuleSpawn::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << SpawnRate;
    Ar << BurstCount;
}

void UParticleModuleLifetime::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << Lifetime;
}

void UParticleModuleLocation::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << InitialLocation;
    Ar << SphereRadius;
    Ar << CylinderRadius;
    Ar << CylinderHeight;
    Ar << RandomSeedInfo;
}

void UParticleModuleVelocity::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << InitialVelocity;
    Ar << RandomSeedInfo;
}

void UParticleModuleColor::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << InitialColor;
    Ar << FinalColor;
    Ar << bUseColorOverLife;
}

void UParticleModuleSize::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << InitialSize;
    Ar << FinalSize;
    Ar << bUseSizeByLife;
    Ar << RandomSeedInfo;
}

// ─────────────────────────────────────────
// Motion Modules
// ─────────────────────────────────────────

void UParticleModuleRotation::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << InitialRotation;
    Ar << InitialMeshRotation;
    Ar << RandomSeedInfo;
}

void UParticleModuleRotationRate::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << InitialRotationRate;
    Ar << RotationRateOverLife;
    Ar << MeshRotationRate;
    Ar << RandomSeedInfo;
}

void UParticleModuleAcceleration::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << Acceleration;
    Ar << ConstAcceleration;
    Ar << Drag;
    Ar << bUseAccelerationOverLife;
}

void UParticleModuleAttractor::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << TargetLocation;
    Ar << Strength;
    Ar << Radius;
}

void UParticleModuleOrbit::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << Offset;
    Ar << RotationRate;
}

// ─────────────────────────────────────────
// Collision / Kill Modules
// ─────────────────────────────────────────

void UParticleModuleCollision::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << bEnableCollision;
    Ar << bKillOnCollision;
    Ar << Bounce;
    Ar << Friction;
}

void UParticleModuleKill::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << bUseKillBox;
    Ar << bUseKillHeight;
    Ar << KillBox;
    Ar << KillHeight;
}

// ─────────────────────────────────────────
// Event Modules
// ─────────────────────────────────────────

void UParticleModuleEventGenerator::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << GeneratedEvents;
}

void UParticleModuleEventReceiverSpawn::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    int32 EventTypeInt = static_cast<int32>(ListenEventType);
    Ar << EventTypeInt;
    if (Ar.IsLoading()) ListenEventType = static_cast<EParticleEventType>(EventTypeInt);
    Ar << SpawnCount;
}

void UParticleModuleEventReceiverKillAll::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    int32 EventTypeInt = static_cast<int32>(ListenEventType);
    Ar << EventTypeInt;
    if (Ar.IsLoading()) ListenEventType = static_cast<EParticleEventType>(EventTypeInt);
}

// ─────────────────────────────────────────
// Render Expression Modules
// ─────────────────────────────────────────

void UParticleModuleSubUV::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << HorizontalCount;
    Ar << VerticalCount;
    Ar << StartFrame;
    Ar << EndFrame;
    Ar << bUseSubImageIndex;
}

void UParticleModuleLight::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << LightColor;
    Ar << Intensity;
    Ar << Radius;
}

void UParticleModuleVectorField::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    // VectorFieldAsset은 이 엔진에서 지원하지 않음 - 경로 placeholder만 저장
    FString VectorFieldPath;
    Ar << VectorFieldPath;
    Ar << Intensity;
}

void UParticleModuleCamera::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << CameraOffset;
}

void UParticleModuleParameter::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << ParameterName;
    Ar << ParameterValue;
}

// ─────────────────────────────────────────
// TypeData Modules
// ─────────────────────────────────────────

void UParticleModuleTypeDataSprite::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    // Sprite는 추가 설정값 없음
}

// MeshManager가 초기화 되어있다고 가정 
void UParticleModuleTypeDataMesh::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    if (Ar.IsSaving())
        MeshPath = Mesh ? Mesh->GetAssetPathFileName() : FString();
    Ar << MeshPath;
    if (Ar.IsLoading() && !MeshPath.empty())
        Mesh = FMeshManager::FindStaticMesh(MeshPath);
}

void UParticleModuleTypeDataBeam::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << Source;
    Ar << Target;
    Ar << Width;
    Ar << TextureTiling;
}

void UParticleModuleTypeDataRibbon::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << Width;
    Ar << Lifetime;
    Ar << TextureTiling;
}
