#include "CharacterMovementComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Serialization/Archive.h"

#include <cstring>

void UCharacterMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();
	UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());
}

void UCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());
}

void UCharacterMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);
	Ar << MovementMode;
	Ar << MaxWalkSpeed;
	Ar << MaxAcceleration;
	Ar << BrakingDecelerationWalking;
	Ar << GroundFriction;
	Ar << GravityZ;
	Ar << JumpZVelocity;
	Ar << AirControl;
	Ar << bOrientRotationToMovement;
	Ar << bUseControllerDesiredRotation;
	Ar << RotationRateYaw;
	Ar << MouseSensitivity;
}

void UCharacterMovementComponent::PostEditProperty(const char* PropertyName)
{
	UMovementComponent::PostEditProperty(PropertyName);

	if (std::strcmp(PropertyName, "Orient Rotation To Movement") == 0 && bOrientRotationToMovement)
	{
		// 이동 방향 회전과 컨트롤러 회전은 같은 프레임에 동시에 적용할 수 없으므로 하나만 활성화
		bUseControllerDesiredRotation = false;
	}
	else if (std::strcmp(PropertyName, "Use Controller Desired Rotation") == 0 && bUseControllerDesiredRotation)
	{
		// 컨트롤러 Yaw를 우선 사용할 때는 이동 방향 회전을 끕니다.
		bOrientRotationToMovement = false;
	}

	UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());
}
