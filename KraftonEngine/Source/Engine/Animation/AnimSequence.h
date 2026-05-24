#pragma once

#include "Animation/AnimSequenceBase.h"
#include "AnimSequence.generated.h"

class UAnimDataModel;

/** 실제 단일 Animation Clip입니다. */
UCLASS()
class UAnimSequence : public UAnimSequenceBase
{
public:
	GENERATED_BODY(UAnimSequence)

	UAnimSequence() = default;
	~UAnimSequence() override = default;

	void Serialize(FArchive& Ar) override;

	void SetDataModel(UAnimDataModel* InDataModel);
	UAnimDataModel* GetDataModel() const { return DataModel; }

	int32 GetNumberOfSampledKeys() const override;
	float GetSamplingFrameRate() const override;
	bool EvaluatePose(float Time, FPoseContext& OutPose, bool bLoopOverride = true) const override;

	void CollectNotifies(float PrevTime, float CurrentTime, bool bLooping,
		bool bReverse, TArray<FAnimNotifyEvent>& OutNotifies);
	const TArray<FAnimNotifyEvent>& GetNotifyEvents() const;

private:
	UAnimDataModel* DataModel = nullptr;
};
