/**
 * @file ParticleEmitterInstance.h
 * @brief Emitter 하나의 Runtime 실행 객체 정의.
 *
 * 포함 타입:
 * - FParticleEmitterInstance: Emitter별 Particle 생성 / 갱신 / 제거 실행 상태
 */

#pragma once

#include "../Rendering/ParticleRenderData.h"

/** Runtime에서 Particle 생성, 갱신, 제거, Event 처리를 담당하는 Emitter Instance */
struct FParticleEmitterInstance
{
    UParticleEmitter         *EmitterTemplate = nullptr; // 원본 Emitter Asset
    UParticleSystemComponent *Component = nullptr;       // 소유 Component
    int32                     CurrentLODLevelIndex = 0;  // 현재 LOD 인덱스
    UParticleLODLevel        *CurrentLODLevel = nullptr; // 현재 LOD

    uint8  *ParticleData = nullptr;    // Particle 데이터 배열
    uint16 *ParticleIndices = nullptr; // 활성 Particle 인덱스 배열
    uint8  *InstanceData = nullptr;    // Instance Payload 데이터

    int32  InstancePayloadSize = 0;                // Instance Payload 크기
    int32  PayloadOffset = 0;                      // Particle Payload 오프셋
    int32  ParticleSize = sizeof(FBaseParticle);   // Particle 전체 크기
    int32  ParticleStride = sizeof(FBaseParticle); // Particle 메모리 간격
    int32  ActiveParticles = 0;                    // 활성 Particle 수
    uint32 ParticleCounter = 0;                    // 누적 생성 카운터
    int32  MaxActiveParticles = 0;                 // 최대 활성 Particle 수
    float  SpawnFraction = 0.0f;                   // SpawnRate 소수점 이월값

    TArray<FParticleEventData> PendingEvents; // 이번 프레임 처리 대기 Event 목록

    void Init(UParticleSystemComponent *InComponent, UParticleEmitter *InTemplate);                                                     // Instance 초기화
    void Tick(float DeltaTime);                                                                                                         // 매 프레임 갱신
    void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector &InitialLocation, const FVector &InitialVelocity); // Particle 생성
    void KillParticle(int32 Index);                                                                                                     // 단일 Particle 제거
    void KillAllParticles();                                                                                                            // 전체 Particle 제거

    int32 GetActiveParticleCount() const { return ActiveParticles; }

    FDynamicEmitterDataBase *CreateDynamicEmitterData(); // 렌더링 데이터 생성

  private:
    void PreSpawn(FBaseParticle &Particle, const FVector &InitialLocation, const FVector &InitialVelocity); // Spawn 기본값 설정
    void PostSpawn(FBaseParticle &Particle, float SpawnTime);                                               // Spawn 이후 보정
    void KillExpiredParticles();                                                                            // 수명 종료 Particle 제거
    void ProcessEvents();                                                                                   // Pending Event 처리
};
