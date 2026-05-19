#include "CharacterMovementComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Serialization/Archive.h"

void UCharacterMovementComponent::BeginPlay()
{
	UMovementComponent::BeginPlay();
	UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());
}

void UCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction)
{
	UMovementComponent::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());
}

void UCharacterMovementComponent::Serialize(FArchive& Ar)
{
	UMovementComponent::Serialize(Ar);
}

void UCharacterMovementComponent::PostEditProperty(const char* PropertyName)
{
	UMovementComponent::PostEditProperty(PropertyName);
	UpdatedPrimitive = Cast<UPrimitiveComponent>(GetUpdatedComponent());
}
