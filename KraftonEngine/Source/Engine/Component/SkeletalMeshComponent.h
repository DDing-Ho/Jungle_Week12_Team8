#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"

// SkeletalMesh 전용 render proxy만 제공하는 얇은 wrapper.
// Skinning/bone/material/bounds 상태는 모두 USkinnedMeshComponent가 소유한다.
class USkeletalMeshComponent : public USkinnedMeshComponent
{
public:
	DECLARE_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)
	USkeletalMeshComponent() = default;
	~USkeletalMeshComponent() override = default;

	// Render access 섹션: SceneProxy
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	// AnimationInstance 관리
	void SetAnimInstance(UAnimInstance* InInstance);
	UAnimInstance* GetAnimInstance() const { return AnimInstance; }

	// Notify 실행을 위해서 AnimInstance에서 여기로 전달하고 GetOwner로 전달
	void HandleAnimNotify(const FAnimNotifyEvent& Notify);

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction& ThisTickFunction) override;

private:
	// AnimationInstance의 포즈 BoneEditLocalMatrices에 복사
	void ApplyPoseToComponent(const FPoseContext& Pose);

	void SolveTwoBoneIK(FPoseContext& Pose, int RootBoneIndex, int MidBoneIndex, int EndBoneIndex, const FVector& TargetPosition, const FVector& PolePosition);

	UAnimInstance* AnimInstance = nullptr;
};
