#pragma once
#include "PropertyTypes.h"

class FStructProperty final : public FProperty
{
public:
	FStructProperty(
		const FString& InName,
		const FString& InCategory,
		uint32 InPropertyFlag,
		uint32 InOffset,
		uint32 InElementSize,
		FStructPropertySchemaFn InSchemaFn)
		: FProperty(InName, InCategory, InPropertyFlag, InOffset, InElementSize)
		, SchemaFn(InSchemaFn)
	{
	}

	FStructPropertySchemaFn SchemaFn = nullptr;

	EPropertyType GetType() const override { return EPropertyType::Struct; }
	json::JSON Serialize(const void* Instance) const override;
	void Deserialize(void* Instance, const json::JSON& Value) const override;
};

