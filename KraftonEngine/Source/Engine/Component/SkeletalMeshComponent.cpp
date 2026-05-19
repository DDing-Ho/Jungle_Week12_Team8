#include "SkeletalMeshComponent.h"
#include "Animation/AnimationRuntime.h"
#include "Core/Log.h"
#include "Render/Proxy/SkeletalMeshSceneProxy.h"
#include "Mesh/SkeletalMesh.h"
#include "Mesh/SkeletonAsset.h"
#include "GameFramework/AActor.h"
#include "Serialization/Archive.h"
#include <cctype>

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();
}

FPrimitiveSceneProxy* USkeletalMeshComponent::CreateSceneProxy()
{
	return new FSkeletalMeshSceneProxy(this);
}

void USkeletalMeshComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar << bEnableTwoBoneIK;

	uint32 ChainCount = static_cast<uint32>(TwoBoneIKChains.size());
	Ar << ChainCount;

	if (Ar.IsLoading())
	{
		TwoBoneIKChains.resize(ChainCount);
	}

	for (FTwoBoneIKChain& Chain : TwoBoneIKChains)
	{
		Ar << Chain;
	}
}

void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	AnimInstance = InInstance;
	if (AnimInstance)
		AnimInstance->Initialize(this, "");
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
	{
		ApplyTwoBoneIKChains(Pose);
		ApplyPoseToComponent(Pose);
	}
}

void USkeletalMeshComponent::SetTwoBoneIKChains(const TArray<FTwoBoneIKChain>& InChains)
{
	TwoBoneIKChains = InChains;
}

void USkeletalMeshComponent::AddTwoBoneIKChain(const FTwoBoneIKChain& Chain)
{
	TwoBoneIKChains.push_back(Chain);
}

void USkeletalMeshComponent::ClearTwoBoneIKChains()
{
	TwoBoneIKChains.clear();
}

bool USkeletalMeshComponent::SetIKTargetPosition(int32 ChainIndex, const FVector& WorldPosition)
{
	if (ChainIndex < 0 || ChainIndex >= static_cast<int32>(TwoBoneIKChains.size()))
	{
		return false;
	}

	TwoBoneIKChains[ChainIndex].TargetPosition = GetWorldInverseMatrix().TransformPositionWithW(WorldPosition);
	return true;
}

int32 USkeletalMeshComponent::FindBoneIndexByName(const FString& BoneName) const
{
	if (!SkeletalMesh || BoneName.empty())
	{
		return -1;
	}

	const FSkeletonAsset* SkeletonAsset = SkeletalMesh->GetSkeletonAsset();
	if (!SkeletonAsset)
	{
		return -1;
	}

	for (int32 BoneIndex = 0; BoneIndex < static_cast<int32>(SkeletonAsset->Bones.size()); ++BoneIndex)
	{
		if (SkeletonAsset->Bones[BoneIndex].Name == BoneName)
		{
			return BoneIndex;
		}
	}

	return -1;
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

void USkeletalMeshComponent::ApplyTwoBoneIKChains(FPoseContext& Pose)
{
	if (!bEnableTwoBoneIK || TwoBoneIKChains.empty() || !SkeletalMesh)
	{
		return;
	}

	const FSkeletonAsset* SkeletonAsset = SkeletalMesh->GetSkeletonAsset();
	if (!SkeletonAsset)
	{
		return;
	}

	for (const FTwoBoneIKChain& Chain : TwoBoneIKChains)
	{
		if (!Chain.bEnabled)
		{
			continue;
		}

		FAnimationRuntime::SolveTwoBoneIK(Pose, SkeletonAsset, Chain);
	}
}

void USkeletalMeshComponent::SolveTwoBoneIK(FPoseContext& Pose, int RootBoneIndex, int MidBoneIndex, int EndBoneIndex,
	const FVector& TargetPosition, const FVector& PolePosition)
{
	if (!SkeletalMesh)
	{
		return;
	}

	FTwoBoneIKChain Chain;
	Chain.RootBoneIndex = RootBoneIndex;
	Chain.MidBoneIndex = MidBoneIndex;
	Chain.EndBoneIndex = EndBoneIndex;
	Chain.TargetPosition = TargetPosition;
	Chain.PolePosition = PolePosition;

	FAnimationRuntime::SolveTwoBoneIK(Pose, SkeletalMesh->GetSkeletonAsset(), Chain);
}

void USkeletalMeshComponent::HandleAnimNotify(const FAnimNotifyEvent& Notify)
{
	if (!Notify.NotifyTrigger)
		return;

	AActor* Owner = GetOwner();
	Notify.NotifyTrigger->OnNotify(Owner, this);
}
