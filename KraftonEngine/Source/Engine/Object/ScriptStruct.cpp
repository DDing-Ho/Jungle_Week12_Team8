#include "ScriptStruct.h"
#include "UClass.h"

UClass UScriptStruct::StaticClassInstance(
	"UScriptStruct",
	UStruct::StaticClass(),
	sizeof(UScriptStruct),
	CF_None,
	CASTCLASS_UScriptStruct
);
FClassRegistrar UScriptStruct::s_Registrar(&UScriptStruct::StaticClassInstance);

UScriptStruct::~UScriptStruct() = default;

void UScriptStruct::RegisterScriptStructsAsStaticObjects()
{
	for (UScriptStruct* Struct : GetAllScriptStructs())
	{
		if (!Struct) continue;
		if (const char* Name = Struct->GetName())
		{
			Struct->SetFName(FName(Name));
		}
		GUObjectArray.AddStaticObject(Struct);
	}
}
