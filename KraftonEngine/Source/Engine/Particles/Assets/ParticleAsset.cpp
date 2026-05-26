/**
 * @file ParticleAsset.cpp
 * @brief UParticleSystem / UParticleEmitter / UParticleLODLevel
 *        의 Cache, Serialize, Save, Load 구현.
 */

#include "Particles/Assets/ParticleAsset.h"
#include "Asset/AssetPackage.h"
#include "Object/FUObjectArray.h"
#include "Serialization/WindowsArchive.h"
#include "Particles/Runtime/ParticleRuntimeTypes.h"

// ─────────────────────────────────────────
// 파일 내부 헬퍼: 다형 Module 생성
// ─────────────────────────────────────────

static UParticleModule* CreateModuleByClass(EParticleModuleClass ClassTag, UObject* Outer)
{
    switch (ClassTag)
    {
    case EParticleModuleClass::Required:             return GUObjectArray.CreateObject<UParticleModuleRequired>(Outer);
    case EParticleModuleClass::Spawn:                return GUObjectArray.CreateObject<UParticleModuleSpawn>(Outer);
    case EParticleModuleClass::Lifetime:             return GUObjectArray.CreateObject<UParticleModuleLifetime>(Outer);
    case EParticleModuleClass::Location:             return GUObjectArray.CreateObject<UParticleModuleLocation>(Outer);
    case EParticleModuleClass::Velocity:             return GUObjectArray.CreateObject<UParticleModuleVelocity>(Outer);
    case EParticleModuleClass::Color:                return GUObjectArray.CreateObject<UParticleModuleColor>(Outer);
    case EParticleModuleClass::Size:                 return GUObjectArray.CreateObject<UParticleModuleSize>(Outer);
    case EParticleModuleClass::Rotation:             return GUObjectArray.CreateObject<UParticleModuleRotation>(Outer);
    case EParticleModuleClass::RotationRate:         return GUObjectArray.CreateObject<UParticleModuleRotationRate>(Outer);
    case EParticleModuleClass::Acceleration:         return GUObjectArray.CreateObject<UParticleModuleAcceleration>(Outer);
    case EParticleModuleClass::Attractor:            return GUObjectArray.CreateObject<UParticleModuleAttractor>(Outer);
    case EParticleModuleClass::Orbit:                return GUObjectArray.CreateObject<UParticleModuleOrbit>(Outer);
    case EParticleModuleClass::Collision:            return GUObjectArray.CreateObject<UParticleModuleCollision>(Outer);
    case EParticleModuleClass::Kill:                 return GUObjectArray.CreateObject<UParticleModuleKill>(Outer);
    case EParticleModuleClass::EventGenerator:       return GUObjectArray.CreateObject<UParticleModuleEventGenerator>(Outer);
    case EParticleModuleClass::EventReceiverSpawn:   return GUObjectArray.CreateObject<UParticleModuleEventReceiverSpawn>(Outer);
    case EParticleModuleClass::EventReceiverKillAll: return GUObjectArray.CreateObject<UParticleModuleEventReceiverKillAll>(Outer);
    case EParticleModuleClass::SubUV:                return GUObjectArray.CreateObject<UParticleModuleSubUV>(Outer);
    case EParticleModuleClass::Light:                return GUObjectArray.CreateObject<UParticleModuleLight>(Outer);
    case EParticleModuleClass::VectorField:          return GUObjectArray.CreateObject<UParticleModuleVectorField>(Outer);
    case EParticleModuleClass::Camera:               return GUObjectArray.CreateObject<UParticleModuleCamera>(Outer);
    case EParticleModuleClass::Parameter:            return GUObjectArray.CreateObject<UParticleModuleParameter>(Outer);
    case EParticleModuleClass::TypeDataSprite:       return GUObjectArray.CreateObject<UParticleModuleTypeDataSprite>(Outer);
    case EParticleModuleClass::TypeDataMesh:         return GUObjectArray.CreateObject<UParticleModuleTypeDataMesh>(Outer);
    case EParticleModuleClass::TypeDataBeam:         return GUObjectArray.CreateObject<UParticleModuleTypeDataBeam>(Outer);
    case EParticleModuleClass::TypeDataRibbon:       return GUObjectArray.CreateObject<UParticleModuleTypeDataRibbon>(Outer);
    default: return nullptr;
    }
}

// UParticleModule* 하나를 Archive에 저장/복원
static void SerializeModulePtr(FArchive& Ar, UParticleModule*& Module, UObject* Outer)
{
    if (Ar.IsSaving())
    {
        uint8 Tag = Module
            ? static_cast<uint8>(Module->GetModuleClass())
            : static_cast<uint8>(EParticleModuleClass::Unknown);
        Ar << Tag;
        if (Module)
            Module->Serialize(Ar);
    }
    else
    {
        uint8 Tag = 0;
        Ar << Tag;
        EParticleModuleClass Class = static_cast<EParticleModuleClass>(Tag);
        Module = CreateModuleByClass(Class, Outer);
        if (Module)
            Module->Serialize(Ar);
    }
}

static bool IsTypeDataClass(EParticleModuleClass Class)
{
    return Class == EParticleModuleClass::TypeDataSprite
        || Class == EParticleModuleClass::TypeDataMesh
        || Class == EParticleModuleClass::TypeDataBeam
        || Class == EParticleModuleClass::TypeDataRibbon;
}

// UParticleModuleTypeDataBase* 하나를 Archive에 저장/복원
static void SerializeTypeDataPtr(FArchive& Ar, UParticleModuleTypeDataBase*& TypeData, UObject* Outer)
{
    if (Ar.IsSaving())
    {
        uint8 Tag = TypeData
            ? static_cast<uint8>(TypeData->GetModuleClass())
            : static_cast<uint8>(EParticleModuleClass::Unknown);
        Ar << Tag;
        if (TypeData)
            TypeData->Serialize(Ar);
    }
    else
    {
        uint8 Tag = 0;
        Ar << Tag;
        EParticleModuleClass Class = static_cast<EParticleModuleClass>(Tag);
        if (IsTypeDataClass(Class))
        {
            // CreateModuleByClass가 TypeData 태그에 대해 TypeDataBase* 파생 클래스를 반환함이 보장됨
            UParticleModule* Module = CreateModuleByClass(Class, Outer);
            TypeData = static_cast<UParticleModuleTypeDataBase*>(Module);
            if (TypeData)
                TypeData->Serialize(Ar);
        }
        else
        {
            TypeData = nullptr;
        }
    }
}

// ─────────────────────────────────────────
// UParticleLODLevel
// ─────────────────────────────────────────

void UParticleLODLevel::AddModule(UParticleModule* InModule)
{
    if (InModule)
        Modules.push_back(InModule);
}

void UParticleLODLevel::CacheModules()
{
    SpawnModules.clear();
    UpdateModules.clear();

    for (UParticleModule* M : Modules)
    {
        if (!M || !M->IsEnabled()) continue;

        M->CacheModuleValues();

        EParticleModuleUpdatePhase Phase = M->GetUpdatePhase();
        if (Phase == EParticleModuleUpdatePhase::PMUP_Spawn ||
            Phase == EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate)
        {
            SpawnModules.push_back(M);
        }
        if (Phase == EParticleModuleUpdatePhase::PMUP_Update ||
            Phase == EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate)
        {
            UpdateModules.push_back(M);
        }
    }
}

void UParticleLODLevel::Serialize(FArchive& Ar)
{
    Ar << Level;
    Ar << bEnabled;

    // RequiredModule
    bool bHasRequired = (RequiredModule != nullptr);
    Ar << bHasRequired;
    if (bHasRequired)
    {
        if (Ar.IsLoading())
            RequiredModule = GUObjectArray.CreateObject<UParticleModuleRequired>(this);
        RequiredModule->Serialize(Ar);
    }

    // TypeDataModule (nullable)
    SerializeTypeDataPtr(Ar, TypeDataModule, this);

    // 일반 Module 배열
    uint32 ModuleCount = Ar.IsSaving() ? static_cast<uint32>(Modules.size()) : 0u;
    Ar << ModuleCount;
    if (Ar.IsLoading())
        Modules.assign(ModuleCount, nullptr);
    for (uint32 i = 0; i < ModuleCount; ++i)
        SerializeModulePtr(Ar, Modules[i], this);

    if (Ar.IsLoading())
        CacheModules();
}

// ─────────────────────────────────────────
// UParticleEmitter
// ─────────────────────────────────────────

void UParticleEmitter::CacheEmitterModuleInfo()
{
    // FBaseParticle 크기를 기본값으로 설정
    // 추후 Module별 payload가 추가되면 여기서 누적
    ParticleSize = sizeof(FBaseParticle);

	if (LODLevels.size() == 0)
		return;
	UParticleLODLevel* HightestLODLevel = LODLevels[0];

	UParticleModuleTypeDataBase* HighTypeData = HightestLODLevel->GetTypeDataModule();
	if (HighTypeData)
	{
		// TODO : Mesh, Beam 처리	
	}

	auto& Modules = HightestLODLevel->GetModules();
	for (int32 ModuleIndex = 0; ModuleIndex < Modules.size(); ModuleIndex++)
	{
		UParticleModule* ParticleModule = Modules[ModuleIndex];
		
		if (ParticleModule->IsA(UParticleModuleTypeDataBase::StaticClass()) == false)
		{
			int32 ReqBytes = ParticleModule->RequiredBytes(HighTypeData);
			if (ReqBytes)
			{
				ParticleSize += ReqBytes;
			}
		}
	}
}

void UParticleEmitter::Serialize(FArchive& Ar)
{
    Ar << EmitterName;

    int32 RenderModeInt = static_cast<int32>(EmitterRenderMode);
    Ar << RenderModeInt;
    if (Ar.IsLoading()) EmitterRenderMode = static_cast<EParticleEmitterRenderMode>(RenderModeInt);

    Ar << EmitterEditorColor;
    Ar << InitialAllocationCount;
    Ar << MediumDetailSpawnRateScale;
    Ar << bCollapsed;
    Ar << MaxActiveParticles;

    uint32 LODCount = Ar.IsSaving() ? static_cast<uint32>(LODLevels.size()) : 0u;
    Ar << LODCount;
    if (Ar.IsLoading())
        LODLevels.assign(LODCount, nullptr);
    for (uint32 i = 0; i < LODCount; ++i)
    {
        if (Ar.IsLoading())
            LODLevels[i] = GUObjectArray.CreateObject<UParticleLODLevel>(this);
        LODLevels[i]->Serialize(Ar);
    }

    if (Ar.IsLoading())
        CacheEmitterModuleInfo();
}

// ─────────────────────────────────────────
// UParticleSystem
// ─────────────────────────────────────────

void UParticleSystem::CacheSystemModuleInfo()
{
    for (UParticleEmitter* Emitter : Emitters)
    {
        if (Emitter)
            Emitter->CacheEmitterModuleInfo();
    }
}

void UParticleSystem::Serialize(FArchive& Ar)
{
    Ar << LODDistances;

    uint32 EmitterCount = Ar.IsSaving() ? static_cast<uint32>(Emitters.size()) : 0u;
    Ar << EmitterCount;
    if (Ar.IsLoading())
        Emitters.assign(EmitterCount, nullptr);
    for (uint32 i = 0; i < EmitterCount; ++i)
    {
        if (Ar.IsLoading())
            Emitters[i] = GUObjectArray.CreateObject<UParticleEmitter>(this);
        Emitters[i]->Serialize(Ar);
    }

    if (Ar.IsLoading())
        CacheSystemModuleInfo();
}
