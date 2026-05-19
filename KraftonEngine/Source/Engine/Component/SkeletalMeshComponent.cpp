#include "SkeletalMeshComponent.h"
#include "Core/Log.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Mesh/SkeletalMesh.h"
#include "GameFramework/AActor.h"
#include <cctype>

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	AnimInstance = InInstance;
	if (AnimInstance)
		AnimInstance->Initialize(this);
}

void USkeletalMeshComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction& ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!AnimInstance)
		return;

	AnimInstance->Update(DeltaTime);

	FPoseContext Pose;
	AnimInstance->GetCurrentPose(Pose);
	AnimInstance->TriggerAnimNotifies();

	if (!Pose.BoneLocalTransforms.empty())
		ApplyPoseToComponent(Pose);
}

void USkeletalMeshComponent::ApplyPoseToComponent(const FPoseContext& Pose)
{
	if (!SkeletalMesh)
		return;

	EnsureBoneEditPose();

	const FSkeletonAsset* SkeletonAsset = SkeletalMesh->GetSkeletonAsset();

	for (uint32 BoneIdx = 0; BoneIdx < static_cast<uint32>(Pose.BoneLocalTransforms.size()); ++BoneIdx)
	{
		if (BoneIdx >= static_cast<uint32>(BoneEditLocalMatrices.size()))
			continue;

		FTransform BoneTM = Pose.BoneLocalTransforms[BoneIdx];

		if (bIgnoreRootMotion && BoneIdx == TargetRootBoneIndex)
		{
			BoneTM.Location = FVector::ZeroVector;
		}

		BoneEditLocalMatrices[BoneIdx] = BoneTM.ToMatrix();
	}

	bUseBoneEditPose = true;
	UpdateSkinMatrices();
	MarkWorldBoundsDirty();
}

void USkeletalMeshComponent::SolveTwoBoneIK(FPoseContext& Pose, int RootBoneIndex, int MidBoneIndex, int EndBoneIndex,
	const FVector& TargetPosition, const FVector& PolePosition)
{
	/**
	 * 1. Root Mid, End의 현재 Global 위치를 구함
	 * 2. Bone 길이 계산
	 * 3. Root -> Target 방향 계산
	 * 4. Pole  방향 기준으로 Joint Plane 계산
	 * 5. 새로운 Mid 위치 계산
	 * 6. Root Bone 회전 갱신
	 * 7. Mid Bone 회전 갱선
	 * 8. Local Transform으로 변환해서 Pose에 저장
	 */

	/** 
	 * 새로운 위치만 구하는게 아니라.
	 * 그 위치를 만들 수 있도록 Bone Rotation을 다시 계산해야함.
	 */
}

void USkeletalMeshComponent::HandleAnimNotify(const FAnimNotifyEvent& Notify)
{
	if (!Notify.NotifyTrigger)
		return;

	AActor* Owner = GetOwner();
	Notify.NotifyTrigger->OnNotify(Owner, this);
}
