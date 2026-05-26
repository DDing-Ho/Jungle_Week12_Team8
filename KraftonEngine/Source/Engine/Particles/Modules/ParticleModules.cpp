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
#include "Component/ParticleSystemComponent.h"
#include "Core/CollisionTypes.h"
#include "Core/Notification.h"
#include "GameFramework/World.h"
#include <chrono>
#include <cstring>

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

void UParticleModuleColor::Update(FParticleEmitterInstance* Owner, float DeltaTime, TArray<FParticleEventData>* /*OutEventQueue*/)
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

void UParticleModuleSize::Update(FParticleEmitterInstance* Owner, float DeltaTime, TArray<FParticleEventData>* /*OutEventQueue*/)
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

void UParticleModuleRotationRate::Update(FParticleEmitterInstance* Owner, float DeltaTime, TArray<FParticleEventData>* /*OutEventQueue*/)
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
// Collision Modules
// ─────────────────────────────────────────────────────────────────────────────

void UParticleModuleCollision::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << bEnableCollision;
    Ar << Bounce;
    Ar << Friction;
    Ar << DampingFactor;
    Ar << CollisionRadius;
    Ar << MaxCollisions;
    Ar << MaxCollisionDistance;
    Ar << bApplyPhysics;

    int32 CompletionInt = static_cast<int32>(CompletionOption);
    Ar << CompletionInt;
    if (Ar.IsLoading())
        CompletionOption = static_cast<EParticleCollisionCompletionOption>(CompletionInt);

    int32 ChannelInt = static_cast<int32>(TraceChannel);
    Ar << ChannelInt;
    if (Ar.IsLoading())
        TraceChannel = static_cast<ECollisionChannel>(ChannelInt);

    int32 QueryModeInt = static_cast<int32>(CollisionQueryMode);
    Ar << QueryModeInt;
    if (Ar.IsLoading())
        CollisionQueryMode = static_cast<EParticleCollisionQueryMode>(QueryModeInt);
}

void UParticleModuleCollision::PostEditProperty(const char* PropertyName)
{
    UParticleModule::PostEditProperty(PropertyName);

    // 1. CollisionRadius는 0 이하 불가 — 최솟값으로 클램프
    if (CollisionRadius < 0.01f)
    {
        CollisionRadius = 0.01f;
        FNotificationManager::Get().AddNotification(
            "Collision Radius는 0.01 이상이어야 합니다. 최솟값으로 보정했습니다.",
            ENotificationType::Error, 4.0f);
    }

    // 2. Raycast 모드에서 CollisionRadius 설정 시 경고
    //    Raycast는 선분 검사이므로 radius는 충돌 감지 형상에 영향을 주지 않습니다.
    //    (ray 길이 보정과 반사 위치 보정에만 부분적으로 사용됩니다.)
    if (CollisionQueryMode == EParticleCollisionQueryMode::Raycast)
    {
        FNotificationManager::Get().AddNotification(
            "Collision Query Mode가 Raycast입니다.\n"
            "Raycast는 선분 검사이므로 Collision Radius가 충돌 형상에 적용되지 않습니다.\n"
            "반지름 기반 충돌이 필요하면 ExpandedAABBSweep 또는 SphereSweep을 사용하세요.",
            ENotificationType::Info, 5.0f);
    }
}

void UParticleModuleCollision::PreSpawn(FParticleEmitterInstance* Owner, FBaseParticle& Particle)
{
    // per-particle 페이로드를 0으로 초기화
    FParticleCollisionPayload* Payload =
        reinterpret_cast<FParticleCollisionPayload*>(reinterpret_cast<uint8*>(&Particle) + Owner->PayloadOffset);
    Payload->CollisionCount = 0;
    Payload->bStopCollision = 0;
}

void UParticleModuleCollision::Update(
    FParticleEmitterInstance* Owner,
    float DeltaTime,
    TArray<FParticleEventData>* OutEventQueue)
{
    // ------------------------------------------------------------
    // 1. Collision Module 활성화 여부 검사
    // ------------------------------------------------------------
    if (!bEnabled || !bEnableCollision)
        return;

    // ------------------------------------------------------------
    // 2. 파티클 시스템이 속한 Component / World / PhysicsScene 획득
    // ------------------------------------------------------------
    UParticleSystemComponent* Comp = Owner->OwnerComponent;
    if (!Comp)
        return;

    UWorld* World = Comp->GetWorld();
    if (!World)
        return;

    IPhysicsScene* PhysScene = World->GetPhysicsScene();
    if (!PhysScene)
        return;

    // ------------------------------------------------------------
    // 3. 충돌 검사에 필요한 공통 정보 캐싱
    // ------------------------------------------------------------
    const AActor* OwnerActor = Comp->GetOwner();
    const FVector EmitterPos = Comp->GetWorldLocation();
    const bool bLimitDist = MaxCollisionDistance > 0.0f;

    // ------------------------------------------------------------
    // 4. 파티클 데이터 배열 접근 정보
    // ------------------------------------------------------------
    uint8* ParticleData = Owner->ParticleData;
    uint16* ParticleIndices = Owner->ParticleIndices;
    int32 ParticleStride = Owner->ParticleStride;
    int32 ActiveParticles = Owner->ActiveParticles;

    // ------------------------------------------------------------
    // 5. Active Particle 순회
    // ------------------------------------------------------------
    // KillParticle()로 active list가 바뀔 수 있으므로 뒤에서부터 순회한다.
    for (int32 Index = ActiveParticles - 1; Index >= 0; --Index)
    {
        // --------------------------------------------------------
        // 5-1. Active Particle index → 실제 ParticleData index 변환
        // --------------------------------------------------------
        const int32 RealIndex = ParticleIndices[Index];
        uint8* ParticleBase = ParticleData + RealIndex * ParticleStride;

        FBaseParticle& Particle =
            *reinterpret_cast<FBaseParticle*>(ParticleBase);

        // --------------------------------------------------------
        // 5-2. Collision Module 전용 per-particle payload 접근
        // --------------------------------------------------------
        FParticleCollisionPayload* Payload =
            reinterpret_cast<FParticleCollisionPayload*>(ParticleBase + Owner->PayloadOffset);

        // 이미 충돌 검사가 중단된 파티클은 skip
        if (Payload->bStopCollision)
            continue;

        // --------------------------------------------------------
        // 6. MaxCollisionDistance 제한 검사
        // --------------------------------------------------------
        if (bLimitDist)
        {
            const FVector Diff = Particle.Location - EmitterPos;
            const float DistSq = Diff.Dot(Diff);
            const float MaxDistSq = MaxCollisionDistance * MaxCollisionDistance;

            if (DistSq > MaxDistSq)
                continue;
        }

        // --------------------------------------------------------
        // 7. 이번 프레임 이동 방향 / 이동 거리 계산
        // --------------------------------------------------------
        const FVector Move = Particle.Velocity * DeltaTime;
        const float MoveDist = Move.Length();

        if (MoveDist < 1e-4f)
            continue;

        // --------------------------------------------------------
        // 8. CollisionQueryMode에 따라 Raycast 또는 SphereSweep 수행
        //
        // Raycast     : 파티클 중심점 경로(선)로 검사. 빠름.
        // SphereSweep : CollisionRadius 크기의 구가 이동한 부피로 검사. 정확함.
        //               Hit.ShapeLocation = 충돌 당시 구 중심 위치.
        //               Hit.WorldHitLocation = 표면 접촉점.
        // --------------------------------------------------------
        FHitResult Hit;
        bool bGotHit = false;

        if (CollisionQueryMode == EParticleCollisionQueryMode::Raycast)
        {
            // 파티클 중심점 경로(선)로 검사. MoveDist + Radius로 ray를 살짝 늘려
            // 표면 진입 직전에 감지. 빠르지만 radius 기반 정확도는 낮음.
            const FVector MoveDir = Move / MoveDist;
            bGotHit = PhysScene->Raycast(
                Particle.Location,
                MoveDir,
                MoveDist + CollisionRadius,
                Hit,
                TraceChannel,
                OwnerActor);
        }
        else if (CollisionQueryMode == EParticleCollisionQueryMode::ExpandedAABBSweep)
        {
            // AABB를 Radius만큼 팽창 후 ray test (Minkowski sum 근사).
            // Raycast보다 정확하고, 완전한 sphere sweep보다 빠름.
            // Native / PhysX 모두 동일한 ExpandedAABB 경로를 사용한다.
            const FVector SweepEnd = Particle.Location + Move;
            bGotHit = PhysScene->SphereSweep(
                Particle.Location,
                SweepEnd,
                CollisionRadius,
                Hit,
                TraceChannel,
                OwnerActor);
        }
        else // SphereSweep
        {
            // 구가 이동한 부피를 정확히 검사.
            // PhysX 백엔드에서는 PxScene::sweep()을 사용하여 가장 정확함.
            // Native 백엔드에서는 ExpandedAABBSweep으로 폴백.
            const FVector SweepEnd = Particle.Location + Move;
            bGotHit = PhysScene->SphereSweep(
                Particle.Location,
                SweepEnd,
                CollisionRadius,
                Hit,
                TraceChannel,
                OwnerActor);
        }

        if (!bGotHit || !Hit.bHit)
            continue;

        // --------------------------------------------------------
        // 9. Hit Object 표면 normal 획득
        // --------------------------------------------------------
        const FVector ImpactNormal = Hit.ImpactNormal;

        // --------------------------------------------------------
        // 9-1. 모드별 파티클 위치 보정 기준점 계산
        // --------------------------------------------------------
        // Raycast     : Hit.ShapeLocation == 표면 접촉점.
        //               CollisionRadius만큼 normal 방향으로 띄워야 표면 안으로 파고들지 않음.
        // SphereSweep : Hit.ShapeLocation == 구 중심 위치 (이미 Radius만큼 떠 있음).
        //               그대로 사용.
        const FVector RestPosition =
            (CollisionQueryMode == EParticleCollisionQueryMode::Raycast)
            ? Hit.WorldHitLocation + ImpactNormal * CollisionRadius
            : Hit.ShapeLocation;

        // --------------------------------------------------------
        // 10. 충돌 횟수 누적
        // --------------------------------------------------------
        Payload->CollisionCount++;

        // --------------------------------------------------------
        // 11. Collision Event 발행
        // --------------------------------------------------------
        // bApplyPhysics 여부와 무관하게 충돌 이벤트는 항상 발행한다.
        Owner->PublishParticleEvents(
            EParticleEventType::PEET_Collision,
            Particle,
            Index,
            OutEventQueue,
            ImpactNormal);

        // --------------------------------------------------------
        // 12. MaxCollisions 도달 시 CompletionOption 처리
        // --------------------------------------------------------
        // MaxCollisions가 0 이하이면 충돌 횟수 제한을 사용하지 않는 것으로 본다.
        //
        // 이 조건이 없으면 MaxCollisions == 0일 때
        // 첫 충돌부터 CollisionCount >= MaxCollisions가 true가 되어
        // 의도치 않게 Kill / Freeze 등이 바로 실행될 수 있다.
        if (MaxCollisions > 0 && Payload->CollisionCount >= MaxCollisions)
        {
            const FVector CorrectedLocation = RestPosition;

            switch (CompletionOption)
            {
            case EParticleCollisionCompletionOption::EPCC_Kill:
                // ------------------------------------------------
                // 충돌 횟수 초과 시 파티클 제거
                // ------------------------------------------------
                Owner->KillParticle(Index);
                continue;

            case EParticleCollisionCompletionOption::EPCC_Freeze:
                // ------------------------------------------------
                // 충돌 지점에 파티클 고정
                // ------------------------------------------------
                // 이동 속도를 제거하고, 위치를 충돌 표면 바깥으로 보정한다.
                // 회전은 유지한다.
                Particle.Velocity = FVector::ZeroVector;
                Particle.BaseVelocity = FVector::ZeroVector;
                Particle.Location = CorrectedLocation;

                Payload->bStopCollision = 1;
                continue;

            case EParticleCollisionCompletionOption::EPCC_HaltCollisions:
                // ------------------------------------------------
                // 이후 충돌 검사만 중단
                // ------------------------------------------------
                // 이번 충돌에 대한 반사 처리는 아래에서 계속 수행한다.
                Payload->bStopCollision = 1;
                break;

            case EParticleCollisionCompletionOption::EPCC_FreezeTranslation:
                // ------------------------------------------------
                // 이동만 정지
                // ------------------------------------------------
                // 기존 버전과 달리 위치도 충돌 지점으로 보정한다.
                // 회전은 유지한다.
                Particle.Velocity = FVector::ZeroVector;
                Particle.BaseVelocity = FVector::ZeroVector;
                Particle.Location = CorrectedLocation;

                Payload->bStopCollision = 1;
                continue;

            case EParticleCollisionCompletionOption::EPCC_FreezeRotation:
                // ------------------------------------------------
                // 회전만 정지
                // ------------------------------------------------
                // 이동 속도는 유지한다.
                // 이 option은 이동을 멈추는 것이 아니므로 위치 보정은 하지 않는다.
                Particle.RotationRate = 0.0f;
                Particle.BaseRotationRate = 0.0f;

                Payload->bStopCollision = 1;
                continue;

            case EParticleCollisionCompletionOption::EPCC_FreezeMovement:
                // ------------------------------------------------
                // 이동 + 회전 모두 정지
                // ------------------------------------------------
                // 기존 버전과 달리 위치도 충돌 지점으로 보정한다.
                Particle.Velocity = FVector::ZeroVector;
                Particle.BaseVelocity = FVector::ZeroVector;
                Particle.RotationRate = 0.0f;
                Particle.BaseRotationRate = 0.0f;
                Particle.Location = CorrectedLocation;

                Payload->bStopCollision = 1;
                continue;
            }
        }

        // --------------------------------------------------------
        // 13. Physics Response 적용 여부 검사
        // --------------------------------------------------------
        // bApplyPhysics가 false여도 여기까지의 처리는 이미 수행됨:
        // - 충돌 감지
        // - CollisionCount 증가
        // - Collision Event 발행
        // - CompletionOption 처리
        if (!bApplyPhysics)
            continue;

        // --------------------------------------------------------
        // 14. 이미 표면에서 멀어지는 중인지 검사
        // --------------------------------------------------------
        // NdotV < 0:
        //   Velocity가 표면 안쪽을 향하고 있음.
        //   즉, 실제로 충돌 후 반사가 필요한 상태.
        //
        // NdotV >= 0:
        //   이미 표면 normal 방향으로 나가고 있음.
        //   이 상태에서 다시 반사하면 오히려 이상하게 튈 수 있다.
        const float NdotV = Particle.Velocity.Dot(ImpactNormal);

        if (NdotV >= 0.0f)
        {
            Particle.Location = RestPosition;
            continue;
        }

        // --------------------------------------------------------
        // 15. 충돌 표면 normal 기준으로 속도 분해
        // --------------------------------------------------------
        // Velocity = NormalVel + TangentVel
        //
        // NormalVel : 표면 normal 방향 성분
        // TangentVel: 표면을 따라 미끄러지는 접선 방향 성분
        const FVector NormalVel = ImpactNormal * NdotV;
        const FVector TangentVel = Particle.Velocity - NormalVel;

        // --------------------------------------------------------
        // 16. Bounce / Friction 적용
        // --------------------------------------------------------
        // NormalVel은 표면에 수직인 성분이므로 반대 방향으로 뒤집는다.
        // TangentVel은 표면을 따라 미끄러지는 성분이므로 friction으로 줄인다.
        FVector Reflected =
            (NormalVel * -Bounce) +
            (TangentVel * (1.0f - Friction));

        // --------------------------------------------------------
        // 17. DampingFactor 적용
        // --------------------------------------------------------
        // 최종 속도 전체를 감쇠시킨다.
        Reflected = Reflected * DampingFactor;

        // --------------------------------------------------------
        // 18. 파티클 속도 / 위치 갱신
        // --------------------------------------------------------
        Particle.Velocity = Reflected;
        Particle.BaseVelocity = Reflected;

        Particle.Location = RestPosition;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Kill Modules
// ─────────────────────────────────────────────────────────────────────────────


void UParticleModuleKill::Serialize(FArchive& Ar)
{
    UParticleModule::Serialize(Ar);
    Ar << bUseKillBox;
    Ar << bUseKillHeight;
    Ar << KillBox;
    Ar << KillHeight;
}

void UParticleModuleKill::Update(FParticleEmitterInstance* Owner, float DeltaTime, TArray<FParticleEventData>* /*OutEventQueue*/)
{
    if (!bEnabled)
        return;
    if (!bUseKillBox && !bUseKillHeight)
        return;

    uint8*  ParticleData    = Owner->ParticleData;
    uint16* ParticleIndices = Owner->ParticleIndices;
    int32   ParticleStride  = Owner->ParticleStride;
    int32   ActiveParticles = Owner->ActiveParticles;

    for (int32 Index = ActiveParticles - 1; Index >= 0; --Index)
    {
        const int32    RealIndex = ParticleIndices[Index];
        FBaseParticle& Particle  = *reinterpret_cast<FBaseParticle*>(ParticleData + RealIndex * ParticleStride);

        bool bKill = false;

        if (bUseKillHeight && Particle.Location.Z <= KillHeight)
            bKill = true;

        if (!bKill && bUseKillBox)
        {
            const FVector& P = Particle.Location;
            if (P.X >= KillBox.Min.X && P.X <= KillBox.Max.X &&
                P.Y >= KillBox.Min.Y && P.Y <= KillBox.Max.Y &&
                P.Z >= KillBox.Min.Z && P.Z <= KillBox.Max.Z)
                bKill = true;
        }

        if (bKill)
            Owner->KillParticle(Index);
    }
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
