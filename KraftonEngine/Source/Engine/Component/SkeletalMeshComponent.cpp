#include "SkeletalMeshComponent.h"
#include "Core/Log.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Mesh/SkeletalMesh.h"
#include "GameFramework/AActor.h"
#include <cctype>

IMPLEMENT_CLASS(USkeletalMeshComponent, USkinnedMeshComponent)

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

	// "root"로 시작하는 본의 translation을 잠근다 (IgnoreRootMotion)
	// Armature → root 계층처럼 임포트된 경우도 이름으로 정확히 찾는다
	auto IsRootMotionBone = [&](uint32 Idx) -> bool
	{
		if (!SkeletonAsset) return Idx == 0;
		const FString& Name = SkeletonAsset->Bones[Idx].Name;
		if (Name.empty()) return false;
		// "root", "Root", "ROOT" 등 대소문자 무관하게 처리
		FString Lower = Name;
		for (char& c : Lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return Lower == "root";
	};

	for (uint32 BoneIdx = 0; BoneIdx < static_cast<uint32>(Pose.BoneLocalTransforms.size()); ++BoneIdx)
	{
		if (BoneIdx >= static_cast<uint32>(BoneEditLocalMatrices.size()))
			continue;

		FTransform BoneTM = Pose.BoneLocalTransforms[BoneIdx];

		if (IsRootMotionBone(BoneIdx))
		{
			BoneTM.Location = FVector::ZeroVector;
		}

		BoneEditLocalMatrices[BoneIdx] = BoneTM.ToMatrix();
	}

	bUseBoneEditPose = true;
	UpdateCPUSkinning();
	++SkinnedRevision;
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
