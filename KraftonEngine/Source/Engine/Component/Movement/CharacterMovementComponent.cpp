#include "CharacterMovementComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SceneComponent.h"
#include "Math/Quat.h"
#include "Serialization/Archive.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	// 시연을 위해 임시로 추가한 Ground 조건
	// 반드시 시연 끝나고 제거해야함.
	constexpr float TemporaryFlatGroundZ = 4.0f;

	// float clamp 함수
	float ClampFloat(float Value, float MinValue, float MaxValue)
	{
		return std::max(MinValue, std::min(MaxValue, Value));
	}

	float ClampFloatRange(float Value, float A, float B)
	{
		return ClampFloat(Value, std::min(A, B), std::max(A, B));
	}

	// -180.0f ~ 180.0f 사이로 Degree 정규화
	float NormalizeAxisDegrees(float AngleDegrees)
	{
		AngleDegrees = std::fmod(AngleDegrees, 360.0f);
		if (AngleDegrees > 180.0f)
		{
			AngleDegrees -= 360.0f;
		}
		else if (AngleDegrees <= -180.0f)
		{
			AngleDegrees += 360.0f;
		}
		return AngleDegrees;
	}

	// DeltaTime동안 Degree 변화량얻는 함수.
	float FindDeltaAngleDegrees(float CurrentDegrees, float TargetDegrees)
	{
		return NormalizeAxisDegrees(TargetDegrees - CurrentDegrees);
	}

	// Yaw를 통해 방향값을 얻는 헬퍼 함수
	float DirectionToYawDegrees(const FVector& Direction)
	{
		// Z-up LH 기준: +X가 Yaw 0도, +Y가 Yaw 90도입니다.
		return std::atan2(Direction.Y, Direction.X) * RAD_TO_DEG;
	}

	FQuat MakeYawQuatDegrees(float YawDegrees)
	{
		return FQuat::FromAxisAngle(FVector::UpVector, YawDegrees * DEG_TO_RAD).GetNormalized();
	}

	// Component에 Rotation 값 저장
	// WorldQuat * ParentQuat.inv
	void SetWorldYaw(USceneComponent* Component, float WorldYawDegrees)
	{
		if (!Component)
		{
			return;
		}

		const FQuat WorldYawQuat = MakeYawQuatDegrees(WorldYawDegrees);
		if (USceneComponent* Parent = Component->GetParent())
		{
			const FQuat ParentWorldQuat = FQuat::FromMatrix(Parent->GetWorldMatrix());
			Component->SetRelativeRotation((WorldYawQuat * ParentWorldQuat.Inverse()).GetNormalized());
			return;
		}

		Component->SetRelativeRotation(WorldYawQuat);
	}
}

void UCharacterMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();
	UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());
	if (UpdatedPrimitive)
	{
		FVector InitialForward = UpdatedPrimitive->GetForwardVector();
		InitialForward.Z = 0.0f;
		if (!InitialForward.IsNearlyZero())
		{
			InitialForward.Normalize();
			ControllerDesiredYawDegrees = DirectionToYawDegrees(InitialForward);
		}
	}
}

void UCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());

	if (!UpdatedPrimitive || DeltaTime <= 0.0f)
	{
		ClearLookInput();
		return;
	}

	switch (MovementMode)
	{
	case MOVE_Walking:
		PhysWalking(DeltaTime);
		break;

	case MOVE_Falling:
		PhysFalling(DeltaTime);
		break;

	case MOVE_None:
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		ClearLookInput();
		return;

	default:
		ClearLookInput();
		return;
	}

	UpdateRotation(DeltaTime);
	ClearLookInput();

	// MoveInput은 여기서 Clear하지 않는다
	// 현재 입력 주입은 Lua/외부 컴포넌트 Tick 순서에 의존할 수 있기 때문
	// Lua 쪽에서 매 프레임 SetMoveInput(0.0f, 0.0f)을 호출
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
	Ar << MinControllerPitchDegrees;
	Ar << MaxControllerPitchDegrees;
}

void UCharacterMovementComponent::PostEditProperty(const char* PropertyName)
{
	if (!PropertyName)
	{
		UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());
		return;
	}

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
	// Character/Lua 쪽에서 이번 프레임에 사용할 이동 입력을 직접 지정
	MoveForwardInput = std::clamp(ForwardValue, -1.0f, 1.0f);
	MoveRightInput = std::clamp(RightValue, -1.0f, 1.0f);
}

void UCharacterMovementComponent::SetLookInput(float DeltaX, float DeltaY)
{
	// Look 입력은 회전 단계에서 매 프레임 소비할 델타값입니다.
	LookInputX = DeltaX;
	LookInputY = DeltaY;
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
}

const FVector& UCharacterMovementComponent::GetVelocity() const
{
	return Velocity;
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

float UCharacterMovementComponent::GetControllerDesiredPitch() const
{
	return ControllerDesiredPitchDegrees;
}

float UCharacterMovementComponent::GetSpeed2D() const
{
	return std::sqrt(Velocity.X * Velocity.X + Velocity.Y * Velocity.Y);
}

bool UCharacterMovementComponent::IsFalling() const
{
	return MovementMode == MOVE_Falling;
}

bool UCharacterMovementComponent::IsMovingOnGround() const
{
	return MovementMode == MOVE_Walking;
}

// 지금은 World Offset을 주는 방식으로 이동
// 기존에 작성한 LinearVelocity는 PhsyX기반이라 제거
void UCharacterMovementComponent::PhysWalking(float DeltaTime)
{
	UpdateVelocityWalking(DeltaTime);
	Velocity.Z = 0.0f;
	MoveUpdatedComponentKinematic(DeltaTime);
	ApplyTemporaryFlatGroundConstraint();
}

void UCharacterMovementComponent::PhysFalling(float DeltaTime)
{
	UpdateVelocityFalling(DeltaTime);
	MoveUpdatedComponentKinematic(DeltaTime);
	ApplyTemporaryFlatGroundConstraint();
}

void UCharacterMovementComponent::UpdateVelocityWalking(float DeltaTime)
{
	const FVector MoveDirection = GetCurrentMoveDirection();

	if (!MoveDirection.IsNearlyZero())
	{
		Acceleration = MoveDirection * MaxAcceleration;
		Velocity += Acceleration * DeltaTime;
		Velocity.Z = 0.0f;
		LimitVelocity2D(MaxWalkSpeed);
		return;
	}

	Acceleration = FVector::ZeroVector;
	Velocity.Z = 0.0f;

	const float Speed2D = GetSpeed2D();
	if (Speed2D <= 0.0f)
	{
		Velocity.X = 0.0f;
		Velocity.Y = 0.0f;
		return;
	}

	// 입력이 없을 때는 마찰과 제동 감속을 함께 써서 수평 속도를 줄인다.
	const float BrakingAmount = (BrakingDecelerationWalking + Speed2D * GroundFriction) * DeltaTime;
	const float NewSpeed = std::max(0.0f, Speed2D - BrakingAmount);
	const float SpeedScale = NewSpeed / Speed2D;
	Velocity.X *= SpeedScale;
	Velocity.Y *= SpeedScale;
}

void UCharacterMovementComponent::UpdateVelocityFalling(float DeltaTime)
{
	const FVector MoveDirection = GetCurrentMoveDirection();

	if (!MoveDirection.IsNearlyZero())
	{
		Acceleration = MoveDirection * (MaxAcceleration * AirControl);
		Velocity.X += Acceleration.X * DeltaTime;
		Velocity.Y += Acceleration.Y * DeltaTime;
	}
	else
	{
		Acceleration = FVector::ZeroVector;
	}

	Velocity.Z += GravityZ * DeltaTime;
	LimitVelocity2D(MaxWalkSpeed);
}

void UCharacterMovementComponent::UpdateRotation(float DeltaTime)
{
	if (!UpdatedPrimitive)
	{
		return;
	}

	bool bHasTargetYaw = false;
	float TargetYawDegrees = 0.0f;

	// 진짜 PlayerController::ControlRotation이 아직 없으므로, Look 입력은 회전 모드와 무관하게
	// CMC의 ControllerDesiredYaw/Pitch에 누적해 카메라 회전으로 사용한다.
	ControllerDesiredYawDegrees = NormalizeAxisDegrees(ControllerDesiredYawDegrees + LookInputX * MouseSensitivity);
	ControllerDesiredPitchDegrees = ClampFloatRange(
		ControllerDesiredPitchDegrees + LookInputY * MouseSensitivity,
		MinControllerPitchDegrees,
		MaxControllerPitchDegrees);

	if (bOrientRotationToMovement)
	{
		FVector VelocityDirection(Velocity.X, Velocity.Y, 0.0f);
		if (!VelocityDirection.IsNearlyZero())
		{
			VelocityDirection.Normalize();
			TargetYawDegrees = DirectionToYawDegrees(VelocityDirection);
			bHasTargetYaw = true;
		}
	}
	else if (bUseControllerDesiredRotation)
	{
		TargetYawDegrees = ControllerDesiredYawDegrees;
		bHasTargetYaw = true;
	}

	if (!bHasTargetYaw)
	{
		return;
	}

	FVector CurrentForward = UpdatedPrimitive->GetForwardVector();
	CurrentForward.Z = 0.0f;
	if (CurrentForward.IsNearlyZero())
	{
		CurrentForward = FVector::ForwardVector;
	}
	else
	{
		CurrentForward.Normalize();
	}

	const float CurrentYawDegrees = DirectionToYawDegrees(CurrentForward);
	float NewYawDegrees = TargetYawDegrees;

	if (RotationRateYaw >= 0.0f)
	{
		const float DeltaYawDegrees = FindDeltaAngleDegrees(CurrentYawDegrees, TargetYawDegrees);
		const float MaxYawStep = RotationRateYaw * DeltaTime;
		const float AppliedYawStep = ClampFloat(DeltaYawDegrees, -MaxYawStep, MaxYawStep);
		NewYawDegrees = CurrentYawDegrees + AppliedYawStep;
	}

	SetWorldYaw(UpdatedPrimitive, NormalizeAxisDegrees(NewYawDegrees));
}

FVector UCharacterMovementComponent::GetCurrentMoveDirection() const
{
	const float ForwardInput = std::clamp(MoveForwardInput, -1.0f, 1.0f);
	const float RightInput = std::clamp(MoveRightInput, -1.0f, 1.0f);

	FVector Forward = FVector::ForwardVector;
	FVector Right = FVector::RightVector;

	// 이동 입력은 항상 카메라/컨트롤 yaw 기준이다.
	// 현재 캡슐 회전을 다시 읽으면 OrientRotationToMovement에서 입력-회전 피드백이 생긴다.
	const FQuat ControlYawQuat = MakeYawQuatDegrees(ControllerDesiredYawDegrees);
	Forward = ControlYawQuat.RotateVector(FVector::ForwardVector);
	Right = ControlYawQuat.RotateVector(FVector::RightVector);

	FVector Direction = Forward * ForwardInput + Right * RightInput;
	Direction.Z = 0.0f;

	const float DirectionLength = Direction.Length();
	if (DirectionLength > 1.0f)
	{
		Direction.Normalize();
	}
	else if (DirectionLength <= 0.0f)
	{
		Direction = FVector::ZeroVector;
	}

	return Direction;
}

bool UCharacterMovementComponent::ApplyTemporaryFlatGroundConstraint()
{
	if (!UpdatedPrimitive)
	{
		return false;
	}

	FVector Location = UpdatedPrimitive->GetWorldLocation();

	if (Location.Z > TemporaryFlatGroundZ)
	{
		return false;
	}
	
	// 시연을 위한 임시 바닥 판정 코드
	// TODO: 시연 끝나고 제거해야함.
	if (Location.Z < TemporaryFlatGroundZ)
	{
		Location.Z = TemporaryFlatGroundZ;
		UpdatedPrimitive->SetWorldLocation(Location);
	}
	Velocity.Z = 0.0f;

	if (MovementMode == MOVE_Falling)
	{
		SetMovementMode(MOVE_Walking);
	}

	return true;
}

void UCharacterMovementComponent::LimitVelocity2D(float MaxSpeed)
{
	const float ClampedMaxSpeed = std::max(0.0f, MaxSpeed);
	const float Speed2D = GetSpeed2D();
	if (Speed2D <= ClampedMaxSpeed)
	{
		return;
	}

	if (ClampedMaxSpeed <= 0.0f)
	{
		Velocity.X = 0.0f;
		Velocity.Y = 0.0f;
		return;
	}

	const float SpeedScale = ClampedMaxSpeed / Speed2D;
	Velocity.X *= SpeedScale;
	Velocity.Y *= SpeedScale;
}

void UCharacterMovementComponent::MoveUpdatedComponentKinematic(float DeltaTime)
{
	if (!UpdatedPrimitive || DeltaTime <= 0.0f)
	{
		return;
	}

	const FVector Delta = Velocity * DeltaTime;
	if (Delta.IsNearlyZero())
	{
		return;
	}

	UpdatedPrimitive->AddWorldOffset(Delta);
}
