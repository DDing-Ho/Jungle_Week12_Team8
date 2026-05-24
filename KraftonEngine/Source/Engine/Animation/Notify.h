#pragma once
#include "Object/Object.h"
#include "Notify.generated.h"

class AActor;
class USkeletalMeshComponent;

UCLASS()
class UNotify : public UObject
{
public:
	GENERATED_BODY(UNotify)

	virtual void OnNotify(AActor* MeshOwner, USkeletalMeshComponent* MeshComp);
};
