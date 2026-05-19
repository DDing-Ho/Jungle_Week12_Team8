# 특수 프로퍼티 타입 처리 상세

[`PropertyReflectionSystem.md`](PropertyReflectionSystem.md) 가 리플렉션 시스템의 큰 그림을 다뤘다면, 본 문서는 **기본 값 타입(`bool`, `float`, `FVector` 등)을 넘어서는 "특수" 프로퍼티 타입들이 실제로 어떻게 처리되는지**를 더 깊게 들여다본다.

다루는 타입:

1. **`FEnumProperty`** — `enum class` 멤버
2. **`FStructProperty`** — `USTRUCT()` 로 마킹된 구조체 멤버
3. **`FArrayProperty`** — `TArray<T>` 멤버
4. **`FSceneComponentRefProperty`** — `USceneComponent*` (문자열 경로 기반 특수 케이스)
5. **`FSoftObjectProperty`** — 자산에 대한 약/지연 참조
6. **`FObjectProperty` / `FClassProperty`** — 하드 `UObject*` / `UClass*` 참조 *(현재 빈 스텁; 의도된 동작을 §6 에서 설계 수준까지 기술)*

각 항목마다 **저장 표현 → codegen 산출물 → 직렬화 → 에디터 UI → 한계와 주의점** 순으로 정리한다.

---

## 0. 공통 모델 한 번 더 짚기

모든 프로퍼티 디스크립터는 `FProperty` 의 서브클래스이며 **순수 스키마**이다. 값은 객체 메모리 안에 살고, 디스크립터는 `Offset_Internal` 로 그 주소만 계산한다:

```cpp
void* ContainerPtrToValuePtr(void* Container) const {
    return static_cast<char*>(Container) + Offset_Internal;
}
```

각 디스크립터의 가상 메서드는 둘이다:

```cpp
virtual json::JSON Serialize(const void* Instance) const;
virtual void       Deserialize(void* Instance, const json::JSON& Value) const;
```

특수 타입들이 흥미로운 이유는, 이 두 메서드 하나만으로는 부족해서 **추가 메타데이터(이름 테이블, 자식 스키마, 컨테이너 액세서, 타입 제약 클래스 등)** 를 디스크립터에 들고 있어야 하기 때문이다.

---

## 1. `FEnumProperty` — `enum class` 멤버

### 1-1. 저장 표현
저자 코드는 그냥 평범한 `enum class` 이다.

```cpp
// InterpToMovementComponent.h
UENUM()
enum class EInterpBehaviour {
    OneShot,
    OneShotReverse,
    Loop,
    PingPong
};

class UInterpToMovementComponent : public UMovementComponent
{
    UPROPERTY(Edit, Category="Movement", DisplayName="Interpolation Behaviour")
    EInterpBehaviour InterpBehaviour = EInterpBehaviour::OneShot;
};
```

저장은 enum 의 underlying type 크기 그대로(보통 4바이트). 디스크립터는 그 사이즈만 알면 된다.

### 1-2. 디스크립터
[`FEnumProperty`](../KraftonEngine/Source/Engine/Core/Property/FEnumProperty.h) 는 다음 세 가지 추가 메타데이터를 보유한다.

```cpp
class FEnumProperty final : public FProperty {
public:
    const char** EnumNames = nullptr;      // 이름 문자열 배열 (인덱스 → 표시 문자열)
    uint32       EnumCount = 0;            // 항목 개수
    uint32       EnumSize  = sizeof(int32);// 실제 메모리 폭 (1/2/4/8)
};
```

`EnumSize` 가 별도로 있는 이유는 `enum class X : uint8` 처럼 **underlying type 가변**을 지원하기 위함이다. 이 폭을 모르면 `memcpy` 가 어긋난다.

### 1-3. Codegen 산출물
[`Scripts/GenerateCode.py`](../Scripts/GenerateCode.py) 는 `UENUM()` 을 발견하면 **`.generated.h` 에 이름 테이블**을 emit 한다.

```cpp
// InterpToMovementComponent.generated.h
inline const char* GInterpBehaviourNames[] = {
    "OneShot",
    "OneShotReverse",
    "Loop",
    "PingPong"
};
```

- `inline` 변수(C++17)이므로 여러 TU에서 include해도 ODR 안전하게 병합된다.
- 이름 규칙: `enum_names_symbol("EInterpBehaviour")` → 앞의 `E` 한 글자를 떼고 `G<Stem>Names`.
- 이 헤더는 enum 을 사용하는 다른 헤더에 자연스럽게 include 체인을 따라 들어가므로, **다른 헤더의 `UPROPERTY` 에서도 이 enum 을 그대로 참조 가능**하다.

`.gen.cpp` 의 등록 코드는 짧다:

```cpp
Cls->AddProperty(new FEnumProperty(
    "Interpolation Behaviour", "Movement", CPF_Edit,
    offsetof(ThisClass, InterpBehaviour),
    sizeof(((ThisClass*)0)->InterpBehaviour),
    GInterpBehaviourNames, 4, sizeof(EInterpBehaviour)));
```

수동 enum(=헤더 툴이 안 만든 외부 enum)을 쓰려면 `UPROPERTY(EnumNames=GMyNames, EnumCount=3, EnumSize=sizeof(int32))` 처럼 명시 메타데이터를 줘야 한다([`GenerateCode.py:802-814`](../Scripts/GenerateCode.py#L802-L814)).

### 1-4. 직렬화
값을 항상 `int32` 로 승격시켜 저장하므로 폭에 관계없이 JSON 호환된다.

```cpp
// PropertyTypes.cpp
json::JSON FEnumProperty::Serialize(const void* Instance) const {
    int32 Value = 0;
    std::memcpy(&Value, ContainerPtrToValuePtr(Instance), EnumSize);
    return json::JSON(Value);
}
void FEnumProperty::Deserialize(void* Instance, const json::JSON& Value) const {
    int32 Stored = Value.ToInt();
    std::memcpy(ContainerPtrToValuePtr(Instance), &Stored, EnumSize);
}
```

⚠️ **이름이 아니라 정수 인덱스로 저장**한다. 항목 순서가 바뀌면 기존 씬 파일이 깨진다. 이름 기반 저장(예: `"OneShot"`)으로 가는 것이 후일 마이그레이션 대상.

### 1-5. 에디터 UI
[`EditorPropertyWidget.cpp:1564`](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp#L1564) 에서 그냥 `ImGui::BeginCombo` 로 이름 테이블을 그대로 보여준다.

```cpp
const FEnumProperty& EnumProp = static_cast<const FEnumProperty&>(Prop);
int32 Val = 0;
memcpy(&Val, ValuePtr, EnumProp.EnumSize);
const char* Preview = ((uint32)Val < EnumProp.EnumCount) ? EnumProp.EnumNames[Val] : "Unknown";
// → 콤보 박스
```

### 1-6. 한계
- 이름 기반 직렬화가 아직 없음.
- "Hidden enum entry" 같은 메타데이터 없음.
- `enum`(class 아님)은 지원하지 않음 — `enum class` 만 파싱.

---

## 2. `FStructProperty` — `USTRUCT()` 구조체 멤버

### 2-1. 저장 표현
저자 코드는 그냥 POD 구조체이다.

```cpp
// Transform.h
USTRUCT()
struct FTransform
{
    GENERATED_BODY(FTransform)

    UPROPERTY(Edit, Category="Transform")
    FVector Location;

    FQuat Rotation;  // UPROPERTY 미부착 → 리플렉션 제외

    UPROPERTY(Edit, Category="Transform")
    FVector Scale;
};
```

값은 구조체 그 자체로 메모리에 인라인된다. 별도 포인터·박싱 없음.

### 2-2. 디스크립터
[`FStructProperty`](../KraftonEngine/Source/Engine/Core/Property/FStructProperty.h) 는 자식 필드 스키마를 **함수 포인터**로 받는다.

```cpp
using FStructPropertySchemaFn = const std::vector<FProperty*>& (*)();

class FStructProperty final : public FProperty {
public:
    FStructPropertySchemaFn SchemaFn = nullptr;
};
```

함수 포인터로 받는 이유:
- **순환 의존 회피**: 구조체 A 안에 구조체 B 가, B 안에 A 의 다른 인스턴스가 있을 수 있다. 즉시 평가 대신 lazy 평가.
- **정적 초기화 순서 회피**: `static const std::vector` 가 함수 안에 있으므로 첫 호출에서 초기화된다.

### 2-3. Codegen 산출물
`USTRUCT()` 가 붙은 타입은 `KE_GENERATED_BODY_<Name>()` 매크로 안에 `static const std::vector<FProperty*>& GetSchema();` 선언을 emit하고 ([`GenerateCode.py:567-570`](../Scripts/GenerateCode.py#L567-L570)), `.gen.cpp` 에 정의를 만든다.

`FTransform` 의 산출물 ([`Transform.gen.cpp`](../KraftonEngine/Intermediate/Generated/Source/Transform.gen.cpp)):

```cpp
const std::vector<FProperty*>& FTransform::GetSchema()
{
    static const std::vector<FProperty*> Schema = []() {
        std::vector<FProperty*> Properties;
        Properties.push_back(new FVec3Property(
            "Location", "Transform", CPF_Edit,
            offsetof(FTransform, Location), sizeof(((FTransform*)0)->Location)));
        Properties.push_back(new FVec3Property(
            "Scale", "Transform", CPF_Edit,
            offsetof(FTransform, Scale), sizeof(((FTransform*)0)->Scale)));
        return Properties;
    }();
    return Schema;
}
```

호스트 클래스(예: `AActor`)가 `FTransform` 멤버를 가지면, 그 클래스의 `PropertyRegistrar` 가 다음과 같이 디스크립터를 만든다:

```cpp
new FStructProperty(
    "Transform", "Actor", CPF_Edit,
    offsetof(AActor, ActorTransform),
    sizeof(((AActor*)0)->ActorTransform),
    &FTransform::GetSchema);   // ← 함수 포인터로 자식 스키마 전달
```

### 2-4. 직렬화
재귀적이다. 각 자식 `FProperty` 의 `Serialize` 를 호출해 JSON 객체로 합친다.

```cpp
json::JSON FStructProperty::Serialize(const void* Instance) const {
    const void* StructInstance = ContainerPtrToValuePtr(Instance);
    json::JSON Object = json::Object();
    for (const FProperty* Child : SchemaFn()) {
        if (Child) Object[Child->Name] = Child->Serialize(StructInstance);
    }
    return Object;
}
```

`Deserialize` 도 같은 모양: 자식 키가 JSON에 없으면 그 필드는 건너뛴다(=기본값 유지). 이 동작 덕분에 **구조체에 필드를 추가해도 기존 씬 파일이 깨지지 않는다.**

### 2-5. 에디터 UI
[`EditorPropertyWidget.cpp:1666`](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp#L1666) 에서 트리 노드로 펼치고, 자식들을 재귀적으로 `RenderPropertyWidget` 에 다시 넣는다. 자식의 변경은 부모 struct property 의 `PostEditProperty` 한 번으로 bubble-up 된다.

### 2-6. 한계
- **상속 없음**: USTRUCT 끼리 상속을 지원하지 않는다. 자식 스키마는 자기 자신의 필드만.
- **TArray<USTRUCT>** 는 v1 에서 차단되어 있다 ([`GenerateCode.py:660`](../Scripts/GenerateCode.py#L660)). 곧 풀릴 예정.
- **`UScriptStruct`** 도입은 WIP — 현재는 `FTransform::GetSchema()` 같은 *free function* 만 만들고 메타 객체(UStruct 인스턴스)는 없다.

---

## 3. `FArrayProperty` — `TArray<T>`

### 3-1. 저장 표현
표준 `TArray<T>` (= `std::vector<T>` 의 typedef). 메모리는 컨테이너가 관리한다.

### 3-2. 디스크립터 + 타입 소거 액세서
[`FArrayProperty`](../KraftonEngine/Source/Engine/Core/Property/FArrayProperty.h) 의 핵심은 **T 를 모른 채로 모든 배열을 다루기 위한 함수 포인터 테이블** 이다.

```cpp
// PropertyTypes.h
struct FArrayAccessor {
    uint32 (*Num)(const void*);
    void*  (*GetAt)(void*, uint32);
    void   (*AddDefault)(void*);
    void   (*RemoveAt)(void*, uint32);
    void   (*Clear)(void*);
    void   (*Assign)(void* Dst, const void* Src);
};

template <typename T>
inline FArrayAccessor* GetTArrayAccessor() {
    static FArrayAccessor s = {
        +[](const void* A) -> uint32 { return (uint32)static_cast<const TArray<T>*>(A)->size(); },
        +[](void* A, uint32 i) -> void* { return &(*static_cast<TArray<T>*>(A))[i]; },
        +[](void* A) { static_cast<TArray<T>*>(A)->emplace_back(); },
        +[](void* A, uint32 i) { auto& V = *static_cast<TArray<T>*>(A); V.erase(V.begin() + i); },
        +[](void* A) { static_cast<TArray<T>*>(A)->clear(); },
        +[](void* D, const void* S) { *static_cast<TArray<T>*>(D) = *static_cast<const TArray<T>*>(S); },
    };
    return &s;
}
```

따라서 `FArrayProperty` 는 두 가지를 들고 다닌다:

```cpp
class FArrayProperty final : public FProperty {
public:
    std::unique_ptr<FProperty> Inner;    // 요소 디스크립터 (재귀)
    FArrayAccessor*            Accessor; // T 의존 함수 포인터 테이블
};
```

`Inner` 의 `Offset_Internal` 은 **0** 이다. `Accessor->GetAt(Array, i)` 가 반환하는 포인터 자체가 요소의 시작점이므로, inner 디스크립터에는 추가 오프셋이 필요 없다.

### 3-3. Codegen 산출물
[`StaticMeshComponent.gen.cpp`](../KraftonEngine/Intermediate/Generated/Source/StaticMeshComponent.gen.cpp) 에서 발췌:

```cpp
Cls->AddProperty(new FArrayProperty(
    "Materials", "Materials", CPF_Edit | CPF_FixedSize,
    offsetof(ThisClass, MaterialSlots),
    sizeof(((ThisClass*)0)->MaterialSlots),
    std::unique_ptr<FProperty>(new FMaterialSlotProperty(
        "Element", "Materials", CPF_None, 0, sizeof(FMaterialSlot))),
    GetTArrayAccessor<FMaterialSlot>()));
```

원소 타입이 enum/struct 면 inner 디스크립터도 그에 맞춰 만들어진다 ([`GenerateCode.py:866-907`](../Scripts/GenerateCode.py#L866-L907)).

### 3-4. 직렬화
JSON 배열로 직렬화한다.

```cpp
// FixedSize 플래그가 켜져 있으면 (ex. 머티리얼 슬롯) Clear 하지 않고 in-place 로
// 덮어쓰며, 부족하면 잘라낸다. 이는 호스트(예: StaticMeshComponent)가 슬롯 개수를
// 자체 로직으로 미리 잡아두기 때문이다.
```

### 3-5. 에디터 UI
[`EditorPropertyWidget.cpp:1589`](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp#L1589): 헤더("N elements"), 추가/삭제 버튼(고정 사이즈가 아닐 때), 각 요소를 `RenderPropertyWidget` 에 재귀적으로 넘긴다. 요소 이름은 `"Element <i>"` 컨벤션 — 머티리얼 슬롯이 인덱스 추출에 의존하므로 절대 바꾸면 안 되는 약속이다.

### 3-6. 한계
- **`TArray<UObject*>`** 는 inner 가 `FObjectProperty` 여야 동작하지만, 그것이 **빈 스텁**이라 현재 못 쓴다 (§6 참고).
- **`TArray<USTRUCT>`** 는 v1 차단([`GenerateCode.py:660`](../Scripts/GenerateCode.py#L660)).
- 정렬·키 기반 `TMap` 류는 미지원.

---

## 4. `FSceneComponentRefProperty` — 컴포넌트 경로 참조 (특수 케이스)

이건 "포인터 같지만 사실은 문자열 경로" 인 **레거시 인터럽**이다.

### 4-1. 동기
`UMovementComponent::UpdatedComponent` 같은 멤버는 *"같은 액터 안의 어떤 SceneComponent" 를 가리켜야* 한다. raw 포인터를 들고 다니면 직렬화가 어렵고 (포인터를 다시 디스크 표현으로 매핑해야 함), 컴포넌트 이름이 바뀌면 즉시 깨진다.

### 4-2. 저장 표현
멤버 타입이 `FString` 이고, 내용물은 컴포넌트 경로 문자열(예: `"Root/Body"`). codegen 의 [`POINTER_TYPE_MAP`](../Scripts/GenerateCode.py#L281-L283) 에 `USceneComponent*` → `EPropertyType::SceneComponentRef` 매핑이 등록되어 있다.

### 4-3. 디스크립터
[`FSceneComponentRefProperty`](../KraftonEngine/Source/Engine/Core/Property/PropertyTypes.h) 는 추가 메타데이터 없이 `FString` 그대로 직렬화한다.

### 4-4. 에디터 UI
[`EditorPropertyWidget.cpp:1212`](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp#L1212): 호스트가 `UMovementComponent` 임을 검사하고, 자기 액터의 SceneComponent 목록을 콤보 박스로 그린 뒤 선택된 항목의 경로 문자열을 저장한다.

### 4-5. 한계 / 향후
- **현재 UI 는 `UMovementComponent` 전용**으로 하드코딩되어 있다.
- 이상적으로는 **`FObjectProperty`** 가 도입되면 이 타입을 흡수해야 한다 (`FObjectProperty(PropertyClass=USceneComponent)` + Outer-relative path 표현). 그러나 컴포넌트는 자산이 아니므로 `FSoftObjectProperty` 로 흡수되지는 않는다.

---

## 5. `FSoftObjectProperty` — 자산에 대한 약/지연 참조

### 5-1. 목적
> *"액터가 자산을 '안다'고 해서, 그 자산을 메모리에 올려두고 싶지는 않다."*

`UPROPERTY(Class=UStaticMesh)` 같은 어노테이션과 함께 사용하면, 멤버는 자산 경로만 들고 다니다가 필요한 순간 `Get()` 호출에서 로드된다.

### 5-2. 저장 표현
멤버 타입은 [`TSoftObjectPtr<T>`](../KraftonEngine/Source/Engine/Core/UObject/TSoftObjectPtr.h), 베이스는 [`FSoftObjectPtr`](../KraftonEngine/Source/Engine/Core/UObject/FSoftObjectPtr.h):

```cpp
class FSoftObjectPtr {
protected:
    FSoftObjectPath          Path;            // 문자열 경로
    mutable FWeakObjectPtr   CachedObject;    // 로드 후 캐시 (Index+Serial)
};
```

`TSoftObjectPtr<T>` 는 데이터 멤버가 없는 typed wrapper다(`static_assert(sizeof(...) == sizeof(FSoftObjectPtr))`).

### 5-3. 디스크립터 — `FSoftObjectProperty`
[`FSoftObjectProperty`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/FSoftObjectProperty.h)는 `FObjectPropertyBase` 를 상속하여 다음 가상 메서드 두 개를 구현한다.

```cpp
class FObjectPropertyBase : public FProperty {
public:
    virtual UObject* GetObjectPropertyValue(void* PropAddr) const = 0;
    virtual void     SetObjectPropertyValue(void* PropAddr, UObject* Value) const = 0;

    UClass* PropertyClass = nullptr;  // 허용되는 타입(예: UStaticMesh::StaticClass())
};
```

소프트 구현:

```cpp
// FSoftObjectProperty.cpp
UObject* FSoftObjectProperty::GetObjectPropertyValue(void* Addr) const {
    return static_cast<FSoftObjectPtr*>(Addr)->Get();
    // 미로드 상태면 ResolveObject() 가 GUObjectArray 순회 → 자산이 이미 메모리에
    // 있으면 캐시에 채우고 반환, 없으면 nullptr.
}

void FSoftObjectProperty::SetObjectPropertyValue(void* Addr, UObject* Value) const {
    auto* Ptr = static_cast<FSoftObjectPtr*>(Addr);
    if (!Value || (PropertyClass && !Value->IsA(PropertyClass))) {
        Ptr->Reset();
        return;
    }
    Ptr->SetPath(Value->GetAssetPathFileName());
    Ptr->SetCache(Value);
}
```

타입 안전성 체크는 디스크립터의 `PropertyClass` 에서 한다. *"Data is dumb. Metadata is smart."*

### 5-4. Codegen 어노테이션
```cpp
UPROPERTY(Edit, Category="Mesh", Class=UStaticMesh)
TSoftObjectPtr<UStaticMesh> StaticMesh;
```

[`GenerateCode.py:826-831`](../Scripts/GenerateCode.py#L826-L831) 에서 `Class=` 를 받아 `PropertyClass` 인자로 emit:

```cpp
new FSoftObjectProperty(
    "Static Mesh", "Mesh", CPF_Edit,
    offsetof(ThisClass, StaticMesh),
    sizeof(((ThisClass*)0)->StaticMesh),
    UStaticMesh::StaticClass());
```

### 5-5. 직렬화
경로 문자열만 JSON 으로 나간다. 캐시는 직렬화 대상 아님.

```cpp
// 저장: "/Game/Mesh/Tank.uasset"
// 로딩: SetPath만 호출 → Get() 첫 호출 때 ResolveObject 시도
```

[`FSoftObjectPath::ResolveObject`](../KraftonEngine/Source/Engine/Core/UObject/FSoftObjectPath.cpp) 는 `GUObjectArray` 를 선형 순회하면서 `GetAssetPathFileName()` 이 일치하는 객체를 찾는다 — 따라서 객체가 이미 로드되어 있어야 hit한다. 자동 로딩 트리거는 아직 없다.

### 5-6. 에디터 UI
[`EditorPropertyWidget.cpp:1268`](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp#L1268): `PropertyClass` 가 무엇이냐에 따라 분기하여 ImGui 콤보 박스 + Import 버튼을 그린다. `UStaticMesh`, `USkeletalMesh` 등 자산 클래스별로 별도 분기가 있다.

### 5-7. 한계
- `ResolveObject` 가 O(N) 선형 검색 → 자산 수가 늘면 비용 증가. 경로 → 객체 해시 테이블이 필요.
- 미로드 자산을 디스크에서 자동으로 끌어오는 로더 훅 없음 — 누군가가 미리 로드해야 한다.
- 에디터 UI의 `PropertyClass` 분기가 하드코딩 — `if/else` 사슬이 늘어남.

---

## 6. `FObjectProperty` / `FClassProperty` — 하드 참조 *(빈 스텁; 의도 설계)*

현재 두 헤더 모두 `#pragma once` 한 줄뿐이다.

```cpp
// FObjectProperty.h
#pragma once

// FClassProperty.h
#pragma once
```

본 절은 **이 두 클래스가 채워지면 어떻게 동작해야 하는지** 를 설계 수준에서 명세한다. GC 도입의 사전 조건이기도 하다 ([`GCImplementationStatus.md`](GCImplementationStatus.md) §4-1).

### 6-1. 의도된 저장 표현
저자 코드:

```cpp
UPROPERTY(Edit, Category="Aim")
AActor* TargetActor = nullptr;

UPROPERTY(Edit, Category="Spawn")
UClass* SpawnableClass = nullptr;
```

멤버 타입은 그냥 `T*` (또는 향후 `TObjectPtr<T>` 래퍼). 메모리에 raw 포인터가 살아 있다.

### 6-2. `FObjectProperty` 의도 명세

```cpp
class FObjectProperty : public FObjectPropertyBase {
public:
    FObjectProperty(const FString& InName, const FString& InCategory,
                    uint32 InFlag, uint32 InOffset, uint32 InSize,
                    UClass* InPropertyClass)
        : FObjectPropertyBase(InName, InCategory, InFlag, InOffset, InSize, InPropertyClass)
    {}

    EPropertyType GetType() const override { return EPropertyType::Object; } // 신규 enum 값
    json::JSON Serialize(const void* Instance) const override;
    void       Deserialize(void* Instance, const json::JSON& Value) const override;

    UObject* GetObjectPropertyValue(void* Addr) const override {
        return *static_cast<UObject* const*>(Addr);   // 그냥 포인터 deref
    }

    void SetObjectPropertyValue(void* Addr, UObject* Value) const override {
        if (Value && PropertyClass && !Value->IsA(PropertyClass)) {
            return;  // 타입 미스매치는 무시 (또는 로그)
        }
        *static_cast<UObject**>(Addr) = Value;
    }
};
```

**핵심 동작 — 직렬화**:

```cpp
json::JSON FObjectProperty::Serialize(const void* Instance) const {
    UObject* Obj = *static_cast<UObject* const*>(ContainerPtrToValuePtr(Instance));
    if (!Obj) return json::JSON("");

    // 결정 사항: 어떤 식별자로 저장할 것인가?
    //   A) UUID 기반 (동일 씬 내부에서만 valid; 액터 간 약참조에 적합)
    //   B) 자산 경로 기반 (자산이라면 FSoftObjectProperty와 동일)
    //   C) Outer-relative 이름 경로 (Unreal 의 GetPathName() 방식)
    // 현재 엔진은 A) UUID 가 가장 자연스럽다 — UObject 마다 UUID 가 이미 있고
    // GUObjectArray::FindByUUID 가 이미 존재한다.
    return json::JSON(std::to_string(Obj->GetUUID()));
}

void FObjectProperty::Deserialize(void* Instance, const json::JSON& Value) const {
    UObject** Target = static_cast<UObject**>(ContainerPtrToValuePtr(Instance));
    const FString S = Value.ToString();
    if (S.empty()) { *Target = nullptr; return; }

    UObject* Resolved = GUObjectArray.FindByUUID(static_cast<uint32>(std::stoul(S)));
    // PropertyClass 가 강제되어 있다면 IsA 체크
    if (Resolved && PropertyClass && !Resolved->IsA(PropertyClass)) {
        Resolved = nullptr;
    }
    *Target = Resolved;
}
```

> ⚠️ UUID 기반은 **씬 로딩 패스 순서** 문제를 만든다 — 참조되는 객체가 참조하는 객체보다 먼저 로드되어야 한다. 현재 [`FSceneSaveManager`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) 는 이미 2-패스 로드(객체 생성 → 프로퍼티 채우기)를 하므로 자연스럽게 들어맞는다.

**핵심 동작 — GC 통합**:

GC 의 mark 페이즈에서 객체 그래프를 순회할 때 `FObjectProperty` 는 다음 역할을 한다.

```cpp
// 가상의 AddReferencedObjects 기본 구현
void UObject::AddReferencedObjects(FReferenceCollector& Collector) {
    TArray<const FProperty*> Props;
    GetAllProperties(Props);
    for (const FProperty* P : Props) {
        if (auto* OP = dynamic_cast<const FObjectPropertyBase*>(P)) {
            void* Addr = OP->ContainerPtrToValuePtr(this);
            UObject* Ref = OP->GetObjectPropertyValue(Addr);
            if (Ref) Collector.AddReferencedObject(Ref);
            // 주의: FSoftObjectProperty 의 GetObjectPropertyValue 는 미로드 시 nullptr
            // 을 반환하므로 자연스럽게 강참조에서 빠진다 — 이게 "소프트" 의 정의.
        }
        // FArrayProperty/FStructProperty 의 inner 도 재귀 순회
    }
}
```

즉 `FObjectProperty` 가 GC 에 노출하는 것은 **하드 참조**, `FSoftObjectProperty` 는 **약 참조**. 같은 가상 인터페이스 `GetObjectPropertyValue` 를 통해 두 의미가 다르게 표현된다.

### 6-3. `FClassProperty` 의도 명세

`FClassProperty` 는 `FObjectProperty` 와 동일한 메모리 표현(`UObject*` 슬롯)을 갖지만, **항상 `UClass` 인스턴스**를 가리킨다. 두 가지 차이:

1. **`PropertyClass`** 는 "이 슬롯이 가리킬 수 있는 UClass 의 베이스 제약" 을 의미한다 (예: `MetaClass = AActor::StaticClass()` → "AActor 의 자식 클래스만").
2. **직렬화**는 UUID 가 아니라 클래스 이름 문자열이 자연스럽다 (UClass 는 정적 객체이고 이름이 유일하므로).

```cpp
class FClassProperty : public FObjectProperty {
public:
    UClass* MetaClass = nullptr;  // PropertyClass 와 별개의 추가 제약

    json::JSON Serialize(const void* Instance) const override {
        UClass* C = static_cast<UClass*>(*static_cast<UObject* const*>(ContainerPtrToValuePtr(Instance)));
        return json::JSON(C ? FString(C->GetName()) : FString(""));
    }
    void Deserialize(void* Instance, const json::JSON& Value) const override {
        UClass* Found = UClass::FindByName(Value.ToString().c_str());
        if (Found && MetaClass && !Found->IsChildOf(MetaClass)) Found = nullptr;
        *static_cast<UObject**>(ContainerPtrToValuePtr(Instance)) = Found;
    }
};
```

### 6-4. Codegen 통합
[`GenerateCode.py`](../Scripts/GenerateCode.py) 측에서 필요한 변경:

1. `POINTER_TYPE_MAP` 에 `"UObject"` (그리고 모든 UClass-derived) → `EPropertyType::Object` 매핑 추가. 현재는 `USceneComponent` 만 매핑되어 있고 그것도 `SceneComponentRef` 라는 특수 케이스다.
2. 더 일반적으로는, **포인터형 멤버 + 알려진 `UCLASS` 타입의 조합**을 만나면 자동으로 `FObjectProperty(PropertyClass=<TheClass>::StaticClass())` 로 emit.
3. `UClass*` 멤버는 `FClassProperty` 로 분기. 어노테이션으로 `MetaClass=AActor` 같은 추가 제약 지원.
4. `PROPERTY_HEADER_BY_TYPE` 에 `EPropertyType::Object`, `EPropertyType::Class` → `Core/Property/FObjectPropertyBase/FObjectProperty.h`, `FClassProperty.h` 추가.

### 6-5. 에디터 UI 의도
- **`FObjectProperty`**: 콤보 박스에 `GUObjectArray` 의 `PropertyClass` 자식들을 나열. 드래그&드롭으로 씬 트리에서 객체를 떨어뜨릴 수 있게 함.
- **`FClassProperty`**: 콤보 박스에 `UClass::GetAllClasses()` 중 `MetaClass` 자식만 필터링하여 나열.

### 6-6. 도입 시 결정해야 할 것
- **포인터 vs `TObjectPtr<T>` 래퍼**: UE5처럼 raw 포인터를 래퍼로 감쌀 것인가? 현재 코드베이스는 raw `T*` 가 압도적이므로 래퍼 도입은 점진적 마이그레이션 비용을 동반한다.
- **PIE/씬 간 참조 정책**: 한 씬의 액터가 다른 씬의 액터를 참조하면 어떻게 할 것인가? (현재는 단일 World 전제라 큰 이슈 없음.)
- **댕글링 정책**: GC 도입 전까지는 `FObjectProperty` 가 참조하는 객체가 다른 곳에서 `DestroyObject` 되어 사라질 수 있다. 직렬화 시 `IsValid` 체크가 안전망. 장기적으로는 GC 가 raw 포인터를 자동으로 null 처리하거나, `TObjectPtr` 가 stale 감지를 한다.

### 6-7. 한 줄 결론
`FObjectProperty` / `FClassProperty` 는 *기술적으로* 단순(포인터 deref + 타입 체크)하지만, *정책적으로*는 직렬화 표현과 GC 의미를 동시에 결정해야 하므로 도입 비용이 코드보다 설계에 있다.

---

## 7. 비교 요약표

| 디스크립터 | 추가 메타데이터 | 직렬화 표현 | GC 의미 (예정) |
|---|---|---|---|
| `FEnumProperty` | `EnumNames[]`, `EnumCount`, `EnumSize` | int32 인덱스 | — (값 타입) |
| `FStructProperty` | `SchemaFn` (자식 디스크립터) | JSON 객체 (자식 재귀) | 자식 재귀 |
| `FArrayProperty` | `Inner` 디스크립터, `Accessor` 함수 테이블 | JSON 배열 (요소 재귀) | 요소 재귀 |
| `FSceneComponentRefProperty` | 없음 (FString 그대로) | 경로 문자열 | — (문자열) |
| `FSoftObjectProperty` | `PropertyClass` | 경로 문자열 | **약 참조** (미로드 시 nullptr) |
| `FObjectProperty` *(예정)* | `PropertyClass` | UUID 문자열 | **강 참조** |
| `FClassProperty` *(예정)* | `PropertyClass`, `MetaClass` | 클래스 이름 문자열 | **강 참조** (UClass 는 항상 정적) |

---

## 8. 한 줄 요약

> **enum 은 이름 테이블, struct 는 자식 스키마 함수, 배열은 타입 소거 액세서, soft 참조는 경로+캐시, hard 참조는 (도입 시) raw 포인터+`PropertyClass` 제약** — 추상 베이스 `FProperty` 의 `Serialize`/`Deserialize` 두 가상 메서드 뒤에, 각 특수 타입마다 *딱 그 타입에만 필요한 최소한의 메타데이터* 만 더해서 모든 처리를 일반화한다. `FObjectProperty` 의 스텁을 채우는 일은 **단순한 포인터 처리 함수 한 쌍**이지만, **하드 참조의 직렬화 표현(UUID vs 경로 vs Outer-relative path)을 결정하는 정책 작업**이 본질적인 무게이다.
