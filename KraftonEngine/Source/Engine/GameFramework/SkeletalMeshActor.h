#pragma once

#include "GameFramework/AActor.h"
#include "SkeletalMeshActor.generated.h"

class USkeletalMeshComponent;

UCLASS(Actor)
class ASkeletalMeshActor : public AActor
{
public:
	GENERATED_BODY(ASkeletalMeshActor)

	ASkeletalMeshActor() = default;

	void BeginPlay() override;

	void InitDefaultComponents(const FString& SkeletalMeshFileName = "Data/Samba Dancing (10).fbx");

private:
	USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
};
