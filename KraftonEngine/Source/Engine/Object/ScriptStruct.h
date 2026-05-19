#pragma once
#include "UStruct.h"

class UScriptStruct : public UStruct
{
public:
	UScriptStruct(FString InName, UScriptStruct* InSuperStruct, size_t InSize);


};