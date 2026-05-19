#pragma once
#include "Core/EngineTypes.h"
#include "MovementComponent.h"
#include "CharacterMovementComponent.generated.h"

class UPrimitiveComponent;

// TODO: Enum Table을 만들려면 enum class로 선언
// 근데 MovementMode는 여기저기서 사용 가능해야함 -> 전역 enum이어야 한다
// 따라서 MovementMode는 그냥 enum으로 유지하고 별도의 inline 보조 테이블 생성
// 추후 일반 enum도 UENUM이 가능해진다면 그때 이걸 삭제
inline const char* GCharacterMovementModeNames[] = {
	"MOVE_None",
	"MOVE_Walking",
	"MOVE_Falling",
	"MOVE_Max"
};

/*
 * ACharacter가 사용할 경량 CharacterMovement API의 기반 컴포넌트
 * ACharacter는 이 컴포넌트를 소유하고, 
 * 생성/초기화 시 SetUpdatedComponent(CapsuleComponent 또는 RootComponent)를 호출
 * CharacterMovementComponent의 UpdatedComponent는 Mesh가 아니라 Capsule 또는 Root Primitive
 * SkeletalMesh는 시각 표현이고, 실제 캐릭터 이동 기준은 Capsule/Primitive
 */
UCLASS()
class UCharacterMovementComponent : public UMovementComponent
{
public:
	GENERATED_BODY(UCharacterMovementComponent)

	UCharacterMovementComponent() = default;
	~UCharacterMovementComponent() override = default;

	void			BeginPlay() override;
	void			TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
	void			Serialize(FArchive& Ar) override;
	void			PostEditProperty(const char* PropertyName) override;

	UFUNCTION(Lua)
	void			SetMoveInput(float ForwardValue, float RightValue);
	UFUNCTION(Lua)
	void			AddMoveInput(float ForwardValue, float RightValue);
	UFUNCTION(Lua)
	void			ClearMoveInput();
	UFUNCTION(Lua)
	void			SetLookInput(float DeltaX, float DeltaY);
	UFUNCTION(Lua)
	void			AddLookInput(float DeltaX, float DeltaY);
	UFUNCTION(Lua)
	void			ClearLookInput();
	UFUNCTION(Lua)
	void			Jump();
	UFUNCTION(Lua)
	void			StopMovementImmediately();
	UFUNCTION(Lua)
	void			SetControllerDesiredYaw(float InYawDegrees);

	const FVector&	GetVelocity() const;
	void			SetVelocity(const FVector& InVelocity);
	EMovementMode	GetMovementMode() const;
	void			SetMovementMode(EMovementMode NewMovementMode);
	float			GetControllerDesiredYaw() const;
	UFUNCTION(Lua)
	float			GetSpeed2D() const;
	UFUNCTION(Lua)
	bool			IsWalking() const;
	UFUNCTION(Lua)
	bool			IsFalling() const;
	UFUNCTION(Lua)
	bool			IsMovingOnGround() const;

private:
	void			PhysWalking(float DeltaTime);
	void			PhysFalling(float DeltaTime);
	void			UpdateVelocityWalking(float DeltaTime);
	void			UpdateVelocityFalling(float DeltaTime);
	void			UpdateRotation(float DeltaTime);
	FVector			GetCurrentMoveDirection() const;
	FVector			GetPlanarForward() const;
	FVector			GetPlanarRight() const;
	void			LimitVelocity2D(float MaxSpeed);

	UPrimitiveComponent* UpdatedPrimitive = nullptr;

	// 런타임 이동 상태. 저장/에디터 노출 대상이 아니므로 UPROPERTY로 만들지 않음.
	FVector Velocity = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;

	// 외부 Character/Lua/Controller가 이후 단계에서 주입할 입력 누적값.
	float MoveForwardInput = 0.0f;
	float MoveRightInput = 0.0f;
	float LookInputX = 0.0f;
	float LookInputY = 0.0f;

	// 컨트롤러 방향 회전 모드에서 사용할 목표 월드 Yaw.
	float ControllerDesiredYawDegrees = 0.0f;

	// 현재 캐릭터 이동 상태.
	UPROPERTY(Edit, Category="Character Movement", DisplayName="Movement Mode", Type=Enum, EnumNames=GCharacterMovementModeNames, EnumCount=4, EnumSize=sizeof(EMovementMode))
	EMovementMode MovementMode = MOVE_Walking;

	// 지상 이동 시 도달할 수 있는 최대 2D 속도.
	UPROPERTY(Edit, Category="Character Movement|Walking", DisplayName="Max Walk Speed", Min=0.0f, Max=5000.0f, Speed=10.0f)
	float MaxWalkSpeed = 600.0f;

	// 입력이 들어왔을 때 속도를 늘리는 가속도.
	UPROPERTY(Edit, Category="Character Movement|Walking", DisplayName="Max Acceleration", Min=0.0f, Max=10000.0f, Speed=10.0f)
	float MaxAcceleration = 2048.0f;

	// 이동 입력이 없을 때 지상에서 적용할 감속도.
	UPROPERTY(Edit, Category="Character Movement|Walking", DisplayName="Braking Deceleration Walking", Min=0.0f, Max=10000.0f, Speed=10.0f)
	float BrakingDecelerationWalking = 2048.0f;

	// 지상 감속에 사용할 마찰 계수.
	UPROPERTY(Edit, Category="Character Movement|Walking", DisplayName="Ground Friction", Min=0.0f, Max=64.0f, Speed=0.1f)
	float GroundFriction = 8.0f;

	// 낙하 중 Z축에 적용할 중력 가속도. Z-up 기준이므로 기본값은 음수
	UPROPERTY(Edit, Category="Character Movement|Falling", DisplayName="Gravity Z", Min=-5000.0f, Max=0.0f, Speed=10.0f)
	float GravityZ = -980.0f;

	// 점프 시작 시 Z축 속도에 넣을 값
	UPROPERTY(Edit, Category="Character Movement|Jumping", DisplayName="Jump Z Velocity", Min=0.0f, Max=5000.0f, Speed=10.0f)
	float JumpZVelocity = 420.0f;

	// 낙하 중 수평 입력이 속도에 반영되는 비율
	UPROPERTY(Edit, Category="Character Movement|Falling", DisplayName="Air Control", Min=0.0f, Max=1.0f, Speed=0.01f)
	float AirControl = 0.2f;

	// true면 이동 방향을 바라보도록 회전
	UPROPERTY(Edit, Category="Character Movement|Rotation", DisplayName="Orient Rotation To Movement")
	bool bOrientRotationToMovement = true;

	// true면 컨트롤러가 지정한 Yaw를 바라보도록 회전
	UPROPERTY(Edit, Category="Character Movement|Rotation", DisplayName="Use Controller Desired Rotation")
	bool bUseControllerDesiredRotation = false;

	// 초당 회전 가능한 Yaw 각도. 음수면 즉시 회전
	UPROPERTY(Edit, Category="Character Movement|Rotation", DisplayName="Rotation Rate Yaw", Min=-1.0f, Max=3600.0f, Speed=10.0f)
	float RotationRateYaw = 720.0f;

	// Look 입력에 곱할 마우스 감도
	UPROPERTY(Edit, Category="Character Movement|Input", DisplayName="Mouse Sensitivity", Min=0.0f, Max=10.0f, Speed=0.01f)
	float MouseSensitivity = 0.1f;
};

