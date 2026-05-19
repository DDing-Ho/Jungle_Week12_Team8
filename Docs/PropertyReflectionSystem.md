# Property Reflection System

KraftonEngine에 구현된 **Unreal 스타일 프로퍼티 리플렉션(Reflection) 시스템**에 대한 문서이다. 본 문서는 시스템이 무엇이고, 무엇을 하며, 왜 필요하고, 어떻게 구현되어 있는지를 설명한다.

> 본 문서는 현재 안정화된 부분(UClass / UStruct / FProperty 계층 / Codegen)을 중심으로 작성한다. `UScriptStruct`는 WIP이므로 다루지 않는다.

---

## 1. 시스템이란?

**프로퍼티 리플렉션 시스템**은 C++ 클래스의 멤버 변수에 대한 **메타데이터(타입, 이름, 카테고리, 오프셋, 편집 가능 여부 등)** 를 런타임에 질의할 수 있게 해주는 메커니즘이다. C++는 표준적으로 리플렉션을 지원하지 않기 때문에, 이 엔진은 Unreal Engine과 동일한 접근을 채택한다.

- 헤더에 `UCLASS()`, `USTRUCT()`, `UENUM()`, `UPROPERTY()`, `UFUNCTION()` 등의 **빈 매크로 마커**를 작성한다.
- 빌드 직전 Python 기반 헤더 툴([`Scripts/GenerateCode.py`](../Scripts/GenerateCode.py))이 헤더를 스캔하여 클래스/프로퍼티 정보를 추출한다.
- 추출 결과를 바탕으로 `.generated.h` / `.gen.cpp` 파일을 [`KraftonEngine/Intermediate/Generated/`](../KraftonEngine/Intermediate/Generated/) 아래에 자동 생성한다.
- 생성된 `.gen.cpp`는 정적 초기화 시점에 [`UClass`](../KraftonEngine/Source/Engine/Object/UClass.h)에 [`FProperty`](../KraftonEngine/Source/Engine/Core/Property/PropertyTypes.h) 디스크립터를 등록한다.

결과적으로 런타임에는 임의의 `UObject*`에 대해 다음과 같이 그 객체가 갖는 모든 프로퍼티의 목록과 값에 접근할 수 있다.

```cpp
TArray<const FProperty*> Props;
SomeObject->GetEditableProperties(Props);
for (const FProperty* P : Props)
{
    json::JSON V = P->Serialize(SomeObject); // 이름·타입에 맞춰 값을 직렬화
}
```

---

## 2. 무엇을 하는가?

리플렉션 시스템은 엔진 내 여러 서브시스템의 **공통 기반**을 이루며, 다음 기능을 가능하게 한다.

### 2-1. 에디터의 디테일(Property) 패널 자동 생성
[`EditorPropertyWidget`](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp)은 선택된 액터·컴포넌트에 대해 `GetEditableProperties()`를 호출하여 얻은 `FProperty*` 목록을 순회한다. 각 프로퍼티의 `EPropertyType`(예: `Bool`, `Float`, `Vec3`, `Enum`, `Struct`, `Array`, `SoftObject` 등)에 따라 알맞은 ImGui 위젯(체크박스, 슬라이더, 컬러 피커, 콤보 박스 등)을 자동으로 그린다. **클래스에 멤버를 하나 추가하고 `UPROPERTY(Edit)`만 붙이면 별도의 UI 코드 없이도 에디터에 노출된다.**

### 2-2. JSON 기반 씬 직렬화
[`FSceneSaveManager`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp)는 액터/컴포넌트의 상태를 저장할 때, 각 객체에 대해 `GetNonTransientProperties()`로 직렬화 대상 프로퍼티만 모은 뒤, `FProperty::Serialize(Instance)`를 호출하여 JSON 값을 얻는다. 로딩 시에도 같은 방식으로 `Deserialize(Instance, Value)`를 호출하여 값을 복원한다. **새로운 멤버를 추가해도 별도의 save/load 코드를 작성할 필요가 없다.**

### 2-3. 객체 복제 (Duplicate)
[`UObject::Duplicate()`](../KraftonEngine/Source/Engine/Object/Object.cpp)는 같은 클래스의 인스턴스를 생성한 뒤, 리플렉션 정보를 활용한 메모리 아카이브 왕복으로 멤버 상태를 그대로 복사한다.

### 2-4. RTTI / IsA / Cast
프로퍼티 리플렉션은 클래스 리플렉션과 한 몸이다. `UCLASS()` 마커는 `UClass` 정적 인스턴스와 부모 클래스 포인터를 함께 emit하므로, [`UObject::IsA<T>()`](../KraftonEngine/Source/Engine/Object/Object.h) / `Cast<T>` 가 정상 동작한다.

### 2-5. 디테일 패널에서의 카테고리·범위·표시 이름 제어
`UPROPERTY(Edit, Category="Movement", Min=0, Max=200, Speed=0.5, DisplayName="Max Speed")` 처럼 어노테이션 인자만으로 에디터 노출 정책을 제어한다. 자식 클래스에서 부모의 프로퍼티를 숨기고 싶을 때는 `UPROPERTY_HIDE("MemberName")` 마커를 사용한다 (예: `USubUVComponent` 가 `UBillboardComponent` 의 `Material` 을 숨김).

### 2-6. 에디터에서의 클래스 카탈로그
[`UClass::GetAllClasses()`](../KraftonEngine/Source/Engine/Object/UClass.h)는 정적 초기화 시 등록된 모든 클래스를 보관한다. 컴포넌트 추가 메뉴, 액터 스폰 다이얼로그 등은 이 레지스트리를 순회하여 사용 가능한 타입을 노출한다. 또한 이름 문자열로 클래스를 찾을 수 있어 (`UClass::FindByName`), 직렬화된 씬 파일의 `"Class": "UPointLightComponent"` 같은 필드를 다시 객체 타입으로 해석할 수 있다.

---

## 3. 왜 필요한가?

리플렉션이 없으면 위에서 열거한 기능을 위해 **각 클래스마다 손으로 직렬화 함수, UI 빌더, 복제 함수, 클래스 등록 코드를 반복 작성**해야 한다. 클래스가 수십 개이고 멤버가 수백 개인 엔진 코드베이스에서 이는:

- **유지보수 폭증**: 멤버 하나를 추가/이름변경할 때마다 직렬화·UI·복제 코드를 일관되게 수정해야 한다.
- **버그의 온상**: 손으로 작성한 직렬화 코드와 UI 코드는 쉽게 서로 어긋난다.
- **확장 비용**: 새 컴포넌트를 만들 때마다 “전부 다 똑같이 다시” 적어야 한다.

리플렉션은 이 문제를 다음 한 줄의 격언으로 해결한다.

> **“데이터는 단순하다. 메타데이터는 똑똑하다.”**  
> (Data is dumb. Metadata is smart.)

즉, 멤버 변수 본체는 그저 메모리 블록이며, 그 블록을 어떻게 해석하고 편집하고 직렬화할지는 모두 **`FProperty` 디스크립터**가 안다. 그러므로 데이터 위에서 동작하는 모든 일반 코드(에디터, 직렬화, 복제, 디핑, 네트워킹 등)는 디스크립터만 보고 동작할 수 있다.

---

## 4. 어떻게 구현되어 있는가?

### 4-1. 전체 흐름

```
헤더 (UCLASS/UPROPERTY 마커)
      │
      ▼
Scripts/GenerateCode.py  ── 스캔/파싱
      │
      ├──► Intermediate/Generated/Inc/<File>.generated.h
      │       └─ KE_GENERATED_BODY_<ClassName>() 매크로
      │
      └──► Intermediate/Generated/Source/<File>.gen.cpp
              ├─ UClass 정적 인스턴스 정의
              ├─ FClassRegistrar (전역 클래스 레지스트리 등록)
              ├─ REGISTER_FACTORY (이름 → 인스턴스 생성)
              ├─ <ClassName>_PropertyRegistrar (FProperty 디스크립터 등록)
              └─ (옵션) Lua usertype 바인딩
      │
      ▼
런타임:
  - UClass::GetAllClasses() / FindByName()
  - UStruct::GetProperties() / GetEditableProperties() / GetNonTransientProperties()
  - FProperty::Serialize / Deserialize
```

### 4-2. 마커 매크로

[`ObjectMacros.h`](../KraftonEngine/Source/Engine/Object/ObjectMacros.h)에 정의된 매크로들은 **컴파일 시 빈 매크로**이다. 단지 Python 파서가 위치를 식별할 수 있도록 존재한다.

```cpp
#define UCLASS(...)
#define UPROPERTY(...)
#define UFUNCTION(...)
#define UENUM(...)
#define USTRUCT(...)
#define UPROPERTY_HIDE(...)
#define GENERATED_BODY(ClassName) KE_GENERATED_BODY_##ClassName()
```

저자 사용 예:

```cpp
#include "Foo.generated.h"

UCLASS()
class UFoo : public UBar
{
    GENERATED_BODY(UFoo)

    UPROPERTY(Edit, Category="Movement", Min=0, Max=200, Speed=0.5)
    float MaxSpeed = 50.f;

    UFUNCTION(Lua)
    void Possess(AActor* Target);
};
```

`GENERATED_BODY(UFoo)` 는 매칭되는 `.generated.h` 의 `KE_GENERATED_BODY_UFoo()` 로 토큰 페이스트 된다. Unreal과 달리 `__LINE__` 추적을 쓰지 않아, **한 헤더에 여러 UCLASS가 공존**할 수 있다.

### 4-3. 헤더 툴 ([`Scripts/GenerateCode.py`](../Scripts/GenerateCode.py))

핵심 동작:

1. **`SOURCE_ROOTS`** 아래의 모든 `.h` 를 스캔하고, `UCLASS(` / `USTRUCT(` / `UENUM(` 마커가 있는 것만 추린다.
2. 정규식과 중괄호 매칭으로 클래스 본문을 잘라, **`ClassInfo`** / **`StructInfo`** / **`EnumInfo`** / **`PropertyInfo`** / **`FunctionInfo`** 로 모델링한다.
3. C++ 타입을 `EPropertyType` 으로 분류한다 (`TYPE_MAP`, `POINTER_TYPE_MAP`, 그리고 알려진 enum/struct/`TArray<T>` 추론).
4. 두 파일을 emit한다:
   - **`<File>.generated.h`**: `KE_GENERATED_BODY_<Class>()` 매크로와, UENUM 의 이름 테이블(`GFooNames[]`) 정의.
   - **`<File>.gen.cpp`**: `UClass` 정적 인스턴스 정의 + 프로퍼티 등록 구조체.

생성된 `.gen.cpp` 예시 ([`StaticMeshComponent.gen.cpp`](../KraftonEngine/Intermediate/Generated/Source/StaticMeshComponent.gen.cpp)):

```cpp
UClass UStaticMeshComponent::StaticClassInstance(
    "UStaticMeshComponent", &UMeshComponent::StaticClassInstance,
    sizeof(UStaticMeshComponent), CF_None, CASTCLASS_UStaticMeshComponent);
FClassRegistrar UStaticMeshComponent::s_Registrar(&UStaticMeshComponent::StaticClassInstance);
REGISTER_FACTORY(UStaticMeshComponent)

struct UStaticMeshComponent_PropertyRegistrar {
    UStaticMeshComponent_PropertyRegistrar() {
        using ThisClass = UStaticMeshComponent;
        UClass* Cls = UStaticMeshComponent::StaticClass();
        {
            Cls->AddProperty(new FSoftObjectProperty(
                "Static Mesh", "Mesh", CPF_Edit,
                offsetof(ThisClass, StaticMesh),
                sizeof(((ThisClass*)0)->StaticMesh),
                UStaticMesh::StaticClass()));
        }
        {
            Cls->AddProperty(new FArrayProperty(
                "Materials", "Materials", CPF_Edit | CPF_FixedSize,
                offsetof(ThisClass, MaterialSlots),
                sizeof(((ThisClass*)0)->MaterialSlots),
                std::unique_ptr<FProperty>(new FMaterialSlotProperty(...)),
                GetTArrayAccessor<FMaterialSlot>()));
        }
    }
};
static UStaticMeshComponent_PropertyRegistrar s_UStaticMeshComponent_PropertyReg;
```

`static` 인스턴스 두 개가 **정적 초기화 시점**에 동작하면서:

- `FClassRegistrar` 가 `UClass` 를 전역 레지스트리에 push.
- `<Class>_PropertyRegistrar` 가 모든 `FProperty` 디스크립터를 `UClass::Properties` 에 추가.

이로써 `main()` 진입 전에 모든 리플렉션 메타데이터가 갖춰진다.

### 4-4. 런타임 데이터 구조

#### `UClass` / `UStruct`
[`UStruct`](../KraftonEngine/Source/Engine/Object/UStruct.h) 는 “필드(=프로퍼티)의 컨테이너”라는 개념을 표현한다. `UClass`는 `UStruct`를 상속해 **이름, 부모 포인터, 크기(sizeof), `ClassFlags`, `ClassCastFlags`** 와 함께 `TArray<FProperty*> Properties` 를 보관한다.

핵심 API:

| 메서드 | 의미 |
|---|---|
| `GetAllProperties(Out)` | 베이스→파생 순서로 전체 프로퍼티 수집 |
| `GetEditableProperties(Out)` | `CPF_Edit` 플래그만 + 숨김 처리 적용 |
| `GetNonTransientProperties(Out)` | `CPF_Transient` 가 아닌 것만 (직렬화 대상) |
| `FindPropertyByName(Name)` | 이름으로 검색, 자기 → 부모 순 |
| `HideInheritedProperty(Name)` | 자식 클래스에서 부모 프로퍼티 숨김 |

캐스팅 빠른 경로는 비트마스크 기반이다. `UClass::IsChildOf(Other)` 는 `Other` 가 고유 cast 비트(`CASTCLASS_*`)를 가진 “fast-path 루트”인 경우 단일 비트 체크로 끝나며, 그 외에는 `SuperStruct` 체인을 포인터 비교로 순회한다. 이 비트는 codegen의 `CAST_FLAG_MAP` 에 등록된 루트 클래스에만 emit되고, 파생 클래스는 `UClass` 생성자에서 부모의 비트를 OR하여 상속한다.

#### `FProperty` 계층
[`FProperty`](../KraftonEngine/Source/Engine/Core/Property/PropertyTypes.h) 는 **순수 스키마**이다. 인스턴스 메모리를 보유하지 않으며, 컨테이너 포인터(=객체)와 `Offset_Internal` 을 합쳐 값 주소를 계산한다.

```cpp
class FProperty {
public:
    FString Name;
    FString Category;
    uint32  PropertyFlag;        // CPF_Edit, CPF_Transient, CPF_FixedSize, ...
    uint32  ElementSize;
    uint32  Offset_Internal;

    virtual EPropertyType GetType() const = 0;
    virtual json::JSON Serialize(const void* Instance) const = 0;
    virtual void Deserialize(void* Instance, const json::JSON& Value) const = 0;

    void* ContainerPtrToValuePtr(void* Container) const {
        return static_cast<char*>(Container) + Offset_Internal;
    }
};
```

서브클래스는 각각 별도 헤더에 위치한다 (이 레이아웃은 의도된 컨벤션이다).

| 타입 | 서브클래스 | 특징 |
|---|---|---|
| Bool / ByteBool | `FBoolProperty`, `FByteBoolProperty` | `bool` vs `uint8` 저장 차이 |
| 수치 | `FIntProperty`, `FFloatProperty` (`FNumericProperty` 상속) | `Min/Max/Speed` 메타데이터 |
| 벡터 | `FVec3Property`, `FVec4Property`, `FRotatorProperty`, `FColor4Property` | float 배열로 직렬화 |
| 문자열 | `FStringProperty`, `FNameProperty`, `FScriptProperty`, `FSceneComponentRefProperty` | 각각 `FString` / `FName` / 컴포넌트 경로 |
| 머티리얼 | `FMaterialSlotProperty` | `{ "Path": "..." }` JSON |
| 열거형 | [`FEnumProperty`](../KraftonEngine/Source/Engine/Core/Property/FEnumProperty.h) | `EnumNames` / `EnumCount` / `EnumSize` 보유, `memcpy` 로 폭 맞춤 |
| 구조체 | [`FStructProperty`](../KraftonEngine/Source/Engine/Core/Property/FStructProperty.h) | `FStructPropertySchemaFn`(= `GetSchema()`) 으로 자식 필드 재귀 |
| 배열 | [`FArrayProperty`](../KraftonEngine/Source/Engine/Core/Property/FArrayProperty.h) | `Inner` (요소 디스크립터) + `FArrayAccessor` (Num/GetAt/AddDefault/...) |
| 객체 참조 | [`FObjectPropertyBase`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/FObjectPropertyBase.h) 계열 | `GetObjectPropertyValue` / `SetObjectPropertyValue` 가상화 |
| 소프트 참조 | [`FSoftObjectProperty`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/FSoftObjectProperty.h) | 경로 문자열만 직렬화, 미로드 시 `nullptr` 반환 |

특히 두 가지 합성 타입의 작동 방식이 시스템 전체의 일반성을 보여준다.

**(a) `FArrayProperty` — 타입 소거된 배열 접근**

```cpp
struct FArrayAccessor {
    uint32 (*Num)(const void*);
    void*  (*GetAt)(void*, uint32);
    void   (*AddDefault)(void*);
    void   (*RemoveAt)(void*, uint32);
    void   (*Clear)(void*);
    void   (*Assign)(void* Dst, const void* Src);
};

template <typename T>
inline FArrayAccessor* GetTArrayAccessor();  // T별 static 인스턴스 반환
```

`FArrayProperty::Serialize` 는 `Accessor->Num` / `GetAt` 만 호출하므로, `TArray<T>` 의 T 가 무엇이든 동일한 코드로 동작한다. 요소 타입의 직렬화는 `Inner` (또 다른 `FProperty`) 가 책임진다.

**(b) `FStructProperty` — 함수 포인터로 받는 자식 스키마**

```cpp
using FStructPropertySchemaFn = const std::vector<FProperty*>& (*)();

class FStructProperty {
    FStructPropertySchemaFn SchemaFn = nullptr;
    // Serialize: SchemaFn() 으로 자식 FProperty 목록을 얻어 각각 Serialize 재호출
};
```

`USTRUCT` 가 붙은 타입은 codegen이 `static const std::vector<FProperty*>& GetSchema()` 를 정의하고, 이를 함수 포인터로 등록한다. 덕분에 **임의 깊이의 중첩 구조체**가 자연스럽게 직렬화/UI 렌더링된다.

### 4-5. 플래그

```cpp
enum EPropertyFlags : uint32 {
    CPF_None      = 0,
    CPF_Edit      = 1 << 1,  // 에디터에 노출 + 편집 가능
    CPF_FixedSize = 1 << 2,  // 배열 길이 고정 (예: 머티리얼 슬롯)
    CPF_Transient = 1 << 3,  // 직렬화 제외
    CPF_Config    = 1 << 4,
};
```

`GetEditableProperties()` 는 `CPF_Edit` 만, `GetNonTransientProperties()` 는 `CPF_Transient` 가 **아닌** 것만 반환한다. 동일한 `UPROPERTY` 가 “에디터에는 보이지만 저장은 되지 않는” 식의 정책을 깔끔히 표현한다.

### 4-6. PostEditProperty 훅

[`UObject::PostEditProperty(const char* PropertyName)`](../KraftonEngine/Source/Engine/Object/Object.h) 는 에디터에서 값이 바뀐 직후, 그리고 직렬화 로드 후 호출된다. 예를 들어 `StaticMesh` 가 바뀌면 `UStaticMeshComponent::PostEditProperty("Static Mesh")` 가 새로운 메시의 슬롯 개수에 맞춰 `MaterialSlots` 배열을 재구성한다. 직렬화 측은 이 점을 고려해 **2-패스**로 동작한다: 1패스에서 알려진 디스크립터를 로드 → `PostEditProperty` 가 새 디스크립터를 만들 수 있음 → 2패스에서 추가된 디스크립터를 마저 로드.

---

## 5. 새 프로퍼티를 추가할 때 저자가 하는 일

전체 시스템의 가치는 다음 한 줄의 사용자 경험으로 요약된다.

```cpp
UPROPERTY(Edit, Category="Lighting", Min=0, Max=10, Speed=0.05)
float Intensity = 1.0f;
```

이 한 줄을 헤더에 추가하고 `python Scripts/GenerateCode.py` 를 실행(또는 빌드 사전 단계로 자동 실행)하면:

- 에디터 디테일 패널에 `Lighting > Intensity` 슬라이더가 0–10 범위로 생긴다.
- 씬 저장 시 `Intensity` 값이 JSON에 포함된다.
- 객체 복제(`Duplicate`) 시 값이 그대로 복사된다.

직렬화 코드, UI 코드, 복제 코드를 **단 한 줄도** 작성하지 않는다.

---

## 6. 한계와 향후 계획

- **`UScriptStruct` 도입 진행 중** (`feature/UScriptStruct` 브랜치). 현재는 `USTRUCT`가 단순히 `GetSchema()` 자유 함수를 생성하지만, 향후 `UScriptStruct`를 정식 메타 객체로 둘 예정이다.
- `EPropertyType` 열거형은 디스패치용 태그로 남아있다. 서브클래스가 명확해진 현재 시점에서 점진적으로 제거될 예정 (별도 커밋).
- 함수 리플렉션(`UFUNCTION`)은 현재 Lua 바인딩에만 활용되며, `CallInEditor`/`Exec` 등은 미구현이다.
- 정규식 기반 파서이므로 `sizeof(decltype(...))` 같은 중첩 괄호 표현은 지원되지 않는다(현 v1 제약).

---

## 7. 핵심 파일 한눈에 보기

| 파일 | 역할 |
|---|---|
| [`Scripts/GenerateCode.py`](../Scripts/GenerateCode.py) | 헤더 스캔 → `.generated.h` / `.gen.cpp` emit |
| [`Source/Engine/Object/ObjectMacros.h`](../KraftonEngine/Source/Engine/Object/ObjectMacros.h) | 빈 매크로 마커 정의 |
| [`Source/Engine/Object/Object.h`](../KraftonEngine/Source/Engine/Object/Object.h) | `UObject` + RTTI 매크로 + 수동 등록 헬퍼 |
| [`Source/Engine/Object/UStruct.h`](../KraftonEngine/Source/Engine/Object/UStruct.h) | 프로퍼티 컨테이너 / 룩업 / 가시성 |
| [`Source/Engine/Object/UClass.h`](../KraftonEngine/Source/Engine/Object/UClass.h) | 클래스 메타 / 캐스트 플래그 / 전역 레지스트리 |
| [`Source/Engine/Core/Property/PropertyTypes.h`](../KraftonEngine/Source/Engine/Core/Property/PropertyTypes.h) | `FProperty` 베이스 + 단순 타입 서브클래스 |
| [`Source/Engine/Core/Property/FArrayProperty.h`](../KraftonEngine/Source/Engine/Core/Property/FArrayProperty.h) | 타입 소거 배열 디스크립터 |
| [`Source/Engine/Core/Property/FStructProperty.h`](../KraftonEngine/Source/Engine/Core/Property/FStructProperty.h) | 구조체 디스크립터(스키마 함수 포인터) |
| [`Source/Engine/Core/Property/FEnumProperty.h`](../KraftonEngine/Source/Engine/Core/Property/FEnumProperty.h) | enum 디스크립터(이름 테이블/저장 폭) |
| [`Source/Engine/Core/Property/FObjectPropertyBase/`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/) | UObject 참조 디스크립터(하드/소프트) |
| [`Source/Editor/UI/EditorPropertyWidget.cpp`](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp) | 리플렉션 기반 디테일 패널 |
| [`Source/Engine/Serialization/SceneSaveManager.cpp`](../KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) | 리플렉션 기반 씬 저장/로드 |

---

## 8. 한 문장 요약

> **헤더의 `UPROPERTY` 한 줄이, 코드 한 줄도 더 쓰지 않고 에디터·직렬화·복제 시스템 전체에서 동작하는 데이터 필드를 만들어낸다.** 이 마법을 가능하게 하는 것이 빈 매크로 마커, Python 헤더 툴, 그리고 `UClass` 안에 살아 있는 `FProperty` 디스크립터 트리이다.
