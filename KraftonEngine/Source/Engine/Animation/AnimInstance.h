#pragma once
#include "Object/Object.h"
#include "Animation/AnimTypes.h"

class UAnimationStateMachine;
class USkeletalMeshComponent;

class UAnimInstance : public UObject
{
public:
	DECLARE_CLASS(UAnimInstance, UObject)
	virtual void Initialize(USkeletalMeshComponent* InOwner, const FString& InScriptPath = "");
	void Update(float DeltaTime);
	virtual void NativeUpdateAnimation(float DeltaSeconds) {}

	void TriggerAnimNotifies();

	// SkeletalMeshComponent가 호출 — 포즈 생성 및 Notify 수집
	virtual void GetCurrentPose(FPoseContext& OutPose);

	void SetStateMachine(UAnimationStateMachine* SM) { StateMachine = SM; }

	USkeletalMeshComponent* GetOwnerComponent() const { return OwnerComponent; }

	void  SetFloat(const FString& Key, float Val) { FloatParams[Key] = Val; }
	float GetFloat(const FString& Key, float Default = 0.f) const
	{
		auto It = FloatParams.find(Key);
		return It != FloatParams.end() ? It->second : Default;
	}
	void SetBool(const FString& Key, bool Val) { BoolParams[Key] = Val; }
	bool GetBool(const FString& Key, bool Default = false) const
	{
		auto It = BoolParams.find(Key);
		return It != BoolParams.end() ? It->second : Default;
	}
	void  SetInt(const FString& Key, int32 Val) { IntParams[Key] = Val; }
	int32 GetInt(const FString& Key, int32 Default = 0) const
	{
		auto It = IntParams.find(Key);
		return It != IntParams.end() ? It->second : Default;
	}

protected:
	USkeletalMeshComponent* OwnerComponent = nullptr;
	UAnimationStateMachine* StateMachine = nullptr;
	TArray<FAnimNotifyEvent> NotifyQueue;
	float                   LastEvaluatedTime = 0.0f;

	TMap<FString, float>  FloatParams;
	TMap<FString, bool>   BoolParams;
	TMap<FString, int32>  IntParams;

	void ResetNotifyState();
	void RouteNotify(const FAnimNotifyEvent& Notify);

private:
	struct FActiveNotifyState
	{
		FAnimNotifyEvent Notify;
	};

	TArray<FActiveNotifyState>  ActiveStateNotifies;
};