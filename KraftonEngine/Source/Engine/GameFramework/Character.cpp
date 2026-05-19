#include "Character.h"
#include "GameFramework/PlayerController.h"
#include "Serialization/Archive.h"
#include <Component/CameraComponent.h>

ACharacter::ACharacter()
{
	
}

void ACharacter::BeginPlay()
{
	SyncSpringArmRotationMode();

	APawn::BeginPlay();
}

void ACharacter::Tick(float DeltaTime)
{
	APawn::Tick(DeltaTime);
	SyncSpringArmRotationMode();
}

void ACharacter::InitDefaultComponents()
{
	CapsuleComponent = AddComponent<UCapsuleComponent>();
	SetRootComponent(CapsuleComponent);

	Mesh = AddComponent<USkeletalMeshComponent>();
	GetRootComponent()->AddChild(Mesh);

	SpringArm = AddComponent<USpringArmComponent>();
	GetRootComponent()->AddChild(SpringArm);
	SpringArm->TargetArmLength = 10;
	SpringArm->TargetOffset = FVector(0.f, 0.f, 10.f);

	Camera = AddComponent<UCameraComponent>();
	SpringArm->AddChild(Camera);
	Camera->SetRelativeRotation(FVector(0.f, 45.f, 0.f));

	CharacterMovement = AddComponent<UCharacterMovementComponent>();
	CharacterMovement->SetUpdatedComponent(GetRootComponent());
	SyncSpringArmRotationMode();

	LuaScript = AddComponent<ULuaScriptComponent>();
	LuaScript->SetScriptFile("PlayerController.lua");
}

void ACharacter::Serialize(FArchive& Ar)
{
	APawn::Serialize(Ar);
}


void ACharacter::PostDuplicate()
{
	CapsuleComponent = Cast<UCapsuleComponent>(GetRootComponent());
	if (!CapsuleComponent)
	{
		CapsuleComponent = GetComponentByClass<UCapsuleComponent>();
	}

	Mesh = GetComponentByClass<USkeletalMeshComponent>();
	SpringArm = GetComponentByClass<USpringArmComponent>();
	Camera = GetComponentByClass<UCameraComponent>();
	CharacterMovement = GetComponentByClass<UCharacterMovementComponent>();
	LuaScript = GetComponentByClass<ULuaScriptComponent>();

	if (CharacterMovement && CapsuleComponent)
	{
		CharacterMovement->SetUpdatedComponent(CapsuleComponent);
	}

	SyncSpringArmRotationMode();

	if (LuaScript && LuaScript->GetScriptFile().empty())
	{
		LuaScript->SetScriptFile("PlayerController.lua");
	}
}

void ACharacter::PossessedBy(APlayerController* PC)
{
	APawn::PossessedBy(PC);
}

void ACharacter::UnPossessed()
{
	APawn::UnPossessed();
}

// ── 이동 입력 ─────────────────────────────────────────────────

void ACharacter::AddMovementInput(FVector WorldDir, float Scale)
{
	if (!CharacterMovement)
	{
		return;
	}
	CharacterMovement->AddMoveInput(WorldDir.X * Scale, WorldDir.Y * Scale);
}

void ACharacter::AddControllerYawInput(float Val)
{
	if (!CharacterMovement)
	{
		return;
	}
	CharacterMovement->AddLookInput(Val, 0.0f);
}

void ACharacter::AddControllerPitchInput(float Val)
{
	if (!CharacterMovement)
	{
		return;
	}
	CharacterMovement->AddLookInput(0.0f, Val);
}

// ── 점프 ─────────────────────────────────────────────────────

void ACharacter::Jump()
{
	if (!CanJump())
	{
		return;
	}
	bPressedJump = true;
	CharacterMovement->Jump();
	OnJumped();
}

void ACharacter::StopJumping()
{
	bPressedJump = false;
	JumpKeyHoldTime = 0.0f;
}

bool ACharacter::CanJump() const
{
	return CharacterMovement && CharacterMovement->IsMovingOnGround();
}

// ── 상태 조회 ─────────────────────────────────────────────────

bool ACharacter::IsJumping() const
{
	return bPressedJump && IsFalling();
}

bool ACharacter::IsFalling() const
{
	return CharacterMovement && CharacterMovement->IsFalling();
}

bool ACharacter::IsOnGround() const
{
	return CharacterMovement && CharacterMovement->IsMovingOnGround();
}

void ACharacter::SyncSpringArmRotationMode()
{
	if (!SpringArm || !CharacterMovement)
	{
		return;
	}

	if (CharacterMovement->ShouldUseControllerDesiredRotation())
	{
		// UseControllerDesiredRotation: 캐릭터와 카메라가 같은 yaw를 따라간다.
		SpringArm->bInheritParentRotation = true;
		return;
	}

	// OrientRotationToMovement 또는 회전 비활성 모드: 카메라는 캐릭터 회전과 분리한다.
	// 둘 다 꺼져도 캐릭터만 고정될 뿐, 마우스 look으로 카메라는 회전해야 한다.
	SpringArm->bInheritParentRotation = false;
	SpringArm->SetFixedWorldYaw(CharacterMovement->GetControllerDesiredYaw());
}
