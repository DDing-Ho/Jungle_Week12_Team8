/**
 * @file ParticleAsset.h
 * @brief ParticleSystem Asset 계층 정의.
 *
 * 포함 클래스:
 * - UParticleSystem: 여러 Emitter를 묶는 Particle System Asset
 * - UParticleEmitter: 개별 Emitter Asset
 * - UParticleLODLevel: Emitter의 LOD별 Module 설정
 */

#pragma once

#include "Object/Object.h"
#include "Particles/Assets/ParticleTypeData.h"
#include "ParticleAsset.generated.h"

/** LOD별 Module, TypeData, 실행 캐시를 보관하는 클래스 */
UCLASS()
class UParticleLODLevel : public UObject
{
  public:
    GENERATED_BODY(UParticleLODLevel)

    int32 GetLevel() const { return Level; }
    void  SetLevel(int32 InLevel) { Level = InLevel; }

    bool IsEnabled() const { return bEnabled; }
    void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

    UParticleModuleRequired *GetRequiredModule() const { return RequiredModule; }
    void                     SetRequiredModule(UParticleModuleRequired *InModule) { RequiredModule = InModule; }

    UParticleModuleTypeDataBase *GetTypeDataModule() const { return TypeDataModule; }
    void                         SetTypeDataModule(UParticleModuleTypeDataBase *InModule) { TypeDataModule = InModule; }

    const TArray<UParticleModule *> &GetModules() const { return Modules; }
    const TArray<UParticleModule *> &GetSpawnModules() const { return SpawnModules; }
    const TArray<UParticleModule *> &GetUpdateModules() const { return UpdateModules; }

    void AddModule(UParticleModule *InModule); // 일반 Module 추가
    void CacheModules();                       // Spawn / Update 실행 캐시 구성
    virtual void Serialize(FArchive& Ar) override;

  private:
    int32 Level = 0;       // LOD 단계
    bool  bEnabled = true; // LOD 사용 여부

    UParticleModuleRequired     *RequiredModule = nullptr; // 필수 Emitter 설정
    TArray<UParticleModule *>    Modules;                  // 전체 일반 Module 목록
    UParticleModuleTypeDataBase *TypeDataModule = nullptr; // Emitter 타입 설정

    TArray<UParticleModule *> SpawnModules;  // Spawn 실행 캐시
    TArray<UParticleModule *> UpdateModules; // Update 실행 캐시
};

/** 단일 Particle Emitter Asset */
UCLASS()
class UParticleEmitter : public UObject
{
  public:
    GENERATED_BODY(UParticleEmitter)

    FName GetEmitterName() const { return EmitterName; }
    void  SetEmitterName(const FName &InName) { EmitterName = InName; }

    EParticleEmitterRenderMode GetEmitterRenderMode() const { return EmitterRenderMode; }
    void                       SetEmitterRenderMode(EParticleEmitterRenderMode InMode) { EmitterRenderMode = InMode; }

    FColor GetEmitterEditorColor() const { return EmitterEditorColor; }
    void   SetEmitterEditorColor(const FColor &InColor) { EmitterEditorColor = InColor; }

    int32 GetInitialAllocationCount() const { return InitialAllocationCount; }
    void  SetInitialAllocationCount(int32 InCount) { InitialAllocationCount = InCount; }

    float GetMediumDetailSpawnRateScale() const { return MediumDetailSpawnRateScale; }
    void  SetMediumDetailSpawnRateScale(float InScale) { MediumDetailSpawnRateScale = InScale; }

    bool IsCollapsed() const { return bCollapsed; }
    void SetCollapsed(bool bInCollapsed) { bCollapsed = bInCollapsed; }

    const TArray<UParticleLODLevel *> &GetLODLevels() const { return LODLevels; }

    UParticleLODLevel *GetLODLevel(int32 Index) const { return Index < (int32)LODLevels.size() ? LODLevels[Index] : nullptr; }

    void AddLODLevel(UParticleLODLevel *InLODLevel) { LODLevels.push_back(InLODLevel); }

    void CacheEmitterModuleInfo(); // Emitter Module 정보 캐싱
    virtual void Serialize(FArchive& Ar) override;

    int32 GetParticleSize() const { return ParticleSize; }

    int32 GetMaxActiveParticles() const { return MaxActiveParticles; }
    void  SetMaxActiveParticles(int32 InMaxCount) { MaxActiveParticles = InMaxCount; }

  private:
    FName EmitterName; // Emitter 이름

    EParticleEmitterRenderMode EmitterRenderMode = EParticleEmitterRenderMode::ERM_Normal; // Emitter 렌더 표시 모드

    FColor EmitterEditorColor = FColor::White(); // 에디터 / 디버그 표시 색상

    int32 InitialAllocationCount = 0; // 초기화 시 미리 할당할 Particle 수

    float MediumDetailSpawnRateScale = 1.0f; // Medium / Low Detail에서 SpawnRate를 줄이는 배율

    bool bCollapsed = false; // 에디터 Emitter List 접힘 상태

    TArray<UParticleLODLevel *> LODLevels; // Emitter LOD 목록

    int32 ParticleSize = 0; // Particle 1개 메모리 크기

    int32 MaxActiveParticles = 1000; // 최대 활성 Particle 수
};

/** 여러 Emitter를 묶는 Particle System Asset */
UCLASS()
class UParticleSystem : public UObject
{
  public:
    GENERATED_BODY(UParticleSystem)

    const TArray<UParticleEmitter *> &GetEmitters() const { return Emitters; }

    UParticleEmitter *GetEmitter(int32 Index) const { return Index < (int32)Emitters.size() ? Emitters[Index] : nullptr; }

    void AddEmitter(UParticleEmitter *InEmitter) { Emitters.push_back(InEmitter); }

    void CacheSystemModuleInfo(); // 전체 Emitter Module 정보 캐싱
    virtual void Serialize(FArchive& Ar) override;

    //UPROPERTY()
    TArray<float> LODDistances; // LOD 전환 거리 목록

    void SetSourcePath(const FString& InPath) { SourcePath = InPath; }
    const FString& GetSourcePath() const { return SourcePath; }
    const FString& GetAssetPathFileName() const override { return SourcePath; }

  private:
    TArray<UParticleEmitter *> Emitters; // ParticleSystem 구성 Emitter 목록
    FString SourcePath;
};
