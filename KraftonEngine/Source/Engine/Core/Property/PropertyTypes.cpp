#include "Core/Property/PropertyTypes.h"

#include <cstring>

#include "Core/Property/FArrayProperty.h"
#include "Core/Property/FEnumProperty.h"
#include "Core/Property/FStructProperty.h"
#include "Core/Property/FObjectPropertyBase/FSoftObjectProperty.h"
#include "Object/FName.h"
#include "SimpleJSON/json.hpp"

namespace
{
	json::JSON SerializeFloatArray(const float* Values, uint32 Count)
	{
		json::JSON Array = json::Array();
		for (uint32 Index = 0; Index < Count; ++Index)
		{
			Array.append(static_cast<double>(Values[Index]));
		}
		return Array;
	}

	void DeserializeFloatArray(float* Values, uint32 Count, const json::JSON& Value)
	{
		uint32 Index = 0;
		for (const auto& Element : Value.ArrayRange())
		{
			if (Index < Count)
			{
				Values[Index] = static_cast<float>(Element.ToFloat());
			}
			++Index;
		}
	}
}

json::JSON FBoolProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const bool*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FBoolProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<bool*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToBool();
}

json::JSON FByteBoolProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const uint8_t*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(static_cast<bool>(*Value != 0));
}

void FByteBoolProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<uint8_t*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToBool() ? 1 : 0;
}

json::JSON FIntProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const int32*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FIntProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<int32*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToInt();
}

json::JSON FFloatProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(static_cast<double>(*Value));
}

void FFloatProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	*Target = static_cast<float>(Value.ToFloat());
}

json::JSON FVec3Property::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return SerializeFloatArray(Value, 3);
}

void FVec3Property::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	DeserializeFloatArray(Target, 3, Value);
}

json::JSON FRotatorProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return SerializeFloatArray(Value, 3);
}

void FRotatorProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	DeserializeFloatArray(Target, 3, Value);
}

json::JSON FVec4Property::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return SerializeFloatArray(Value, 4);
}

void FVec4Property::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	DeserializeFloatArray(Target, 4, Value);
}

json::JSON FColor4Property::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const float*>(ContainerPtrToValuePtr(Instance));
	return SerializeFloatArray(Value, 4);
}

void FColor4Property::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<float*>(ContainerPtrToValuePtr(Instance));
	DeserializeFloatArray(Target, 4, Value);
}

json::JSON FStringProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const FString*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FStringProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<FString*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToString();
}

json::JSON FScriptProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const FString*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FScriptProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<FString*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToString();
}

json::JSON FSceneComponentRefProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const FString*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(*Value);
}

void FSceneComponentRefProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<FString*>(ContainerPtrToValuePtr(Instance));
	*Target = Value.ToString();
}

json::JSON FMaterialSlotProperty::Serialize(const void* Instance) const
{
	const auto* Slot = static_cast<const FMaterialSlot*>(ContainerPtrToValuePtr(Instance));
	json::JSON Object = json::Object();
	Object["Path"] = json::JSON(Slot->Path);
	return Object;
}

void FMaterialSlotProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Slot = static_cast<FMaterialSlot*>(ContainerPtrToValuePtr(Instance));
	if (Value.hasKey("Path"))
	{
		Slot->Path = Value.at("Path").ToString();
	}
}

json::JSON FNameProperty::Serialize(const void* Instance) const
{
	const auto* Value = static_cast<const FName*>(ContainerPtrToValuePtr(Instance));
	return json::JSON(Value->ToString());
}

void FNameProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	auto* Target = static_cast<FName*>(ContainerPtrToValuePtr(Instance));
	*Target = FName(Value.ToString());
}

json::JSON FEnumProperty::Serialize(const void* Instance) const
{
	const void* ValuePtr = ContainerPtrToValuePtr(Instance);
	int32 Value = 0;
	std::memcpy(&Value, ValuePtr, EnumSize);
	return json::JSON(Value);
}

void FEnumProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	void* Target = ContainerPtrToValuePtr(Instance);
	int32 StoredValue = Value.ToInt();
	std::memcpy(Target, &StoredValue, EnumSize);
}

json::JSON FArrayProperty::Serialize(const void* Instance) const
{
	const void* ArrayInstance = ContainerPtrToValuePtr(Instance);
	if (!Accessor || !Inner || !ArrayInstance)
	{
		return json::JSON();
	}

	json::JSON Array = json::Array();
	const uint32 Count = Accessor->Num(ArrayInstance);
	for (uint32 Index = 0; Index < Count; ++Index)
	{
		void* ElementInstance = Accessor->GetAt(const_cast<void*>(ArrayInstance), Index);
		Array.append(Inner->Serialize(ElementInstance));
	}
	return Array;
}

void FArrayProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	void* ArrayInstance = ContainerPtrToValuePtr(Instance);
	if (!Accessor || !Inner || !ArrayInstance)
	{
		return;
	}

	const bool bFixedSize = (PropertyFlag & CPF_FixedSize) != 0;
	if (!bFixedSize)
	{
		Accessor->Clear(ArrayInstance);
	}

	uint32 Index = 0;
	for (const auto& Element : Value.ArrayRange())
	{
		if (!bFixedSize)
		{
			Accessor->AddDefault(ArrayInstance);
		}
		else if (Index >= Accessor->Num(ArrayInstance))
		{
			break;
		}

		const uint32 TargetIndex = bFixedSize ? Index : Accessor->Num(ArrayInstance) - 1;
		void* ElementInstance = Accessor->GetAt(ArrayInstance, TargetIndex);
		Inner->Deserialize(ElementInstance, Element);
		++Index;
	}
}

json::JSON FStructProperty::Serialize(const void* Instance) const
{
	const void* StructInstance = ContainerPtrToValuePtr(Instance);
	if (!StructInstance)
	{
		return json::JSON();
	}

	const TArray<FProperty*>& StructProperties = GetStructProperties();
	if (StructProperties.empty())
	{
		return json::JSON();
	}

	json::JSON Object = json::Object();
	for (const FProperty* Child : StructProperties)
	{
		if (Child)
		{
			Object[Child->Name] = Child->Serialize(StructInstance);
		}
	}
	return Object;
}

void FStructProperty::Deserialize(void* Instance, const json::JSON& Value) const
{
	void* StructInstance = ContainerPtrToValuePtr(Instance);
	if (!StructInstance)
	{
		return;
	}

	const TArray<FProperty*>& StructProperties = GetStructProperties();
	for (const FProperty* Child : StructProperties)
	{
		if (!Child || !Value.hasKey(Child->Name.c_str()))
		{
			continue;
		}

		const json::JSON& ChildValue = Value.at(Child->Name.c_str());
		Child->Deserialize(StructInstance, ChildValue);
	}
}
