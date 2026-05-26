/**
 * @file ParticleTypes.h
 * @brief Particle System 공용 enum / forward declaration 정의.
 *
 * 포함 타입:
 * - EParticleEmitterType: Sprite / Mesh / Beam / Ribbon Emitter 타입
 * - EParticleEmitterRenderMode: Emitter 디버그 / 표시 렌더 모드
 * - EParticleModuleType: Module 분류 타입
 * - EParticleModuleUpdatePhase: Spawn / Update 실행 시점
 * - EParticleEventType: Particle Event 종류
 * - EParticleSortMode: Particle 정렬 방식
 * - EDynamicEmitterType: Rendering 전달용 Dynamic Emitter 타입
 */

#pragma once
#include "Core/CoreTypes.h"
#include "Core/EngineTypes.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"
#include "ParticleTypes.generated.h"

class UObject;
class UMaterial;
class UTexture;
class UStaticMesh;
class UParticleSystem;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleModule;
class UParticleModuleRequired;
class UParticleModuleTypeDataBase;
class UParticleSystemComponent;

struct FBaseParticle;
struct FParticleEmitterInstance;
struct FDynamicEmitterReplayDataBase;
struct FDynamicEmitterDataBase;

/** Emitter의 기본 렌더링 / 구성 타입 */
UENUM()
enum class EParticleEmitterType : uint8
{
    PET_Sprite,     // Quad Sprite 기반 Emitter
    PET_Mesh,       // Mesh Particle 기반 Emitter
    PET_Beam,       // 시작점-끝점 기반 선형 Emitter
    PET_Ribbon      // Particle 궤적 기반 Trail Emitter
};

/** Emitter의 에디터 / 디버그 렌더 표시 방식 */
UENUM()
enum class EParticleEmitterRenderMode : uint8
{
    ERM_Normal,     // 원래 의도된 방식으로 렌더링
    ERM_Point,      // 2x2 픽셀 점으로 렌더링
    ERM_Cross,      // 십자 라인 형태로 렌더링
    ERM_None        // 렌더링하지 않음
};

/** Particle Module의 분류 타입 */
enum class EParticleModuleType : uint8
{
    PMT_Required,       // Emitter 필수 설정
    PMT_Spawn,          // Particle 생성 수 / 생성 주기
    PMT_Lifetime,       // Particle 수명
    PMT_Location,       // Particle 위치
    PMT_Velocity,       // Particle 속도
    PMT_Color,          // Particle 색상 / 알파
    PMT_Size,           // Particle 크기

    PMT_Rotation,       // Particle 초기 회전
    PMT_RotationRate,   // Particle 회전 속도
    PMT_Acceleration,   // Particle 가속도 / 힘
    PMT_Kill,           // 조건 기반 Particle 제거
    PMT_Collision,      // Particle 충돌 처리
    PMT_Event,          // Particle 이벤트 처리
    PMT_SubUV,          // Texture Atlas / Flipbook

    PMT_Light,          // Particle 기반 Light 효과
    PMT_VectorField,    // 3D Vector Field 기반 이동
    PMT_Attractor,      // 특정 위치 / Actor 방향 흡인
    PMT_Orbit,          // 렌더링 위치 궤도 오프셋
    PMT_Camera,         // 카메라 기준 동작 보정
    PMT_Parameter,      // 외부 시스템 / Material 값 전달

    PMT_Custom          // 확장용 Module
};

/** Module이 실행되는 시점 */
enum class EParticleModuleUpdatePhase : uint8
{
    PMUP_Spawn,         // Spawn 시점 실행
    PMUP_Update,        // Update 시점 실행
    PMUP_SpawnAndUpdate // Spawn / Update 양쪽 실행
};

/** Particle Event 종류 */
UENUM()
enum class EParticleEventType : uint8
{
    PEET_Spawn,          // Particle 생성 이벤트
    PEET_Death,          // Particle 소멸 이벤트
    PEET_Collision,      // Particle 충돌 이벤트
    PEET_Burst,          // Burst Spawn 이벤트
    PEET_Custom          // 확장용 이벤트
};

/** Particle 정렬 방식 */
UENUM()
enum class EParticleSortMode : uint8
{
    PSM_None,           // 정렬 없음
    PSM_DistanceToView, // 카메라 거리 기준 정렬
    PSM_Age             // Particle 나이 기준 정렬
};

/** 다형 직렬화 시 모듈 구체 클래스를 식별하는 태그 */
enum class EParticleModuleClass : uint8
{
    Required = 0, Spawn, Lifetime, Location, Velocity, Color, Size,
    Rotation, RotationRate, Acceleration, Attractor, Orbit,
    Collision, Kill,
    EventGenerator, EventReceiverSpawn, EventReceiverKillAll,
    SubUV, Light, VectorField, Camera, Parameter,
    TypeDataSprite, TypeDataMesh, TypeDataBeam, TypeDataRibbon,
    Unknown = 0xFF
};

/** Collision Module의 충돌 쿼리 방식 */
UENUM()
enum class EParticleCollisionQueryMode : uint8
{
    // 파티클 중심점 경로를 선분으로 검사.
    // 가장 빠름. spark, rain, 소형 파티클 대량 처리에 적합.
    Raycast,

    // AABB를 Radius만큼 팽창(Minkowski sum)한 뒤 ray test.
    // CollisionRadius를 반영한 빠른 근사 방식.
    // 모서리/엣지 근처에서 보수적(과검출)이나 대부분 파티클에 실용적.
    // 기본 CPU particle collision에 적합.
    ExpandedAABBSweep,

    // CollisionRadius를 반지름으로 하는 구가 이동한 부피를 정확히 검사.
    // PhysX 백엔드에서는 PxScene::sweep()을 사용하여 정확도가 높음.
    // Native 백엔드에서는 현재 ExpandedAABBSweep으로 폴백.
    // 크기가 눈에 띄는 파티클, mesh particle, debris에 적합.
    SphereSweep,
};

/** Collision Module에서 MaxCollisions 초과 시 취할 동작 */
UENUM()
enum class EParticleCollisionCompletionOption : uint8
{
    EPCC_Kill,             // MaxCollisions 도달 시 파티클 제거
    EPCC_Freeze,           // 파티클 완전 고정, 이후 업데이트 중단
    EPCC_HaltCollisions,   // 충돌 검사만 중단, 나머지 업데이트는 계속
    EPCC_FreezeTranslation,// 위치 이동만 멈추고, 나머지 업데이트는 계속
    EPCC_FreezeRotation,   // 회전만 멈추고, 위치 이동 등은 계속
    EPCC_FreezeMovement,   // 위치 이동 + 회전을 멈추고, 나머지 업데이트는 계속
};

/** Rendering 계층으로 전달되는 Dynamic Emitter 타입 */
enum class EDynamicEmitterType : uint8
{
    DET_Sprite,         // Sprite Emitter 렌더 데이터
    DET_Mesh,           // Mesh Emitter 렌더 데이터
    DET_Beam,           // Beam Emitter 렌더 데이터
    DET_Ribbon          // Ribbon Emitter 렌더 데이터
};
