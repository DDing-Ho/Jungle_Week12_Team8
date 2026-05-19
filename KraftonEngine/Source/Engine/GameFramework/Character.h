#pragma once
#include "GameFramework/Pawn.h"
#include "Component/CapsuleComponent.h"
#include "Component/SkeletalMeshComponent.h"
#include "Component/Movement/CharacterMovementComponent.h"
#include "Component/LuaScriptComponent.h"
#include "Component/SpringArmComponent.h"
#include "Character.generated.h"

class UCameraComponent;

UCLASS(Actor)
class ACharacter : public APawn
{
public:
	GENERATED_BODY(ACharacter)

	ACharacter();
	~ACharacter() override = default;

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	// TODO: 계속 duplicate 오류나서 임시로 사용
	void InitDefaultComponents();

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;
	

	// ── 이동 입력 (PlayerController가 호출) ──────────────────
	void AddMovementInput(FVector WorldDir, float Scale = 1.0f);
	void AddControllerYawInput(float Val);
	void AddControllerPitchInput(float Val);

	// ── 점프 ─────────────────────────────────────────────────
	virtual void Jump();
	virtual void StopJumping();
	bool CanJump() const;

	// ── 상태 조회 ─────────────────────────────────────────────
	bool IsJumping() const;
	bool IsFalling() const;
	bool IsOnGround() const;

	// ── 컴포넌트 접근자 ───────────────────────────────────────
	UCapsuleComponent*           GetCapsuleComponent()    const { return CapsuleComponent; }
	USkeletalMeshComponent*      GetMesh()                const { return Mesh; }
	UFUNCTION(Lua)
	UCharacterMovementComponent* GetCharacterMovement()   const { return CharacterMovement; }
	ULuaScriptComponent*         GetLuaScript()           const { return LuaScript; }

protected:
	void PossessedBy(APlayerController* PC) override;
	void UnPossessed() override;

	virtual void OnJumped() {}
	virtual void OnLanded(const FVector& HitNormal) {}

private:
	UCapsuleComponent*				CapsuleComponent  = nullptr;
	USkeletalMeshComponent*			Mesh              = nullptr;
	UCharacterMovementComponent*	CharacterMovement = nullptr;
	USpringArmComponent*			SpringArm         = nullptr;
	ULuaScriptComponent*			LuaScript         = nullptr;

	// 시연용 카메라
	// Character를 상속받은 객체에 CameraComponent를 만들어야함
	UCameraComponent*				Camera = nullptr;

	bool  bPressedJump    = false;
	float JumpKeyHoldTime = 0.0f;

	void SyncSpringArmRotationMode();
};
