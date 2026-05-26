/**
 * @file ParticleCollisionModules.h
 * @brief Particle 충돌 처리 Module 정의.
 *
 * 포함 클래스:
 * - UParticleModuleCollision: Particle 충돌 처리
 */

#pragma once

#include "Core/CollisionTypes.h"
#include "ParticleCollisionModules.generated.h"
#include "ParticleMotionModules.h"

/**
 * UParticleModuleCollision이 각 파티클에 붙여 쓰는 per-particle 상태.
 * RequiredBytes()가 이 크기만큼 파티클 페이로드를 예약한다.
 */
struct FParticleCollisionPayload
{
    int32 CollisionCount = 0; // 지금까지 충돌한 횟수
    int32 bStopCollision = 0; // 1이면 이 파티클의 충돌 검사를 중단
};

/** Particle과 World 또는 Actor의 충돌을 처리하는 모듈 */
UCLASS()
class UParticleModuleCollision : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleCollision)

    virtual EParticleModuleType GetModuleType() const override
    {
        return EParticleModuleType::PMT_Collision;
    }
    virtual EParticleModuleUpdatePhase GetUpdatePhase() const override
    {
        return EParticleModuleUpdatePhase::PMUP_SpawnAndUpdate;
    }
    virtual EParticleModuleClass GetModuleClass() const override
    {
        return EParticleModuleClass::Collision;
    }

    virtual uint32 RequiredBytes(UParticleModuleTypeDataBase *) const override
    {
        return static_cast<uint32>(sizeof(FParticleCollisionPayload));
    }

    virtual void Serialize(FArchive &Ar) override;
    virtual void PostEditProperty(const char* PropertyName) override;
    virtual void PreSpawn(FParticleEmitterInstance *Owner, FBaseParticle &Particle) override;
    virtual void Update(FParticleEmitterInstance *Owner, float DeltaTime, TArray<FParticleEventData>* OutEventQueue = nullptr) override;

  private:
    // ── 파티클 쪽 설정 ──────────────────────────────────────────────
    UPROPERTY(Edit, Category = "Collision", DisplayName = "Enable Collision")
    bool bEnableCollision = true;

    // Raycast           : 빠름, 대량 소형 파티클 (spark, rain)
    // ExpandedAABBSweep : 빠른 반지름 근사, 일반 파티클 기본값
    // SphereSweep       : 정확, 크기가 보이는 파티클 (debris, mesh particle)
    UPROPERTY(Edit, Category = "Collision", DisplayName = "Collision Query Mode",
              Type = Enum, Enum = StaticEnum_EParticleCollisionQueryMode())
    EParticleCollisionQueryMode CollisionQueryMode = EParticleCollisionQueryMode::ExpandedAABBSweep;

    UPROPERTY(Edit, Category = "Collision", DisplayName = "Max Collisions", Min = 1, Speed = 1)
    int32 MaxCollisions = 1; // 이 횟수 초과 시 CompletionOption 실행

    UPROPERTY(Edit, Category = "Collision", DisplayName = "Completion Option")
    EParticleCollisionCompletionOption CompletionOption = EParticleCollisionCompletionOption::EPCC_Kill;

    UPROPERTY(Edit, Category = "Collision", DisplayName = "Bounce", Min = 0.0, Max = 1.0, Speed = 0.01)
    float Bounce = 0.3f; // 법선 방향 반사 계수 (0=완전흡수, 1=완전반사)

    UPROPERTY(Edit, Category = "Collision", DisplayName = "Friction", Min = 0.0, Max = 1.0, Speed = 0.01)
    float Friction = 0.1f; // 접선 방향 감쇠 계수

    UPROPERTY(Edit, Category = "Collision", DisplayName = "Damping Factor", Min = 0.0, Max = 2.0, Speed = 0.01)
    float DampingFactor = 1.0f; // 반사 후 전체 속도에 곱하는 스케일

    UPROPERTY(Edit, Category = "Collision", DisplayName = "Collision Radius", Min = 0.01, Speed = 0.1)
    float CollisionRadius = 1.0f; // 충돌 판정용 구 반지름 (렌더 크기 무관, 0 이하 불가)

    UPROPERTY(Edit, Category = "Collision", DisplayName = "Max Collision Distance", Min = 0.0, Speed = 1.0)
    float MaxCollisionDistance = 0.0f; // 이미터 기준 이 거리 초과 파티클은 충돌 무시 (0=제한없음)

    UPROPERTY(Edit, Category = "Collision", DisplayName = "Apply Physics")
    bool bApplyPhysics = true; // false 이면 이벤트만 발행, 속도/위치 수정 없음

    // ── 월드 오브젝트 쪽 설정 ────────────────────────────────────────
    // 어떤 채널에 응답하는 Actor/Component와 충돌할지 결정.
    // 대상 Component의 해당 채널 Response가 Block일 때만 Hit로 인정된다.
    // (WorldStatic = 지형/건물, WorldDynamic = 움직이는 오브젝트, Projectile 등)
    UPROPERTY(Edit, Category = "Collision", DisplayName = "Trace Channel", Type = Enum,
              Enum = StaticEnum_ECollisionChannel())
    ECollisionChannel TraceChannel = ECollisionChannel::WorldStatic;
};
