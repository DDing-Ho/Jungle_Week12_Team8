/**
 * @file ParticleModules.cpp
 * @brief 모든 Particle Module 및 TypeData Serialize / Spawn / Update / CacheModuleValues 구현.
 */

#include "Particles/Assets/ParticleTypeData.h"
#include "Particles/Assets/ParticleAsset.h"
#include "Particles/Runtime/ParticleRuntimeTypes.h"
#include "Particles/Common/ParticleHelper.h"
#include "Particles/Common/ParticleDistributionTypes.h"
#include "Serialization/Archive.h"
#include "Materials/Material.h"
#include "Materials/MaterialManager.h"
#include "Mesh/StaticMesh.h"
#include "Mesh/MeshManager.h"
#include "Engine/Runtime/Engine.h"
#include "Object/FUObjectArray.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// UParticleModule (base)
// ─────────────────────────────────────────────────────────────────────────────

void UParticleModule::Serialize(FArchive& Ar)
{
    Ar << bEnabled;
    Ar << RandomSeedInfo.bUseSeed;
    Ar << RandomSeedInfo.Seed;
}

void UParticleModule::InitializeStream()
{
    if (RandomSeedInfo.bUseSeed)
    {
        // 에셋에 저장된 고정 Seed — 항상 동일한 패턴 재현
        ModuleStream.Initialize(RandomSeedInfo.Seed);
    }
    else
    {
        // 시간 기반 Seed — 실행마다 다른 결과
        const int32 TimeSeed = static_cast<int32>(
            std::chrono::steady_clock::now().time_since_epoch().count() & 0x7FFFFFFF);
        ModuleStream.Initialize(TimeSeed);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Core Modules
// ─────────────────────────────────────────────────────────────────────────────

void UParticleModuleRequired::SetMaterial(UMaterial* InMaterial)
{
	Material = InMaterial;
	if (Material)
	{
		MaterialSlot.Path = Material->GetAssetPathFileName();
	}
	else
	{
		MaterialSlot.Path = "None";
	}
}

void UParticleModuleRequired::SetMaterialPath(const FString& InMaterialPath)
{
	if (InMaterialPath.empty() || InMaterialPath == "None")
	{
		SetMaterial(nullptr);
		return;
	}

	SetMaterial(FMaterialManager::Get().GetOrCreateMaterial(InMaterialPath));
}

void UParticleModuleRequired::PostEditProperty(const char* PropertyName)
{
    UParticleModule::PostEditProperty(PropertyName);
    if (PropertyName && (strcmp(PropertyName, "Material") == 0 || strcmp(PropertyName, "MaterialSlot") == 0))
    {
        SetMaterialPath(MaterialSlot.Path);
    }
    else if (PropertyName && strcmp(PropertyName, "Emitter Type") == 0)
    {
        if (UParticleLODLevel* LODLevel = GetTypedOuter<UParticleLODLevel>())
        {
            LODLevel->SyncTypeDataModuleToRequired();
        }
    }

    SubImages_Horizontal = (std::max)(1, SubImages_Horizontal);
    SubImages_Vertical = (std::max)(1, SubImages_Vertical);
    RandomImageChanges = (std::max)(0, RandomImageChanges);
}

int32 UParticleModuleRequired::GetSubImagesHorizontal() const
{
    return (std::max)(1, SubImages_Horizontal);
}

int32 UParticleModuleRequired::GetSubImagesVertical() const
{
    return (std::max)(1, SubImages_Vertical);
}

int32 UParticleModuleRequired::GetSubImageCount() const
{
    return GetSubImagesHorizontal() * GetSubImagesVertical();
}

float UParticleModuleRequired::GetRandomImageTime() const
{
    if (RandomImageChanges <= 0)
    {
        return 1.0f;
    }
    return 0.99f / static_cast<float>(RandomImageChanges + 1);
}

void UParticleModuleRequired::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    int32 TypeInt = static_cast<int32>(EmitterType);
    Ar << TypeInt;
    if (Ar.IsLoading()) EmitterType = static_cast<EParticleEmitterType>(TypeInt);

    FString MatPath = MaterialSlot.Path;
    Ar << MatPath;
    if (Ar.IsLoading())
    {
        if (MatPath.empty() || MatPath == "None")
        {
            SetMaterial(nullptr);
        }
        else
        {
            SetMaterial(FMaterialManager::Get().GetOrCreateMaterial(MatPath));
        }
    }

    int32 SortInt = static_cast<int32>(SortMode);
    Ar << SortInt;
    if (Ar.IsLoading()) SortMode = static_cast<EParticleSortMode>(SortInt);

    Ar << TranslucencySortPriority;

    Ar << SubImages_Horizontal;
    Ar << SubImages_Vertical;
    int32 InterpInt = static_cast<int32>(InterpolationMethod);
    Ar << InterpInt;
    if (Ar.IsLoading()) InterpolationMethod = static_cast<EParticleSubUVInterpMethod>(InterpInt);
    Ar << RandomImageChanges;

    if (Ar.IsLoading())
    {
        SubImages_Horizontal = (std::max)(1, SubImages_Horizontal);
        SubImages_Vertical = (std::max)(1, SubImages_Vertical);
        RandomImageChanges = (std::max)(0, RandomImageChanges);
    }
}

void UParticleModuleSpawn::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << SpawnRate;
    Ar << BurstCount;
}

// ── Lifetime ─────────────────────────────────────────────────────────────────

namespace
{
    constexpr float   DefaultParticleLifetime    = 1.0f;
    const     FVector DefaultLocationMin         = FVector::ZeroVector;
    const     FVector DefaultLocationMax         = FVector::ZeroVector;
    const     FVector DefaultVelocityMin         = FVector(0.f, 0.f, 0.f);
    const     FVector DefaultVelocityMax         = FVector(0.f, 0.f, 10.f);

    void InitializeDefaultLifetimeDistribution(UDistributionFloat* Distribution)
    {
        if (!Distribution) return;
        Distribution->Type = EDistributionType::Constant;
        Distribution->Min  = DefaultParticleLifetime;
        Distribution->Max  = DefaultParticleLifetime;
    }

    void InitializeDefaultLocationDistribution(UDistributionVector* Distribution)
    {
        if (!Distribution) return;
        Distribution->Type = EDistributionType::Constant;
        Distribution->Min  = DefaultLocationMin;
        Distribution->Max  = DefaultLocationMax;
    }

    void InitializeDefaultVelocityDistribution(UDistributionVector* Distribution)
    {
        if (!Distribution) return;
        Distribution->Type = EDistributionType::Uniform;
        Distribution->Min  = DefaultVelocityMin;
        Distribution->Max  = DefaultVelocityMax;
    }
}

void UParticleModuleLifetime::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    if (!LifetimeDist)
    {
        LifetimeDist = GUObjectArray.CreateObject<UDistributionFloat>(this);
        if (Ar.IsSaving())
        {
            InitializeDefaultLifetimeDistribution(LifetimeDist);
        }
    }

    LifetimeDist->Serialize(Ar);

    if (Ar.IsLoading())
        RawLifetime = LifetimeDist->BuildRaw();
}

void UParticleModuleLifetime::CacheModuleValues()
{
    if (!LifetimeDist)
    {
        LifetimeDist = GUObjectArray.CreateObject<UDistributionFloat>(this);
        InitializeDefaultLifetimeDistribution(LifetimeDist);
    }

    if (LifetimeDist)
        RawLifetime = LifetimeDist->BuildRaw();
}

void UParticleModuleLifetime::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    if (!bEnabled) return;
    Particle.Lifetime     = RawLifetime.GetValue(0.f, &ModuleStream);
    Particle.RelativeTime = Particle.Lifetime > 0.0f ? SpawnTime / Particle.Lifetime : 0.0f;
}

// ── Location ─────────────────────────────────────────────────────────────────

void UParticleModuleLocation::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    if (!LocationDist)
    {
        LocationDist = GUObjectArray.CreateObject<UDistributionVector>(this);
        if (Ar.IsSaving())
            InitializeDefaultLocationDistribution(LocationDist);
    }

    LocationDist->Serialize(Ar);
    Ar << SphereRadius << CylinderRadius << CylinderHeight;

    if (Ar.IsLoading())
        RawLocation = LocationDist->BuildRaw();
}

void UParticleModuleLocation::CacheModuleValues()
{
    if (!LocationDist)
    {
        LocationDist = GUObjectArray.CreateObject<UDistributionVector>(this);
        InitializeDefaultLocationDistribution(LocationDist);
    }

    if (LocationDist)
        RawLocation = LocationDist->BuildRaw();
}

void UParticleModuleLocation::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    if (!bEnabled) return;
    Particle.Location += RawLocation.GetValue(0.f, &ModuleStream);
}

// ── Velocity ─────────────────────────────────────────────────────────────────

void UParticleModuleVelocity::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    if (!VelocityDist)
    {
        VelocityDist = GUObjectArray.CreateObject<UDistributionVector>(this);
        if (Ar.IsSaving())
            InitializeDefaultVelocityDistribution(VelocityDist);
    }

    VelocityDist->Serialize(Ar);

    if (Ar.IsLoading())
        RawVelocity = VelocityDist->BuildRaw();
}

void UParticleModuleVelocity::CacheModuleValues()
{
    if (!VelocityDist)
    {
        VelocityDist = GUObjectArray.CreateObject<UDistributionVector>(this);
        InitializeDefaultVelocityDistribution(VelocityDist);
    }

    if (VelocityDist)
        RawVelocity = VelocityDist->BuildRaw();
}

void UParticleModuleVelocity::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    if (!bEnabled) return;
    const FVector V       = RawVelocity.GetValue(0.f, &ModuleStream);
    Particle.BaseVelocity = V;
    Particle.Velocity     = V;
}

// ── Color ─────────────────────────────────────────────────────────────────────

void UParticleModuleColor::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    if (!ColorDist)
    {
        ColorDist = GUObjectArray.CreateObject<UDistributionLinearColor>(this);
        // UDistributionLinearColor 기본값(White)을 그대로 사용
    }

    ColorDist->Serialize(Ar);

    if (Ar.IsLoading())
        RawColor = ColorDist->BuildRaw();
}

void UParticleModuleColor::CacheModuleValues()
{
    if (!ColorDist)
    {
        ColorDist = GUObjectArray.CreateObject<UDistributionLinearColor>(this);
        // 기본값은 헤더 초기화(White)와 동일하므로 별도 설정 불필요
    }

    if (ColorDist)
        RawColor = ColorDist->BuildRaw();
}

void UParticleModuleColor::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    if (!bEnabled) return;
    const FLinearColor C = RawColor.GetValue(0.f, &ModuleStream);
    Particle.BaseColor = FColor(
        static_cast<uint32>(C.R * 255.f),
        static_cast<uint32>(C.G * 255.f),
        static_cast<uint32>(C.B * 255.f),
        static_cast<uint32>(C.A * 255.f)
    );
    Particle.Color = Particle.BaseColor;
}

void UParticleModuleColor::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (!bEnabled) return;

    uint8*  ParticleData    = Owner->ParticleData;
    uint16* ParticleIndices = Owner->ParticleIndices;
    int32   ParticleStride  = Owner->ParticleStride;
    int32   ActiveParticles = Owner->ActiveParticles;

    BEGIN_PARTICLE_UPDATE_LOOP
        const FLinearColor C = RawColor.GetValue(Particle.RelativeTime, nullptr);
        Particle.Color = FColor(
            static_cast<uint32>(C.R * 255.f),
            static_cast<uint32>(C.G * 255.f),
            static_cast<uint32>(C.B * 255.f),
            static_cast<uint32>(C.A * 255.f)
        );
    END_PARTICLE_UPDATE_LOOP
}

// ── Size ──────────────────────────────────────────────────────────────────────

void UParticleModuleSize::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    if (!SizeDist)
    {
        SizeDist = GUObjectArray.CreateObject<UDistributionVector>(this);
        if (Ar.IsSaving())
        {
            SizeDist->Min = FVector(1.f, 1.f, 1.f);
            SizeDist->Max = FVector(1.f, 1.f, 1.f);
        }
    }

    SizeDist->Serialize(Ar);

    if (Ar.IsLoading())
        RawSize = SizeDist->BuildRaw();
}

void UParticleModuleSize::CacheModuleValues()
{
    if (!SizeDist)
    {
        SizeDist = GUObjectArray.CreateObject<UDistributionVector>(this);
        SizeDist->Min = FVector(1.f, 1.f, 1.f);
        SizeDist->Max = FVector(1.f, 1.f, 1.f);
    }

    if (SizeDist)
        RawSize = SizeDist->BuildRaw();
}

void UParticleModuleSize::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    if (!bEnabled) return;
    const FVector S  = RawSize.GetValue(0.f, &ModuleStream);
    Particle.BaseSize = S;
    Particle.Size     = S;
}

void UParticleModuleSize::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (!bEnabled) return;

    uint8*  ParticleData    = Owner->ParticleData;
    uint16* ParticleIndices = Owner->ParticleIndices;
    int32   ParticleStride  = Owner->ParticleStride;
    int32   ActiveParticles = Owner->ActiveParticles;

    BEGIN_PARTICLE_UPDATE_LOOP
        Particle.Size = RawSize.GetValue(Particle.RelativeTime, nullptr);
    END_PARTICLE_UPDATE_LOOP
}

// ─────────────────────────────────────────────────────────────────────────────
// Motion Modules
// ─────────────────────────────────────────────────────────────────────────────

void UParticleModuleRotation::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    if (!RotationDist)
        RotationDist = GUObjectArray.CreateObject<UDistributionFloat>(this);

    RotationDist->Serialize(Ar);
    Ar << InitialMeshRotation;

    if (Ar.IsLoading())
        RawRotation = RotationDist->BuildRaw();
}

void UParticleModuleRotation::CacheModuleValues()
{
    if (!RotationDist)
        RotationDist = GUObjectArray.CreateObject<UDistributionFloat>(this);

    if (RotationDist)
        RawRotation = RotationDist->BuildRaw();
}

void UParticleModuleRotation::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    // 초기 회전각은 렌더러가 별도 필드로 관리하는 경우에만 적용
}

void UParticleModuleRotationRate::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    if (!RotationRateDist)
        RotationRateDist = GUObjectArray.CreateObject<UDistributionFloat>(this);

    RotationRateDist->Serialize(Ar);
    Ar << MeshRotationRate;

    if (Ar.IsLoading())
        RawRotationRate = RotationRateDist->BuildRaw();
}

void UParticleModuleRotationRate::CacheModuleValues()
{
    if (!RotationRateDist)
        RotationRateDist = GUObjectArray.CreateObject<UDistributionFloat>(this);

    if (RotationRateDist)
        RawRotationRate = RotationRateDist->BuildRaw();
}

void UParticleModuleRotationRate::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    if (!bEnabled) return;
    const float Rate          = RawRotationRate.GetValue(0.f, &ModuleStream);
    Particle.BaseRotationRate = Rate;
    Particle.RotationRate     = Rate;
}

void UParticleModuleRotationRate::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (!bEnabled) return;

    uint8*  ParticleData    = Owner->ParticleData;
    uint16* ParticleIndices = Owner->ParticleIndices;
    int32   ParticleStride  = Owner->ParticleStride;
    int32   ActiveParticles = Owner->ActiveParticles;

    BEGIN_PARTICLE_UPDATE_LOOP
        Particle.RotationRate = RawRotationRate.GetValue(Particle.RelativeTime, nullptr);
    END_PARTICLE_UPDATE_LOOP
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

// ─────────────────────────────────────────────────────────────────────────────
// Collision / Kill Modules
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Event Modules
// ─────────────────────────────────────────────────────────────────────────────

void UParticleModuleEventGenerator::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << GeneratedEvents;
}

void UParticleModuleEventReceiverBase::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);

    int32 EventTypeInt = static_cast<int32>(ListenEventType);
    Ar << EventTypeInt;
    if (Ar.IsLoading())
        ListenEventType = static_cast<EParticleEventType>(EventTypeInt);

    Ar << ListenEventName;
}

void UParticleModuleEventReceiverSpawn::Serialize(FArchive& Ar)
{
    UParticleModuleEventReceiverBase::Serialize(Ar);
    Ar << SpawnCount;
}

bool UParticleModuleEventReceiverBase::MatchesEvent(const FParticleEventData& Event) const
{
    if (Event.Type != ListenEventType)
        return false;

    // ListenEventName이 비어 있으면 해당 Type의 Event를 모두 받는 wildcard로 취급한다.
    if (ListenEventName.IsValid() && Event.EventName != ListenEventName)
        return false;

    return true;
}

void UParticleModuleEventReceiverSpawn::ProcessEvent(FParticleEmitterInstance& OwnerEmitter, const FParticleEventData& Event)
{
    if (!MatchesEvent(Event))
        return;

    const int32 Count = std::max(0, SpawnCount);
    if (Count <= 0)
        return;

    OwnerEmitter.SpawnParticles(Count, 0.0f, 0.0f, Event.Location, Event.Velocity);
}

void UParticleModuleEventReceiverKillAll::Serialize(FArchive& Ar)
{
    UParticleModuleEventReceiverBase::Serialize(Ar);
}

void UParticleModuleEventReceiverKillAll::ProcessEvent(FParticleEmitterInstance& OwnerEmitter, const FParticleEventData& Event)
{
    if (!MatchesEvent(Event))
        return;

    OwnerEmitter.KillAllParticles();
}

// ─────────────────────────────────────────────────────────────────────────────
// Render Expression Modules
// ─────────────────────────────────────────────────────────────────────────────

void UParticleModuleSubUVBase::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << bUseRealTime;
}

UParticleModuleRequired* UParticleModuleSubUVBase::GetRequiredModule(FParticleEmitterInstance* Owner) const
{
    return (Owner && Owner->CurrentLODLevel) ? Owner->CurrentLODLevel->GetRequiredModule() : nullptr;
}

EParticleSubUVInterpMethod UParticleModuleSubUVBase::GetInterpolationMethod(FParticleEmitterInstance* Owner) const
{
    UParticleModuleRequired* Required = GetRequiredModule(Owner);
    return Required ? Required->GetSubUVInterpolationMethod() : EParticleSubUVInterpMethod::PSUVIM_None;
}

int32 UParticleModuleSubUVBase::GetSubImageCount(FParticleEmitterInstance* Owner) const
{
    UParticleModuleRequired* Required = GetRequiredModule(Owner);
    return Required ? Required->GetSubImageCount() : 1;
}

int32 UParticleModuleSubUVBase::ClampFrameIndex(FParticleEmitterInstance* Owner, int32 InFrame) const
{
    const int32 FrameCount = (std::max)(1, GetSubImageCount(Owner));
    return (std::clamp)(InFrame, 0, FrameCount - 1);
}

int32 UParticleModuleSubUVBase::WrapFrameIndex(FParticleEmitterInstance* Owner, int32 InFrame) const
{
    const int32 FrameCount = (std::max)(1, GetSubImageCount(Owner));
    int32 Wrapped = InFrame % FrameCount;
    if (Wrapped < 0)
    {
        Wrapped += FrameCount;
    }
    return Wrapped;
}

float UParticleModuleSubUVBase::WrapFrameValue(FParticleEmitterInstance* Owner, float InFrame) const
{
    const int32 FrameCount = (std::max)(1, GetSubImageCount(Owner));
    float Wrapped = std::fmod(InFrame, static_cast<float>(FrameCount));
    if (Wrapped < 0.0f)
    {
        Wrapped += static_cast<float>(FrameCount);
    }
    return Wrapped;
}

float UParticleModuleSubUVBase::GetDeltaTime(FParticleEmitterInstance* Owner, float DeltaTime) const
{
    if (bUseRealTime && Owner)
    {
        return Owner->GetRealDeltaTime();
    }
    return DeltaTime;
}

void UParticleModuleSubUVBase::SetParticleSubUVFrames(FBaseParticle& Particle, float FrameA, float FrameB, float Lerp) const
{
    Particle.SubUVFrameA = FrameA;
    Particle.SubUVFrameB = FrameB;
    Particle.SubUVLerp = (std::clamp)(Lerp, 0.0f, 1.0f);
}

void UParticleModuleSubUVBase::SetParticleSequentialSubUV(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float ImageIndex, bool bBlend, bool bLoop) const
{
    const int32 FrameCount = (std::max)(1, GetSubImageCount(Owner));
    const float FrameValue = bLoop
        ? WrapFrameValue(Owner, ImageIndex)
        : (std::clamp)(ImageIndex, 0.0f, static_cast<float>(FrameCount - 1));

    const float BaseFrame = std::floor(FrameValue);
    const float Lerp = bBlend ? (FrameValue - BaseFrame) : 0.0f;
    const int32 FrameA = bLoop
        ? WrapFrameIndex(Owner, static_cast<int32>(BaseFrame))
        : ClampFrameIndex(Owner, static_cast<int32>(BaseFrame));
    const int32 FrameB = bBlend
        ? (bLoop ? WrapFrameIndex(Owner, FrameA + 1) : ClampFrameIndex(Owner, FrameA + 1))
        : FrameA;

    Particle.SubImageIndex = FrameValue;
    SetParticleSubUVFrames(Particle, static_cast<float>(FrameA), static_cast<float>(FrameB), Lerp);
}

float UParticleModuleSubUVBase::SelectRandomFrame(FParticleEmitterInstance* Owner) const
{
    const int32 FrameCount = (std::max)(1, GetSubImageCount(Owner));
    const int32 Frame = (std::min)(FrameCount - 1, static_cast<int32>(ModuleStream.GetFraction() * static_cast<float>(FrameCount)));
    return static_cast<float>((std::max)(0, Frame));
}

void UParticleModuleSubImageIndex::InitializeDefaultSubImageIndexDistribution()
{
    if (!SubImageIndexDist)
    {
        SubImageIndexDist = GUObjectArray.CreateObject<UDistributionFloat>(this);
        SubImageIndexDist->Type = EDistributionType::Constant;
        SubImageIndexDist->Min = 0.0f;
        SubImageIndexDist->Max = 0.0f;
    }
}

void UParticleModuleSubImageIndex::Serialize(FArchive& Ar)
{
    UParticleModuleSubUVBase::Serialize(Ar);
    InitializeDefaultSubImageIndexDistribution();
    SubImageIndexDist->Serialize(Ar);
    if (Ar.IsLoading())
    {
        RawSubImageIndex = SubImageIndexDist->BuildRaw();
    }
}

void UParticleModuleSubImageIndex::CacheModuleValues()
{
    InitializeDefaultSubImageIndexDistribution();
    RawSubImageIndex = SubImageIndexDist->BuildRaw();
}

void UParticleModuleSubImageIndex::InitializeRandomSubImage(FParticleEmitterInstance* Owner, FBaseParticle& Particle)
{
    const float Frame = SelectRandomFrame(Owner);
    Particle.SubUVRandomLastChangeTime = Particle.RelativeTime;
    Particle.SubUVRandomPreviousFrame = Frame;
    Particle.SubUVRandomCurrentFrame = Frame;
    Particle.SubImageIndex = Frame;
    SetParticleSubUVFrames(Particle, Frame, Frame, 0.0f);
}

void UParticleModuleSubImageIndex::ApplyRandomSubImage(FParticleEmitterInstance* Owner, FBaseParticle& Particle)
{
    UParticleModuleRequired* Required = GetRequiredModule(Owner);
    if (!Required)
    {
        SetParticleSubUVFrames(Particle, 0.0f, 0.0f, 0.0f);
        return;
    }

    const EParticleSubUVInterpMethod InterpMethod = Required->GetSubUVInterpolationMethod();
    const bool bBlend = InterpMethod == EParticleSubUVInterpMethod::PSUVIM_Random_Blend;
    const int32 ChangeCount = Required->GetRandomImageChanges();
    const float RandomImageTime = Required->GetRandomImageTime();

    if (ChangeCount > 0 && RandomImageTime > 0.0f
        && (Particle.RelativeTime - Particle.SubUVRandomLastChangeTime) >= RandomImageTime)
    {
        Particle.SubUVRandomPreviousFrame = Particle.SubUVRandomCurrentFrame;
        Particle.SubUVRandomCurrentFrame = SelectRandomFrame(Owner);
        Particle.SubUVRandomLastChangeTime = Particle.RelativeTime;
    }

    const float Alpha = bBlend && ChangeCount > 0 && RandomImageTime > 0.0f
        ? (std::clamp)((Particle.RelativeTime - Particle.SubUVRandomLastChangeTime) / RandomImageTime, 0.0f, 1.0f)
        : 0.0f;

    Particle.SubImageIndex = Particle.SubUVRandomCurrentFrame;
    SetParticleSubUVFrames(Particle,
        Particle.SubUVRandomPreviousFrame,
        Particle.SubUVRandomCurrentFrame,
        Alpha);
}

void UParticleModuleSubImageIndex::ApplySubImageIndex(FParticleEmitterInstance* Owner, FBaseParticle& Particle)
{
    const EParticleSubUVInterpMethod InterpMethod = GetInterpolationMethod(Owner);
    if (InterpMethod == EParticleSubUVInterpMethod::PSUVIM_None)
    {
        return;
    }

    if (InterpMethod == EParticleSubUVInterpMethod::PSUVIM_Random
        || InterpMethod == EParticleSubUVInterpMethod::PSUVIM_Random_Blend)
    {
        ApplyRandomSubImage(Owner, Particle);
        return;
    }

    const bool bBlend = InterpMethod == EParticleSubUVInterpMethod::PSUVIM_Linear_Blend;
    const float T = (std::clamp)(Particle.RelativeTime, 0.0f, 1.0f);
    const float ImageIndex = RawSubImageIndex.GetValue(T, nullptr);
    SetParticleSequentialSubUV(Owner, Particle, ImageIndex, bBlend, false);
}

void UParticleModuleSubImageIndex::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    if (!bEnabled) return;

    const EParticleSubUVInterpMethod InterpMethod = GetInterpolationMethod(Owner);
    if (InterpMethod == EParticleSubUVInterpMethod::PSUVIM_Random
        || InterpMethod == EParticleSubUVInterpMethod::PSUVIM_Random_Blend)
    {
        InitializeRandomSubImage(Owner, Particle);
        return;
    }

    ApplySubImageIndex(Owner, Particle);
}

void UParticleModuleSubImageIndex::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (!bEnabled) return;

    uint8*  ParticleData    = Owner->ParticleData;
    uint16* ParticleIndices = Owner->ParticleIndices;
    int32   ParticleStride  = Owner->ParticleStride;
    int32   ActiveParticles = Owner->ActiveParticles;

    BEGIN_UPDATE_LOOP
        ApplySubImageIndex(Owner, Particle);
    END_UPDATE_LOOP
}

void UParticleModuleSubUVMovie::InitializeDefaultFrameRateDistribution()
{
    if (!FrameRateDist)
    {
        FrameRateDist = GUObjectArray.CreateObject<UDistributionFloat>(this);
        FrameRateDist->Type = EDistributionType::Constant;
        FrameRateDist->Min = 30.0f;
        FrameRateDist->Max = 30.0f;
    }
}

void UParticleModuleSubUVMovie::Serialize(FArchive& Ar)
{
    UParticleModuleSubUVBase::Serialize(Ar);
    InitializeDefaultFrameRateDistribution();
    FrameRateDist->Serialize(Ar);
    Ar << StartingFrame;
    Ar << bUseEmitterTime;
    if (Ar.IsLoading())
    {
        RawFrameRate = FrameRateDist->BuildRaw();
        StartingFrame = (std::max)(0, StartingFrame);
    }
}

void UParticleModuleSubUVMovie::CacheModuleValues()
{
    InitializeDefaultFrameRateDistribution();
    RawFrameRate = FrameRateDist->BuildRaw();
    StartingFrame = (std::max)(0, StartingFrame);
}

int32 UParticleModuleSubUVMovie::ResolveStartingFrame(FParticleEmitterInstance* Owner)
{
    const int32 FrameCount = (std::max)(1, GetSubImageCount(Owner));
    if (StartingFrame == 0)
    {
        return static_cast<int32>(SelectRandomFrame(Owner));
    }
    return (std::clamp)(StartingFrame - 1, 0, FrameCount - 1);
}

void UParticleModuleSubUVMovie::ApplyMovieFrame(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float DeltaTime)
{
    const EParticleSubUVInterpMethod InterpMethod = GetInterpolationMethod(Owner);
    if (InterpMethod == EParticleSubUVInterpMethod::PSUVIM_None)
    {
        return;
    }

    Particle.SubUVMovieTime += GetDeltaTime(Owner, DeltaTime);

    const float EvalT = bUseEmitterTime && Owner
        ? Owner->GetEmitterTime()
        : (std::clamp)(Particle.RelativeTime, 0.0f, 1.0f);
    const float FrameRate = RawFrameRate.GetValue(EvalT, nullptr);
    const float ImageIndex = Particle.SubUVMovieStartFrame + Particle.SubUVMovieTime * FrameRate;
    const bool bBlend = InterpMethod == EParticleSubUVInterpMethod::PSUVIM_Linear_Blend;
    SetParticleSequentialSubUV(Owner, Particle, ImageIndex, bBlend, true);
}

void UParticleModuleSubUVMovie::Spawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle, float SpawnTime)
{
    if (!bEnabled) return;

    const EParticleSubUVInterpMethod InterpMethod = GetInterpolationMethod(Owner);
    if (InterpMethod != EParticleSubUVInterpMethod::PSUVIM_Linear
        && InterpMethod != EParticleSubUVInterpMethod::PSUVIM_Linear_Blend)
    {
        return;
    }

    Particle.SubUVMovieStartFrame = static_cast<float>(ResolveStartingFrame(Owner));
    Particle.SubUVMovieTime = (std::max)(0.0f, SpawnTime);
    ApplyMovieFrame(Owner, Particle, 0.0f);
}

void UParticleModuleSubUVMovie::Update(FParticleEmitterInstance* Owner, float DeltaTime)
{
    if (!bEnabled) return;

    const EParticleSubUVInterpMethod InterpMethod = GetInterpolationMethod(Owner);
    if (InterpMethod != EParticleSubUVInterpMethod::PSUVIM_Linear
        && InterpMethod != EParticleSubUVInterpMethod::PSUVIM_Linear_Blend)
    {
        return;
    }

    uint8*  ParticleData    = Owner->ParticleData;
    uint16* ParticleIndices = Owner->ParticleIndices;
    int32   ParticleStride  = Owner->ParticleStride;
    int32   ActiveParticles = Owner->ActiveParticles;

    BEGIN_UPDATE_LOOP
        ApplyMovieFrame(Owner, Particle, DeltaTime);
    END_UPDATE_LOOP
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

// ─────────────────────────────────────────────────────────────────────────────
// TypeData Modules
// ─────────────────────────────────────────────────────────────────────────────

void UParticleModuleTypeDataSprite::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    // Sprite는 추가 설정값 없음
}

void UParticleModuleTypeDataMesh::PostEditProperty(const char* PropertyName)
{
    UParticleModuleTypeDataBase::PostEditProperty(PropertyName);

    if (PropertyName && strcmp(PropertyName, "Static Mesh") == 0)
    {
        if (MeshAsset.IsNull())
        {
            SetMesh(nullptr);
        }
        else
        {
            ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
            SetMesh(FMeshManager::LoadStaticMesh(MeshAsset.GetPath().ToString(), Device));
        }
    }
}

void UParticleModuleTypeDataMesh::SetMesh(UStaticMesh* InMesh)
{
	Mesh = InMesh;
	MeshAsset = InMesh;
	MeshPath = Mesh ? Mesh->GetAssetPathFileName() : FString("None");
}

// MeshManager가 초기화 되어있다고 가정
void UParticleModuleTypeDataMesh::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    if (Ar.IsSaving())
        MeshPath = Mesh ? Mesh->GetAssetPathFileName() : MeshAsset.GetPath().ToString();
    
	Ar << MeshPath;
    
	if (Ar.IsLoading())
    {
        MeshAsset.SetPath(MeshPath);
        Mesh = nullptr;

		Ar << Mesh;
    }
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
