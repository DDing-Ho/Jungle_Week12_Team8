#pragma once
#include "PropertyTypes.h"

class FEnumProperty final : public FProperty
{
public:
	FEnumProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize,
		const char** InEnumNames,
		uint32 InEnumCount,
		uint32 InEnumSize = sizeof(int32))
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
		, EnumNames(InEnumNames)
		, EnumCount(InEnumCount)
		, EnumSize(InEnumSize)
	{
	}

	const char** EnumNames = nullptr;
	uint32 EnumCount = 0;
	uint32 EnumSize = sizeof(int32);

	EPropertyType GetType() const override { return EPropertyType::Enum; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
};