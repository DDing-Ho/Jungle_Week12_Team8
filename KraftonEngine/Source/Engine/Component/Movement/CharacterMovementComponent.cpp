#include "CharacterMovementComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Serialization/Archive.h"

#include <algorithm>
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

void UCharacterMovementComponent::SetMoveInput(float ForwardValue, float RightValue)
{
	// Character/Lua 쪽에서 이번 프레임에 사용할 이동 입력을 직접 지정합니다.
	MoveForwardInput = std::clamp(ForwardValue, -1.0f, 1.0f);
	MoveRightInput = std::clamp(RightValue, -1.0f, 1.0f);
}

void UCharacterMovementComponent::AddMoveInput(float ForwardValue, float RightValue)
{
	// 여러 입력 소스가 값을 더할 수 있으므로 누적 후 -1~1 범위로 제한합니다.
	MoveForwardInput = std::clamp(MoveForwardInput + ForwardValue, -1.0f, 1.0f);
	MoveRightInput = std::clamp(MoveRightInput + RightValue, -1.0f, 1.0f);
}

void UCharacterMovementComponent::ClearMoveInput()
{
	MoveForwardInput = 0.0f;
	MoveRightInput = 0.0f;
}

void UCharacterMovementComponent::SetLookInput(float DeltaX, float DeltaY)
{
	// Look 입력은 회전 단계에서 매 프레임 소비할 델타값입니다.
	LookInputX = DeltaX;
	LookInputY = DeltaY;
}

void UCharacterMovementComponent::AddLookInput(float DeltaX, float DeltaY)
{
	LookInputX += DeltaX;
	LookInputY += DeltaY;
}

void UCharacterMovementComponent::ClearLookInput()
{
	LookInputX = 0.0f;
	LookInputY = 0.0f;
}

void UCharacterMovementComponent::Jump()
{
	if (!IsMovingOnGround())
	{
		return;
	}

	// 실제 낙하 물리는 이후 단계에서 처리하고, 여기서는 점프 상태 전환만 준비합니다.
	Velocity.Z = JumpZVelocity;
	SetMovementMode(MOVE_Falling);
}

void UCharacterMovementComponent::StopMovementImmediately()
{
	Velocity = FVector::ZeroVector;
	Acceleration = FVector::ZeroVector;

	UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());
	if (UpdatedPrimitive)
	{
		UpdatedPrimitive->SetLinearVelocity(FVector::ZeroVector);
	}
}

void UCharacterMovementComponent::SetControllerDesiredYaw(float InYawDegrees)
{
	ControllerDesiredYawDegrees = InYawDegrees;
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
