#include "CharacterMovementComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Serialization/Archive.h"

#include <cmath>
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

const FVector& UCharacterMovementComponent::GetVelocity() const
{
	return Velocity;
}

void UCharacterMovementComponent::SetVelocity(const FVector& InVelocity)
{
	Velocity = InVelocity;
}

EMovementMode UCharacterMovementComponent::GetMovementMode() const
{
	return MovementMode;
}

void UCharacterMovementComponent::SetMovementMode(EMovementMode NewMovementMode)
{
	MovementMode = NewMovementMode;
	if (MovementMode == MOVE_Walking)
	{
		// 지상 이동으로 전환되면 낙하 속도는 더 이상 유지하지 않습니다.
		Velocity.Z = 0.0f;
	}
}

float UCharacterMovementComponent::GetControllerDesiredYaw() const
{
	return ControllerDesiredYawDegrees;
}

float UCharacterMovementComponent::GetSpeed2D() const
{
	return std::sqrt(Velocity.X * Velocity.X + Velocity.Y * Velocity.Y);
}

bool UCharacterMovementComponent::IsWalking() const
{
	return MovementMode == MOVE_Walking;
}

bool UCharacterMovementComponent::IsFalling() const
{
	return MovementMode == MOVE_Falling;
}

bool UCharacterMovementComponent::IsMovingOnGround() const
{
	return IsWalking();
}
