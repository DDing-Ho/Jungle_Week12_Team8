/**
 * @file ParticleTypeData.h
 * @brief Emitter 렌더링 타입을 결정하는 TypeData 정의.
 *
 * 포함 클래스:
 * - UParticleModuleTypeDataBase: TypeData base class
 * - UParticleModuleTypeDataSprite: Sprite Emitter 타입
 * - UParticleModuleTypeDataMesh: Mesh Emitter 타입
 * - UParticleModuleTypeDataBeam: Beam Emitter 타입
 * - UParticleModuleTypeDataRibbon: Ribbon / Trail Emitter 타입
 */

#pragma once

#include "../Modules/ParticleRenderExpressionModules.h"
#include "Core/UObject/TSoftObjectPtr.h"
#include "Mesh/StaticMesh.h"
#include "ParticleTypeData.generated.h"

/** Emitter 렌더링 타입을 결정하는 TypeData 기반 클래스 */
UCLASS()
class UParticleModuleTypeDataBase : public UParticleModule
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataBase)

    virtual EParticleEmitterType GetEmitterType() const { return EParticleEmitterType::PET_Sprite; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::Unknown; }
    virtual void Serialize(FArchive& Ar) override { UParticleModule::Serialize(Ar); }
};

/** Sprite Emitter TypeData */
UCLASS()
class UParticleModuleTypeDataSprite : public UParticleModuleTypeDataBase
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataSprite)

    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Sprite; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::TypeDataSprite; }
    virtual void Serialize(FArchive& Ar) override;
};

/** Mesh Particle Emitter TypeData */
UCLASS()
class UParticleModuleTypeDataMesh : public UParticleModuleTypeDataBase
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataMesh)

    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Mesh; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::TypeDataMesh; }
    virtual void Serialize(FArchive& Ar) override;
    virtual void PostEditProperty(const char* PropertyName) override;

    UStaticMesh *GetMesh() const { return Mesh; }
    void         SetMesh(UStaticMesh *InMesh);

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Static Mesh", Type=SoftObject, Class=UStaticMesh)
    TSoftObjectPtr<UStaticMesh> MeshAsset;
    UStaticMesh *Mesh = nullptr;      // Mesh Particle에 사용할 StaticMesh
    FString      MeshPath;            // Serialize 시 경로 저장용
};

struct FBeamParticlePayload
{
    FVector Source = FVector::ZeroVector;
    FVector Target = FVector::ZeroVector;
    float   Width = 1.0f;
    float   TextureTiling = 1.0f;
};

/** Beam Emitter TypeData */
UCLASS()
class UParticleModuleTypeDataBeam : public UParticleModuleTypeDataBase
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataBeam)

    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Beam; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::TypeDataBeam; }
	virtual uint32 RequiredBytes(UParticleModuleTypeDataBase* /*TypeData*/) const override { return sizeof(FBeamParticlePayload); }
	virtual void Serialize(FArchive& Ar) override;

	const FVector& GetSource() const { return Source; }
	void    SetSource(const FVector& InSource) { Source = InSource; }
	const FVector& GetTarget() const { return Target; }
	void    SetTarget(const FVector& InTarget) { Target = InTarget; }
	float   GetWidth() const { return Width; }
	void    SetWidth(float InWidth) { Width = InWidth; }
	float   GetTextureTiling() const { return TextureTiling; }
	void    SetTextureTiling(float InTextureTiling) { TextureTiling = InTextureTiling; }

  private:
    UPROPERTY(Edit, Category="Particle", DisplayName="Source")
    FVector Source = FVector::ZeroVector; // Beam 시작점
    UPROPERTY(Edit, Category="Particle", DisplayName="Target")
    FVector Target = FVector::ZeroVector; // Beam 끝점
    UPROPERTY(Edit, Category="Particle", DisplayName="Width", Min=0.0, Max=100000.0, Speed=0.1)
    float   Width = 1.0f;                 // Beam 두께
    UPROPERTY(Edit, Category="Particle", DisplayName="Texture Tiling", Min=0.0, Max=100000.0, Speed=0.1)
    float   TextureTiling = 1.0f;         // Beam Texture 반복 비율
};

/** Ribbon / Trail Emitter TypeData */
UCLASS()
class UParticleModuleTypeDataRibbon : public UParticleModuleTypeDataBase
{
  public:
    GENERATED_BODY(UParticleModuleTypeDataRibbon)

    virtual EParticleEmitterType GetEmitterType() const override { return EParticleEmitterType::PET_Ribbon; }
    virtual EParticleModuleClass GetModuleClass() const override { return EParticleModuleClass::TypeDataRibbon; }
    virtual void Serialize(FArchive& Ar) override;

    int32 GetSheetsPerTrail() const { return SheetsPerTrail; }
    int32 GetMaxTrailCount() const { return MaxTrailCount; }
    int32 GetMaxParticleInTrailCount() const { return MaxParticleInTrailCount; }
    bool ShouldSpawnInitialParticle() const { return bSpawnInitialParticle; }
    bool ShouldDeadTrailsOnDeactivate() const { return bDeadTrailsOnDeactivate; }
    bool ShouldDeadTrailsOnSourceLoss() const { return bDeadTrailsOnSourceLoss; }
    EParticleRibbonRenderAxis GetRenderAxis() const { return RenderAxis; }
    float GetTilingDistance() const { return TilingDistance; }
    float GetDistanceTessellationStepSize() const { return DistanceTessellationStepSize; }
    bool ShouldRecalculateTangentsEveryFrame() const { return bTangentRecalculationEveryFrame; }
    bool ShouldEnablePreviousTangentRecalculation() const { return bEnablePreviousTangentRecalculation; }
    bool ShouldRenderGeometry() const { return bRenderGeometry; }
    bool ShouldRenderSpawnPoints() const { return bRenderSpawnPoints; }
    bool ShouldRenderTangents() const { return bRenderTangents; }
    bool ShouldRenderTessellation() const { return bRenderTessellation; }

  private:
    UPROPERTY(Edit, Category="Ribbon", DisplayName="Sheets Per Trail", Min=1, Max=8, Speed=1.0)
    int32 SheetsPerTrail = 1;
    UPROPERTY(Edit, Category="Ribbon", DisplayName="Max Trail Count", Min=1, Max=1024, Speed=1.0)
    int32 MaxTrailCount = 1;
    UPROPERTY(Edit, Category="Ribbon", DisplayName="Max Particle In Trail Count", Min=2, Max=100000, Speed=1.0)
    int32 MaxParticleInTrailCount = 256;

    UPROPERTY(Edit, Category="Ribbon", DisplayName="Spawn Initial Particle")
    bool bSpawnInitialParticle = true;
    UPROPERTY(Edit, Category="Ribbon", DisplayName="Dead Trails On Deactivate")
    bool bDeadTrailsOnDeactivate = true;
    UPROPERTY(Edit, Category="Ribbon", DisplayName="Dead Trails On Source Loss")
    bool bDeadTrailsOnSourceLoss = true;

    UPROPERTY(Edit, Category="Ribbon", DisplayName="Render Axis", Type=Enum, Enum=StaticEnum_EParticleRibbonRenderAxis())
    EParticleRibbonRenderAxis RenderAxis = EParticleRibbonRenderAxis::CameraUp;
    UPROPERTY(Edit, Category="Ribbon", DisplayName="Tiling Distance", Min=0.0, Max=100000.0, Speed=0.1)
    float TilingDistance = 100.0f;
    UPROPERTY(Edit, Category="Ribbon", DisplayName="Distance Tessellation Step Size", Min=0.0, Max=100000.0, Speed=0.1)
    float DistanceTessellationStepSize = 0.0f;

    UPROPERTY(Edit, Category="Ribbon", DisplayName="Tangent Recalculation Every Frame")
    bool bTangentRecalculationEveryFrame = true;
    UPROPERTY(Edit, Category="Ribbon", DisplayName="Enable Previous Tangent Recalculation")
    bool bEnablePreviousTangentRecalculation = true;

    UPROPERTY(Edit, Category="Ribbon Debug", DisplayName="Render Geometry")
    bool bRenderGeometry = true;
    UPROPERTY(Edit, Category="Ribbon Debug", DisplayName="Render Spawn Points")
    bool bRenderSpawnPoints = false;
    UPROPERTY(Edit, Category="Ribbon Debug", DisplayName="Render Tangents")
    bool bRenderTangents = false;
    UPROPERTY(Edit, Category="Ribbon Debug", DisplayName="Render Tessellation")
    bool bRenderTessellation = false;
};
