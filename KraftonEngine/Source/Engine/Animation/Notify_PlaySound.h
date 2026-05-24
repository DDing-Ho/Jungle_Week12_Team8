#pragma once
#include "Notify.h"
#include "Notify_PlaySound.generated.h"

// notify 예시용 클래스
UCLASS()
class UNotify_PlaySound : public UNotify
{
public:
	GENERATED_BODY(UNotify_PlaySound)

	virtual void OnNotify(AActor* MeshOwner, USkeletalMeshComponent* MeshComp) override;

public:
	FString AudioKey;
	float   Volume = 1.0f;
	bool    bLoop = false;
};
