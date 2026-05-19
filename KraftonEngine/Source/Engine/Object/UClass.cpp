#include "UClass.h"
#include "Serialization/Archive.h"

UClass UClass::StaticClassInstance(
	"UClass",
	UStruct::StaticClass(),
	sizeof(UClass),
	CF_None,
	CASTCLASS_UClass
);

FClassRegistrar UClass::s_Registrar(&UClass::StaticClassInstance);

void UClass::RegisterClassesAsStaticObjects()
{
	for (UClass* C : GetAllClasses())
	{
		if (!C) continue;
		if (const char* Name = C->GetName())
		{
			C->SetFName(FName(Name));
		}
		GUObjectArray.AddStaticObject(C);
	}
}

void UClass::Bind()
{
	if (bBound) return;

	ClassCastFlags = OwnClassCastFlags;
	if (UClass* Super = GetSuperClass())
	{
		Super->Bind();
		ClassCastFlags |= Super->ClassCastFlags;
	}
	bBound = true;
}

bool UClass::IsChildOf(const UClass* Other)
{
	if (Other == nullptr) return false;
	if (this == Other)    return true;

	// Fast path is valid only for classes that own a unique cast bit.
	const uint32 OtherOwnCast = Other->OwnClassCastFlags;
	if (OtherOwnCast != CASTCLASS_None)
	{
		Bind();
		return (ClassCastFlags & OtherOwnCast) == OtherOwnCast;
	}

	// SLOW PATH: walk the SuperStruct chain by pointer-equality.
	return UStruct::IsChildOf(Other);
}
