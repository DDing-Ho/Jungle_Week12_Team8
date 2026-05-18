#include "Notify_PlaySound.h"
#include "Core/Log.h"
#include "Object/ObjectFactory.h"
#include "Audio/AudioManager.h"
#include "Core/PropertyTypes.h"
#include "NotifyRegistry.h"

IMPLEMENT_CLASS(UNotify_PlaySound, UNotify)

// --- PlaySoundNotify 프로퍼티 등록 ---
BEGIN_CLASS_PROPERTIES(UNotify_PlaySound)
PROPERTY_STRING(AudioKey, "AudioKe", "Sound", CPF_Edit)
PROPERTY_FLOAT(Volume, "Volume", "Sound", 0.0f, 2.0f, 0.01f, CPF_Edit)
PROPERTY_BOOL(bLoop, "Loop", "Sound", CPF_Edit)
END_CLASS_PROPERTIES(UNotify_PlaySound)

void UNotify_PlaySound::OnNotify(AActor* MeshOwner, USkeletalMeshComponent* MeshComp)
{
	if (AudioKey.empty())
		return;

	UE_LOG("Notify_PlaySound");

	FAudioManager::Get().PlayAudio(AudioKey, Volume);
}

REGISTER_NOTIFY(UNotify_PlaySound)
