/**
 * @file ParticleEmitterInstance.h
 * @brief Emitter 하나의 Runtime 실행 객체 정의.
 *
 * 포함 타입:
 * - FParticleEmitterInstance: Emitter별 Particle 생성 / 갱신 / 제거 실행 상태
 */

#pragma once

#include "../Rendering/ParticleRenderData.h"
#include "ParticleRuntimeTypes.h"
#include "Core/CoreTypes.h"
#include "Math/Vector.h"

/** Runtime에서 Particle 생성, 갱신, 제거, Event 처리를 담당하는 Emitter Instance */
struct FParticleEmitterInstance
{
	virtual ~FParticleEmitterInstance();

    UParticleEmitter         *EmitterTemplate = nullptr; // 원본 Emitter Asset
    UParticleSystemComponent *OwnerComponent = nullptr;       // 소유 Component
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
    int32  SpawnedThisFrame = 0;                   // 이번 프레임 생성 수
    int32  KilledThisFrame = 0;                    // 이번 프레임 제거 수

    TArray<FParticleEventData>  ReceivedEvents; // 이 Emitter가 받을 Event 목록
    
	virtual void Init(UParticleSystemComponent *InComponent, UParticleEmitter *InTemplate);                                                     // Instance 초기화
    virtual void Tick(float DeltaTime, TArray<FParticleEventData>& OutEventQueue, float RealDeltaTime = -1.0f);                                 // 매 프레임 갱신
    void ProcessEvents(TArray<FParticleEventData>& EventQueue);                                                                                 // EventQueue 처리
    void SpawnParticles(int32 Count, float StartTime, float Increment, const FVector &InitialLocation, const FVector &InitialVelocity, TArray<FParticleEventData>* OutEventQueue = nullptr); // Particle 생성
    void KillParticle(int32 Index);                                                                                                     // 단일 Particle 제거
    void KillParticleWithEvents(int32 Index, TArray<FParticleEventData>* OutEventQueue = nullptr);                                      // Death Event와 함께 단일 Particle 제거
    void KillAllParticles(TArray<FParticleEventData>* OutEventQueue = nullptr);                                                           // 전체 Particle 제거
	void Reset();
	void ResetParticleParameters(float DeltaTime);												// 전체 초기화 아님, 틱 중에 초기화되어야하는 파라미터 초기화

    void SetCurrentLODIndex(int32 NewIndex, bool bFullReset = false); // LOD 레벨 전환

    virtual void PreSpawn(FBaseParticle& Particle, const FVector& InitialLocation, const FVector& InitialVelocity); // Spawn 기본값 설정
    virtual void PostSpawn(FBaseParticle& Particle, float SpawnTime, TArray<FParticleEventData>* OutEventQueue);
	virtual void Tick_SpawnParticles(float DeltaTime, TArray<FParticleEventData>* OutEventQueue);
    int32 GetActiveParticleCount() const { return ActiveParticles; }
    float GetEmitterTime() const { return EmitterTime; }
    float GetRealDeltaTime() const { return RealDeltaTime; }

    virtual FDynamicEmitterDataBase *CreateDynamicEmitterData(); // 렌더링 데이터 생성
    void PublishParticleEvents(EParticleEventType EventType, const FBaseParticle& Particle, int32 ParticleIndex, TArray<FParticleEventData>* OutEventQueue, const FVector& EventNormal = FVector::ZeroVector);

  protected:
    void KillExpiredParticles(TArray<FParticleEventData>& OutEventQueue);                                   // 수명 종료 Particle 제거
    bool HasReceiverFor(const FParticleEventData& Event) const;
    void ProcessReceivedEvents(TArray<FParticleEventData>* OutEventQueue);
    int32 ResolveEmitterIndex() const;
    UParticleModuleSpawnPerUnit* FindSpawnPerUnitModule() const;
    bool SupportsComponentSpawnPerUnit() const;
    void ResetComponentSpawnPerUnitState();
    void SpawnParticlesByRate(float DeltaTime, TArray<FParticleEventData>* OutEventQueue);
    void SpawnParticlesByUnitMovement(UParticleModuleSpawnPerUnit* SpawnPerUnitModule, float DeltaTime, TArray<FParticleEventData>* OutEventQueue);
	
	FBaseParticle& GetParticle(int32 index);

	bool bFirstTime = true;	// 처음 스폰 여부
	bool bEnabled = true;	// 가동 여부
	int32 LoopCount = 0;		// 루프 개수

	float EmitterTime = 0.0f;	// 이미터 시간
	float LastDeltaTime = 0.0f;	// 마지막으로 스폰한 시간
    float RealDeltaTime = 0.0f; // Time dilation 적용 전 delta
    FVector LastComponentSpawnPerUnitLocation = FVector::ZeroVector;
    float ComponentSpawnPerUnitDistanceRemainder = 0.0f;
    bool bHasLastComponentSpawnPerUnitLocation = false;
};

// Sprite Paticle============================================================

struct FParticleSpriteEmitterInstance : public FParticleEmitterInstance
{
public:
	FDynamicEmitterDataBase* CreateDynamicEmitterData() override;

};

// Mesh Paticle============================================================

struct FParticleMeshEmitterInstance : public FParticleEmitterInstance 
{
public:
	void Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate) override;
	void Tick(float DeltaTime, TArray<FParticleEventData>& OutEventQueue, float RealDeltaTime = -1.0f) override;
	FDynamicEmitterDataBase* CreateDynamicEmitterData() override;

private:
	UParticleModuleTypeDataMesh* MeshTypeData = nullptr;
	TArray<UMaterial*> CurrentMaterials;
};

struct FParticleBeamEmitterInstance : FParticleEmitterInstance
{
	void Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate) override;
	FDynamicEmitterDataBase* CreateDynamicEmitterData() override;
	void PreSpawn(FBaseParticle& Particle, const FVector& InitialLocation, const FVector& InitialVelocity) override;

private:
	UParticleModuleTypeDataBeam* BeamTypeData = nullptr;
};

struct FParticleRibbonEmitterInstance : FParticleEmitterInstance
{
	void Init(UParticleSystemComponent* InComponent, UParticleEmitter* InTemplate) override;
	FDynamicEmitterDataBase* CreateDynamicEmitterData() override;
	void Tick_SpawnParticles(float DeltaTime, TArray<FParticleEventData>* OutEventQueue) override;
	void PreSpawn(FBaseParticle& Particle, const FVector& InitialLocation, const FVector& InitialVelocity) override;

private:
	struct FSpawnPerUnitSourceState
	{
		uint32 SourceSpawnId = 0;
		FVector LastLocation = FVector::ZeroVector;
		float DistanceRemainder = 0.0f;
		bool bHasLastLocation = false;
	};

	void CacheRibbonModules();
	FParticleEmitterInstance* ResolveSourceEmitterInstance() const;
	void SpawnFromSourceEmitter(FParticleEmitterInstance* SourceInstance, int32 SpawnEvents, float StartTime, float Increment, TArray<FParticleEventData>* OutEventQueue);
	void SpawnSelfFromMovement(float DeltaTime, TArray<FParticleEventData>* OutEventQueue);
	void SpawnFromSourceMovement(FParticleEmitterInstance* SourceInstance, TArray<FParticleEventData>* OutEventQueue);
	FSpawnPerUnitSourceState* FindOrAddSpawnPerUnitSourceState(uint32 SourceSpawnId);
	void PruneSpawnPerUnitSourceStates(const FParticleEmitterInstance* SourceInstance);

	UParticleModuleTypeDataRibbon* RibbonTypeData = nullptr;
	UParticleModuleTrailSource* TrailSourceModule = nullptr;
	UParticleModuleSpawnPerUnit* SpawnPerUnitModule = nullptr;
	uint32 PendingSourceSpawnId = 0;
	FVector LastSelfSpawnPerUnitLocation = FVector::ZeroVector;
	float SelfSpawnPerUnitDistanceRemainder = 0.0f;
	bool bHasLastSelfSpawnPerUnitLocation = false;
	TArray<FSpawnPerUnitSourceState> SpawnPerUnitSourceStates;
};
