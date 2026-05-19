#include "Component/CineCameraComponent.h"

#include "Serialization/Archive.h"

void UCineCameraComponent::Serialize(FArchive& Ar)
{
	UCameraComponent::Serialize(Ar);
	Ar << Letterbox.bEnabled;
	Ar << Letterbox.Amount;
	Ar << Letterbox.Thickness;
	Ar << Letterbox.Color;
}

