# Garbage Collection 구현 진척도

KraftonEngine은 Unreal Engine의 객체 모델(`UObject` + `UClass` + `FProperty`)을 차용하여 발전해 왔다. Unreal이 그 위에 올린 **Mark & Sweep 기반 가비지 컬렉터(GC)** 를 본 엔진에 도입하기 위해 지금까지 어느 수준의 기반이 마련되어 있고, 남은 작업이 무엇인지를 정리한다.

> 본 문서는 **현재 main 브랜치 기준의 상태 스냅샷**이다. 코드가 바뀌면 같이 갱신해야 한다.

---

## 1. GC가 왜 필요한가?

현재 엔진의 객체 수명은 **명시적 소유권 / 명시적 파괴**로 운영된다.

- 생성: `GUObjectArray.CreateObject<T>(Outer)` 또는 `FObjectFactory::Get().Create("ClassName", Outer)`
- 파괴: `GUObjectArray.DestroyObject(Object)` — 즉시 `delete` 호출

이 모델은 단순하지만 다음 문제를 안고 있다.

1. **댕글링 포인터 위험**: 한 곳에서 `DestroyObject`를 호출하면, 같은 객체를 참조하던 다른 모든 raw `UObject*`가 즉시 무효화된다. 어디서 누가 들고 있는지를 사람이 추적해야 한다.
2. **파괴 순서 위험**: `World` 가 `Actor` 보다 먼저 delete 되면 컴포넌트의 `DestroyRenderState` 가 freed Outer를 deref 해서 crash 난 사례가 이미 발생했다. (현재 [`UObject::GetTypedOuter`](../KraftonEngine/Source/Engine/Object/Object.h)는 매 단계 `IsValid` 가드로 완화 중.)
3. **참조 누수**: 객체를 더 이상 안 쓰는데도 누군가가 raw 포인터를 들고 있으면, 사람이 직접 끊지 않으면 살아남는다 (혹은 끊지 않은 채 destroy 하면 1번 문제로 이어진다).

GC가 도입되면:

- 객체는 **루트에서 도달 가능한 동안만 살아 있고**, 도달 불가능해진 순간 자동 회수된다.
- 명시적 `DestroyObject` 대신 **`MarkAsGarbage()` (= "더는 안 쓴다" 선언)** 만 호출하면 충분하다.
- 댕글링은 `TWeakObjectPtr` / `Serial Number` 기반 약참조와 결합되어 컴파일·런타임 모두에서 다루기 쉬워진다.

---

## 2. 이미 갖춰진 기반 (Already in place)

GC 구현을 위해 필요한 인프라 중 상당 부분이 이미 들어와 있다.

### 2-1. 중앙 객체 레지스트리 — [`FUObjectArray`](../KraftonEngine/Source/Engine/Object/FUObjectArray.h)
모든 `UObject`는 정확히 한 곳, **전역 `GUObjectArray`** 에 등록된다.

```cpp
struct FUObjectItem {
    UObject* Object       = nullptr;
    int32    SerialNumber = -1;
    bool     bIsStatic    = false;   // UClass 등은 GC 대상에서 제외
};

TArray<FUObjectItem> Items;        // 슬롯 배열 (인덱스 안정)
TArray<int32>        FreeIndices;  // 슬롯 재활용을 위한 free-list
TSet<UObject*>       LiveSet;      // 빠른 alive 체크용
```

- 슬롯의 **인덱스가 안정적**이고 (free-list로 재활용), 회수 시 `SerialNumber` 를 bump하여 **stale 핸들을 감지**할 수 있다. ⇒ Mark/Sweep 시점에 비트맵을 슬롯에 다는 것이 자연스럽다.
- `bIsStatic` 플래그로 **GC가 절대 건드리지 말아야 할 객체**(클래스 메타, 영구 자원 등)를 이미 분리해 두었다. [`UClass::RegisterClassesAsStaticObjects`](../KraftonEngine/Source/Engine/Object/UClass.cpp) 가 이 역할을 한다.
- `IsAlive(const UObject*)` 는 hash 조회만 하므로 **freed 포인터를 deref 하지 않아도 안전**하다 — GC 외부에서 약참조를 검증할 때도 그대로 쓸 수 있다.

### 2-2. 단일 생성/파괴 경로
- 생성: `FUObjectArray::CreateObject<T>(Outer)` 가 `new T()` → `SetOuter` → 자동 이름 발급 → `AddObject` 를 한 번에 한다.
- 파괴: 어떤 경로든 `delete Object` 가 호출되면 `UObject::~UObject()` 에서 `GUObjectArray.RemoveObject(this)` 가 자동 실행된다.

⇒ GC 도입 시 `DestroyObject` 를 *직접 delete* 대신 *Mark-as-Garbage* 로 바꾸면, 기존 호출부 대부분이 그대로 작동한다.

### 2-3. 안전한 약참조 — [`FWeakObjectPtr`](../KraftonEngine/Source/Engine/Core/UObject/FWeakObjectPtr.h) / [`TWeakObjectPtr<T>`](../KraftonEngine/Source/Engine/Core/UObject/TWeakObjectPtr.h)
인덱스 + 시리얼 번호 쌍으로 객체를 가리킨다. 시리얼은 회수 시 bump되므로, **객체가 사라졌는데도 코드가 멀쩡히 살아남도록** 만들어 준다.

```cpp
UObject* FWeakObjectPtr::Get() const {
    return IsValid() ? GUObjectArray.IndexToObject(ObjectIndex) : nullptr;
}
```

GC 구현 후 raw `UObject*` 의 자리에 점진적으로 들어갈 **표준 핸들 타입**이 이미 준비되어 있다.

### 2-4. 지연 로드/약 자산 참조 — [`FSoftObjectPtr`](../KraftonEngine/Source/Engine/Core/UObject/FSoftObjectPtr.h) / [`FSoftObjectPath`](../KraftonEngine/Source/Engine/Core/UObject/FSoftObjectPath.h)
경로 문자열만 들고 다니다가 `ResolveObject()` 시점에 객체를 로드한다. **GC가 자산을 회수해도 경로는 살아남는** 정책이 가능해진다.

### 2-5. 프로퍼티 리플렉션
[`Property Reflection System`](PropertyReflectionSystem.md) 문서 참고. GC가 **객체 그래프를 자동으로 순회**하기 위한 핵심 기반은 결국 리플렉션이다. 현재 갖춰진 것:

- `UClass::Properties` 와 `UStruct::GetAllProperties()` — 한 객체의 모든 멤버를 나열할 수 있다.
- `FStructProperty` — 중첩 구조체 안의 멤버까지 재귀적으로 노출.
- `FArrayProperty` + `FArrayAccessor` — `TArray<T>` 의 길이/요소 주소를 타입 소거된 형태로 얻을 수 있다.
- [`FObjectPropertyBase`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/FObjectPropertyBase.h) — **`UObject*` 멤버를 가리키는 디스크립터 가상 베이스**.
  - `GetObjectPropertyValue(void* PropAddr)` 가 가상 메서드로 정의되어 있어, GC는 디스크립터를 보고 "이 슬롯에 들어 있는 UObject*"를 얻을 수 있다.
  - `PropertyClass` 메타데이터로 타입 제약까지 표현된다.

### 2-6. 객체 식별성 / 정적 객체 격리
- 모든 `UObject` 는 `UUID` 와 `InternalIndex` 를 갖는다.
- `UClass`, `UStruct` 같은 메타 객체는 `RegisterClassesAsStaticObjects()` 로 `bIsStatic = true` 가 찍혀 있다 ⇒ Sweep 단계에서 자연스럽게 무시 가능.
- `UObject::Outer` 체인은 **소유 의미가 아니라 논리 스코프**라고 명시되어 있어, 향후 "Outer 가 죽으면 Inner 도 자동으로 unreachable" 같은 정책을 별도로 설계할 여지를 남겨 둔다.

### 2-7. GC를 의식한 자리표시 코드
[`FUObjectArray.cpp`](../KraftonEngine/Source/Engine/Object/FUObjectArray.cpp) 에는 이미 다음과 같은 흔적이 있다:

```cpp
// GC feature: Do this later
//void FUObjectArray::FreeUObjectIndex(UObject* Object);

// TODO: Extend this function for GC
//void FUObjectArray::FreeUObjectIndex(UObject* Object) { ... }
```

⇒ "RemoveObject 와 별개로 인덱스만 회수하는 경로"를 미리 자리 잡아 둔 상태.

---

## 3. 진척도 한눈에 보기

| 카테고리 | 항목 | 상태 |
|---|---|---|
| **레지스트리** | 전역 객체 배열 (`FUObjectArray`) | ✅ 완료 |
| | 인덱스 재활용 (free-list) | ✅ 완료 |
| | 시리얼 넘버 bump | ✅ 완료 |
| | 정적 객체 격리 (`bIsStatic`) | ✅ 완료 |
| **약참조** | `FWeakObjectPtr` / `TWeakObjectPtr<T>` | ✅ 완료 |
| | `FSoftObjectPtr` / `FSoftObjectPath` | ✅ 완료 |
| **생성/파괴 경로** | `CreateObject<T>` 단일화 | ✅ 완료 |
| | `~UObject` 가 자동 `RemoveObject` | ✅ 완료 |
| **리플렉션** | `UClass` / `UStruct` / `FProperty` 트리 | ✅ 완료 |
| | `FArrayProperty` / `FStructProperty` | ✅ 완료 |
| | `FObjectPropertyBase` 가상 인터페이스 | ✅ 완료 |
| | **`FObjectProperty` (하드 UObject* 참조)** | ❌ 헤더 빈 스텁만 존재 |
| | **`FClassProperty` (UClass* 참조)** | ❌ 헤더 빈 스텁만 존재 |
| **GC 코어** | `PendingKill` / `MarkAsGarbage` 비트 | ❌ 미구현 |
| | GC 루트 등록 (`AddToRoot` / `RootSet`) | ❌ 미구현 |
| | 참조 콜렉터 (`FReferenceCollector`) | ❌ 미구현 |
| | `AddReferencedObjects` 가상 메서드 | ❌ 미구현 |
| | Mark phase | ❌ 미구현 |
| | Sweep phase | ❌ 미구현 |
| | `CollectGarbage()` 엔트리 포인트 | ❌ 미구현 |
| **트리거** | 프레임 끝 / 메모리 임계 트리거 | ❌ 미구현 |
| **고급 기능** | 증분 GC / 클러스터 GC | ❌ 범위 외 |
| | 멀티스레드 GC | ❌ 범위 외 |

대략적으로 **"인프라 70%, GC 알고리즘 0%"** 의 상태이다. 그릇은 거의 완성되었지만, 그 안에서 동작할 Mark/Sweep 본체는 아직 한 줄도 쓰여 있지 않다.

---

## 4. 남은 작업 (To-do, 우선순위 순)

### 4-1. 하드 UObject 참조 디스크립터 완성 — **블로커**
- 파일: [`FObjectProperty.h`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/FObjectProperty.h), [`FClassProperty.h`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/FClassProperty.h)
- 현재 둘 다 `#pragma once` 한 줄만 있는 **빈 스텁**이다.
- 이 두 클래스가 없으면 GC는 일반 멤버 변수 `UStaticMesh* StaticMesh` 같은 **하드 참조를 발견할 수 없다.** 모든 UObject 참조가 `FSoftObjectProperty` 로 가는 것은 비현실적이므로, 정상 동작하는 `FObjectProperty` 가 GC의 진짜 사전 조건이다.
- 해야 할 일:
  - `FObjectProperty` 가 raw `UObject*` 슬롯의 직렬화/역직렬화, `GetObjectPropertyValue` / `SetObjectPropertyValue` 를 구현.
  - codegen 측 ([`Scripts/GenerateCode.py`](../Scripts/GenerateCode.py))에 `UObject*` 멤버 → `EPropertyType::Object` 매핑 추가 (현재는 `POINTER_TYPE_MAP` 에 `USceneComponent` 만 있으며 그것도 `SceneComponentRef` 라는 문자열 기반 특수 케이스다).
- `TArray<UObject*>` / `UObject*` 멤버를 갖는 클래스에 정상적으로 `FObjectProperty` 가 등록되어야 GC 그래프 순회가 가능해진다.

### 4-2. PendingKill 상태와 즉시 파괴의 분리
- `UObject` 에 `EObjectFlags` 또는 단순 `bool bPendingKill` 도입.
- `UObject::MarkAsGarbage()` 또는 `MarkPendingKill()` API 추가.
- 기존 `GUObjectArray.DestroyObject(...)` 호출부(약 30곳)를 점진적으로 `MarkAsGarbage()` 로 교체. **즉시 delete 가 정말 필요한 자리(예: 에디터의 임시 Gizmo, 자원 매니저의 미사용 자산 해제)와 GC에 맡길 자리를 구분**하는 작업이 함께 필요하다.
- `IsValid(UObject*)` 의 의미가 *"메모리상 살아있다"* 에서 *"살아있고 PendingKill 도 아님"* 으로 확장되어야 한다.

### 4-3. GC Root Set
- 전역 root 컨테이너 (`TSet<UObject*> RootSet` 같은 것) + `AddToRoot` / `RemoveFromRoot`.
- 자연스러운 root 후보:
  - `UEngine`, `UEditorEngine` 인스턴스
  - 로드된 `UWorld` (런타임/PIE)
  - 모든 `bIsStatic` 객체 (자동 root 취급)
  - 자산 매니저들 ([`MeshManager`](../KraftonEngine/Source/Engine/Mesh/), [`MaterialManager`](../KraftonEngine/Source/Engine/Materials/), `SkeletonManager`, `AnimSequenceManager`, `CameraShakeManager`, `FloatCurveManager`, `Texture2DManager`)가 들고 있는 자산 핸들

### 4-4. 참조 콜렉터 인터페이스
- 단순한 형태로 시작:
  ```cpp
  class FReferenceCollector {
  public:
      virtual void AddReferencedObject(UObject*& Obj) = 0;
  };

  // UObject 가상 메서드
  virtual void AddReferencedObjects(FReferenceCollector& Collector);
  ```
- 기본 구현은 **리플렉션을 그대로 사용**한다:
  1. `GetClass()->GetAllProperties()` 순회
  2. 각 `FProperty` 가 `FObjectPropertyBase` 이면 `GetObjectPropertyValue` → `Collector.AddReferencedObject`
  3. `FArrayProperty` 의 inner가 `FObjectPropertyBase` 이면 요소를 순회
  4. `FStructProperty` 는 자식 스키마를 재귀
- 리플렉션으로 잡히지 않는 멤버(예: 컨테이너에 직접 들어 있는 raw `UObject*` 캐시) 가 있는 클래스는 가상 메서드를 오버라이드해서 수동 emit.

### 4-5. Mark Phase
- `FUObjectItem` 에 `bMarked` (혹은 `uint8 GCFlags`) 비트 추가.
- 알고리즘 (반복형 BFS/DFS):
  1. 모든 슬롯의 mark bit clear.
  2. Root set의 모든 객체를 큐에 push, mark.
  3. 큐를 비울 때까지: 객체를 꺼내 `AddReferencedObjects` 호출 → 참조된 객체 중 unmarked 이면 mark 하고 push.
  4. `bIsStatic == true` 인 슬롯은 자동 mark (혹은 항상 root 취급).

### 4-6. Sweep Phase
- Items 슬롯을 순회, **unmarked && !PendingKill 면 PendingKill 로**, **PendingKill 이면 실제 delete**, 식의 2단계 회수도 고려 가능 (UE 의 클러스터 GC 가 유사한 아이디어). 단순 시작은 "unmarked → 즉시 delete".
- 회수 시 현재 코드의 `RemoveObject` 가 호출하는 단계 (LiveSet erase, SerialNumber bump, FreeIndices push) 가 그대로 재사용된다.
- 주석으로 자리만 잡아 둔 `FreeUObjectIndex` 를 정식 함수로 끌어 올리고, "메모리 free 없이 인덱스만 회수" 와 "메모리도 같이 free" 두 모드를 분리하는 것이 자연스럽다.

### 4-7. 트리거 정책
- 명시적 `CollectGarbage()` 함수.
- 자동 트리거 후보:
  - 매 N 프레임마다 (단순)
  - 메모리 stat 임계 초과 시 ([`MemoryStats`](../KraftonEngine/Source/Engine/Profiling/MemoryStats.h) 가 이미 alloc/free 카운팅 중)
  - 레벨 전환 직후 (보통 가장 안전한 시점)

### 4-8. 호출부 정리 / Raw 포인터 → 핸들 마이그레이션
- 현재 코드 전반에서 `UObject*` 를 raw로 들고 다니는 패턴이 많다.
- GC 도입 후에도 "잡아두고 싶으면" `TStrongObjectPtr<T>` 같은 명시적 핸들이 필요해질 수 있다 (현재 미구현). 아니면 "참조하면 reflection 으로 자동 발견" 모델을 강제하여 핸들 자체를 안 만드는 선택도 가능 — 단, 그러려면 모든 raw 멤버가 `UPROPERTY` 로 노출되어야 한다.

### 4-9. 알려진 위험 지점
- **렌더 측 SceneProxy**: `PrimitiveSceneProxy` 종류는 raw `UObject*` 와 별개의 lifecycle을 가지므로, GC 가 컴포넌트를 회수하면 SceneProxy도 함께 정리해야 한다. 현재 `DestroyRenderState` 가 그 책임을 지지만, 회수 시점이 즉시가 아니라 *프레임 끝 GC* 로 미뤄지면 한 프레임 동안 living proxy + dead component 라는 상태가 생긴다.
- **Outer 체인 의존 코드**: 이미 `GetTypedOuter` 에서 한 번 crash 가 났던 만큼, GC 의 회수 순서가 Outer ↓ Inner 를 보장하지 않을 수 있다는 점을 코드 곳곳에서 다시 점검해야 한다.
- **에디터 selection / undo stack** 이 raw 포인터로 액터를 들고 있으면 GC가 회수해 버리는 사태가 가능. selection 은 weak 핸들로 옮기는 것이 자연스럽다.

---

## 5. 단계별 도입 권장 순서

> 가능한 가장 작은 패치 단위로 진척시키기 위한 추천 시퀀스.

1. **`FObjectProperty` / `FClassProperty` 구현 + codegen 매핑.** GC와 무관하게 직렬화·디테일 패널이 즉시 이득을 본다. — *PR 1개 분량*
2. **`UObject::AddReferencedObjects()` + 리플렉션 기반 기본 구현.** GC 가 없어도 디버그/통계용으로 그래프를 출력할 수 있다. — *PR 1개*
3. **Root set + 자산 매니저의 root 등록.** 아직 회수는 일어나지 않지만 mark 단계만 실행해 보고 카운트로 검증. — *PR 1개*
4. **PendingKill 상태 + `MarkAsGarbage`.** `DestroyObject` 호출부를 천천히 마이그레이션. — *여러 PR로 점진적*
5. **Mark + Sweep 본체.** `CollectGarbage()` 를 우선 명시 호출만 가능하게 두고 검증. — *PR 1~2개*
6. **자동 트리거 정책 + 라이프사이클 정리.** — *나중에*

---

## 6. 한 줄 요약

> **객체 그릇(`FUObjectArray`)·약참조(`FWeakObjectPtr`)·리플렉션(`FProperty`)이라는 3대 기반은 이미 정착했고, 남은 일은 그 위에서 `MarkAsGarbage` → `Mark` → `Sweep` 알고리즘 본체를 쓰는 것이다.** 가장 먼저 막힌 곳은 GC와 직접 관련 없어 보이지만 사실 가장 시급한 [`FObjectProperty`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/FObjectProperty.h) / [`FClassProperty`](../KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/FClassProperty.h) 의 빈 스텁이다.
