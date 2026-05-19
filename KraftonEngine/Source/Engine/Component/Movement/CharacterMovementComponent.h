#pragma once
#include "Core/EngineTypes.h"
#include "MovementComponent.h"
#include "CharacterMovementComponent.generated.h"

class UPrimitiveComponent;

/*
 * ACharacter가 사용할 경량 CharacterMovement API의 기반 컴포넌트입니다.
 * 실제 이동/입력/회전 로직은 단계별로 추가합니다.
 */
UCLASS()
class UCharacterMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY(UCharacterMovementComponent)

	UCharacterMovementComponent() = default;
	~UCharacterMovementComponent() override = default;

	void BeginPlay() override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void Serialize(FArchive& Ar) override;
	void PostEditProperty(const char* PropertyName) override;

private:
	UPrimitiveComponent* UpdatedPrimitive = nullptr;
};

