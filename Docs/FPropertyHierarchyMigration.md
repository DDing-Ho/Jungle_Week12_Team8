# FProperty hierarchy migration (Option B: schema/instance split)

Last updated: 2026-05-17

## Goal

Replace the flat tagged-union `FProperty` with a polymorphic hierarchy
(`FBoolProperty`, `FFloatProperty`, `FEnumProperty`, `FStructProperty`,
`FArrayProperty`, …). At the same time, split **schema** (Name, Type tag,
Offset, type-specific metadata) from **instance binding** (the address of
the live value). Schemas live in `UClass` / `static` caches. Instance
addresses are computed by `FProperty::ContainerPtrToValuePtr(Container)`
using the property's `Offset_Internal`.

This is a single coherent change. There is no half-way state where the
build is green; the value-copy contract documented at
`PropertyTypes.h:80-84` and enforced by `UClass`'s explicit `Inner`
delete is one invariant. It comes apart together.

## Non-goals

- Garbage collection. `FObjectProperty` / `TObjectPtr` (the user's #2)
  are out of scope and tracked separately. The hierarchy here is a
  prerequisite for #2, not a substitute.
- Touching `UTemporaryBoneAnimatorComponent`. It's behind the
  `JUNGLE_ENABLE_TEMP_BONE_ANIMATOR_COMPONENT` flag and hand-writes
  `GetEditableProperties`. Compile-flag it out or update its override
  to match the new signature — decide at Phase 4 time.
- Touching anything under `KraftonEngine/Source/Game/`. Game-side
  classes are still on `DECLARE_CLASS` and don't expose UPROPERTYs;
  they're unaffected.

## Anchor decision: `FProperty` is pure schema

Today `FProperty` mixes schema and instance binding. `ValuePtr` is
stamped per access by [`UObject::GetEditableProperties`](../KraftonEngine/Source/Engine/Object/Object.cpp)
(value-copies the schema from `UClass`, sets `ValuePtr = this + Offset`).
That value-copy is what blocks polymorphism — slicing.

After:
```cpp
class FProperty {
public:
    FString Name;
    FString Category;
    uint32  PropertyFlag    = CPF_None;
    uint32  ElementSize     = 0;
    uint32  Offset_Internal = 0;

    virtual ~FProperty() = default;
    virtual EPropertyType GetType() const = 0;
    virtual json::JSON Serialize(const void* Instance) const = 0;
    virtual void Deserialize(void* Instance, const json::JSON& Value) const = 0;

    void*       ContainerPtrToValuePtr(void* Container) const;
    const void* ContainerPtrToValuePtr(const void* Container) const;

    FProperty(const FProperty&)            = delete;
    FProperty& operator=(const FProperty&) = delete;
protected:
    FProperty(...common-base-args...);
};
```

`ValuePtr` is gone from the base. The deleted copy ctor makes slicing a
compile error rather than a runtime crash.

## Surveyed fanout (concrete sites)

`ValuePtr` consumers (must switch to `ContainerPtrToValuePtr(Container)`):

| File | Sites | Notes |
|---|---|---|
| [Source/Editor/UI/EditorPropertyWidget.cpp](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp) | ~35 | The biggest single piece. `RenderPropertyWidget` gets a `void* Container` arg threaded through the recursion. Array / Struct recursion already pass a different container — see "Container threading" below. |
| [Source/Engine/Core/Property/PropertyTypes.cpp](../KraftonEngine/Source/Engine/Core/Property/PropertyTypes.cpp) | 4 | Whole file becomes per-subclass virtual overrides. |
| [Source/Engine/Object/Object.cpp](../KraftonEngine/Source/Engine/Object/Object.cpp) | 3 | `GetAllProperties` / `GetEditableProperties` / `GetNonTransientProperties` stop stamping `ValuePtr`; instead return `TArray<const FProperty*>&` (or pass through `UClass`). |
| [Source/Engine/Object/UClass.cpp](../KraftonEngine/Source/Engine/Object/UClass.cpp) | 4 | `GetEditablePropertiesFor` / `GetNonTransientPropertiesFor` switch to pointer-array output. Comment block at top loses its rationale. |
| [Source/Engine/Core/CollisionTypes.h](../KraftonEngine/Source/Engine/Core/CollisionTypes.h) | 1 | `FCollisionResponseContainer::DescribeProperties` → cached schema (see "Struct describer" below). |
| [Scripts/GenerateCode.py](../Scripts/GenerateCode.py) | 1 | `Desc.ValuePtr = &Struct->{p.name};` line is dropped along with the rest of the per-field emission switch. |

Type-specific field readers (`.Min`, `.Max`, `.Speed`, `.EnumNames`,
`.EnumCount`, `.EnumSize`, `.StructFunc`, `.Inner`, `.Accessor`):

| File | Sites | Migration |
|---|---|---|
| [Source/Editor/UI/EditorPropertyWidget.cpp](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp) | ~25 | Switch on `Prop.GetType()` stays; each case `static_cast<const FFooProperty&>(Prop)` to read type-specific fields. |
| [Source/Engine/Core/Property/PropertyTypes.cpp](../KraftonEngine/Source/Engine/Core/Property/PropertyTypes.cpp) | many | Becomes per-subclass virtual override body — no downcast needed. |
| [Source/Engine/Object/UClass.h:36-40](../KraftonEngine/Source/Engine/Object/UClass.h#L36-L40) | 1 | Explicit `delete Properties[i]->Inner` block goes away. `FArrayProperty`'s `unique_ptr<FProperty>` owns Inner. |
| [Source/Engine/Core/CollisionTypes.h](../KraftonEngine/Source/Engine/Core/CollisionTypes.h) | 4 | Hand-written; rewrite to construct `FEnumProperty` objects into a static cache. |

## The hierarchy

Base: `FProperty` (abstract). Concrete subclasses:

- **Trivial** (no extra fields beyond base + value semantics on `Container + Offset`):
  `FBoolProperty`, `FByteBoolProperty`, `FIntProperty`, `FStringProperty`,
  `FNameProperty`, `FVec3Property`, `FVec4Property`, `FRotatorProperty`,
  `FColor4Property`, `FScriptProperty`, `FMaterialSlotProperty`,
  `FStaticMeshRefProperty`, `FSkeletalMeshRefProperty`,
  `FSceneComponentRefProperty`.
- **`FFloatProperty`**: + `float Min, Max, Speed`.
- **`FEnumProperty`**: + `const char** EnumNames`, `uint32 EnumCount`, `uint32 EnumSize`.
- **`FStructProperty`**: + `FStructPropertySchemaFn SchemaFn` (see below).
- **`FArrayProperty`**: + `std::unique_ptr<FProperty> Inner` (owned),
  + `FArrayAccessor* Accessor` (static, not owned).

`EPropertyType` enum **stays** — it remains the dispatch tag for the
editor's switch. Each subclass `GetType()` returns its tag.

## Struct describer becomes a cached schema

Today `FStructPropertyFunc` signature is
`void(*)(void* StructPtr, std::vector<FProperty>& OutProps)` — it
allocates `ValuePtr`-bound `FProperty` copies on every call. Under
Option B, the schema is instance-independent, so we cache it once:

```cpp
using FStructPropertySchemaFn = const std::vector<FProperty*>& (*)();
```

Generated USTRUCTs emit a static lazy-init schema function (replacing
the current `DescribeProperties(void*, vector<FProperty>&)`). Editor /
serializer iterate the schema, supply the struct instance pointer for
each child via `child->ContainerPtrToValuePtr(StructInstance)`.

Hand-written `FCollisionResponseContainer::DescribeProperties` follows
the same shape — its child set is compile-time constant despite the
loop, so it caches just fine.

## UClass API change

Current (value-copy):
```cpp
void GetAllProperties(TArray<FProperty>& OutProps) const;
void GetEditableProperties(TArray<FProperty>& OutProps) const;
void GetNonTransientProperties(TArray<FProperty>& OutProps) const;
```

After (pointer accumulator — schemas are owned by `UClass`):
```cpp
void GetAllProperties(TArray<const FProperty*>& OutProps) const;
void GetEditableProperties(TArray<const FProperty*>& OutProps) const;
void GetNonTransientProperties(TArray<const FProperty*>& OutProps) const;
```

Callers that previously read `Prop.ValuePtr` now read
`Prop->ContainerPtrToValuePtr(Owner)` with `Owner` being the `UObject*`
(top-level), array-element ptr, or struct-instance ptr depending on
context.

`UClass::~UClass` simplifies to:
```cpp
~UClass() {
    for (FProperty* P : Properties) delete P;
    Properties.clear();
}
```
The explicit `Inner` cleanup at
[UClass.h:36-40](../KraftonEngine/Source/Engine/Object/UClass.h#L36-L40)
disappears — `FArrayProperty`'s `unique_ptr` handles it.

## Container threading in EditorPropertyWidget

`RenderPropertyWidget` recurses for arrays and structs. The signature
needs the container threaded through:

```cpp
bool RenderPropertyWidget(
    const TArray<const FProperty*>& Props,
    int32& Index,
    void* Container,
    bool bNotifyPostEdit = true);
```

Recursion semantics:

- **Top-level call** from the inspector: `Container = SelectedObject`
  (the `UObject*` being inspected).
- **Array element**: `Container = Acc->GetAt(ArrayInstance, i)`. The
  inner schema has `Offset_Internal = 0`, so
  `Inner->ContainerPtrToValuePtr(ElemContainer)` returns `ElemContainer`.
- **Struct child**: `Container = StructProperty->ContainerPtrToValuePtr(ParentContainer)`.
  Each child uses its own `Offset_Internal` against that.

The current code's `Tmp.push_back(*Prop.Inner); Elem.ValuePtr = …`
gymnastics at [EditorPropertyWidget.cpp:1635-1640](../KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp#L1635-L1640)
disappears — replaced by passing the element pointer as `Container` to
the recursive call.

## Codegen changes

[`emit_property_type_metadata`](../Scripts/GenerateCode.py#L685) and its
two callers collapse. Per property, codegen emits one allocation:

```cpp
Cls->AddProperty(new FFloatProperty(
    "FOV", "Camera", CPF_Edit,
    offsetof(ThisClass, FOV), sizeof(((ThisClass*)0)->FOV),
    /*Min=*/0.1f, /*Max=*/3.14f, /*Speed=*/0.01f));
```

Centralize constructor-arg formatting in a helper that both the class
registrar emitter and the struct schema emitter use. The struct
emitter additionally wraps the constructions in a lazy-init lambda
returning `const std::vector<FProperty*>&`.

`STRUCT_MACRO_TEMPLATE` changes from:
```cpp
static void DescribeProperties(void* Ptr, std::vector<FProperty>& OutProps);
```
to:
```cpp
static const std::vector<FProperty*>& GetSchema();
```

Generated output will not be byte-identical with the previous run.
Expect a single large regen on the first build after this lands.

## Phased execution

The phases are sequencing inside the single PR, not separately
shippable increments.

### Phase 1 — Hierarchy in place (foundation)
1. Add `virtual ~FProperty()`, `GetType()`, `ContainerPtrToValuePtr`,
   delete copy ctor in [PropertyTypes.h](../KraftonEngine/Source/Engine/Core/Property/PropertyTypes.h).
2. Update `FStructPropertySchemaFn` typedef.
3. Strip the value-copy comment block at lines 80-84; replace with the
   schema/instance one-liner.

### Phase 2 — `UClass` and `UObject` move to pointer arrays
1. Change `UClass::Get{All,Editable,NonTransient}Properties` return
   shape (header + impl).
2. Simplify `UClass::~UClass`.
3. Change `UObject::Get{All,Editable,NonTransient}Properties` shape;
   stop stamping `ValuePtr`.

### Phase 3 — `PropertyTypes.cpp` becomes virtual dispatch
1. Each subclass implements `Serialize(const void* Instance)` and
   `Deserialize(void* Instance, const json::JSON&)`.
2. Recursive cases (Array, Struct) pass the element/child instance
   pointer through `ContainerPtrToValuePtr`.

### Phase 4 — `FCollisionResponseContainer` migrates
1. Replace `DescribeProperties` with `GetSchema()` returning a cached
   static `vector<FProperty*>`.
2. Construct one `FEnumProperty` per active channel.

### Phase 5 — Codegen
1. Update `emit_property_type_metadata` + struct emitter +
   `STRUCT_MACRO_TEMPLATE`.
2. Regenerate all `.gen.cpp` and `.generated.h`. Expect ~30 files
   changed.

### Phase 6 — `EditorPropertyWidget.cpp`
1. Thread `void* Container` through `RenderPropertyWidget` (and any
   helpers that read `ValuePtr`).
2. Each `case` downcasts via `static_cast<const FFooProperty&>(Prop)`
   to reach Min/Max/Speed/EnumNames/etc.
3. Array element: pass `Acc->GetAt(ArrPtr, i)` as `Container` to the
   recursive call instead of constructing a `Tmp` array with a stamped
   `ValuePtr`.
4. Struct: call `Prop->SchemaFn()` once, then iterate schema with the
   struct-instance pointer.

### Phase 7 — Audit + smoke test
1. Grep for any surviving `.ValuePtr` references; expect zero.
2. Compile-flag-off or fix `UTemporaryBoneAnimatorComponent`.
3. Build x64 Debug.
4. Scene round-trip: load a scene with a float-with-range
   (`CameraComponent`), an enum (`InterpToMovementComponent`), a
   struct (`CameraComponent::CameraState`), an array
   (`StaticMeshComponent::MaterialSlots`), and an asset ref. Edit each,
   save, reload, confirm round-trip.
5. Inspect `FCollisionResponseContainer` in a
   `PrimitiveComponent`'s details panel; confirm enum dropdowns still
   render and edits still persist.

## Risks

- **Schema cache leaks at process exit.** The lambda-init
  `static const std::vector<FProperty*>` for each USTRUCT never frees
  its contents. Matches the existing `UClass` leak — acceptable for an
  editor binary, document it.
- **Largest single thread of changes is `EditorPropertyWidget.cpp`.**
  The recursive `Container` arg ripples through every case. Do this
  phase only after codegen is stable so you're chasing real signature
  changes, not regen noise.
- **`UTemporaryBoneAnimatorComponent` hand-writes `GetEditableProperties`.**
  Under the new signature it won't compile. Flag-off the file is the
  fastest unblock; proper migration to UCLASS/UPROPERTY is a separate
  task.
- **JSON serialization shape must not change.** Field names and value
  layout stay identical so previously-saved scenes load unchanged.
  Verify with the smoke test, not just by reading code.

## Estimate

Roughly 3 focused days end-to-end. Phase 6 (editor widget) is the
longest single phase. Phases 1–5 can finish in one day; Phase 6 +
Phase 7 is the second day plus polish.

## What this unblocks

`FObjectProperty` (a generic `UObject*` reflection property
subsuming the special-cased `StaticMeshRef` / `SkeletalMeshRef` /
`SceneComponentRef` slots) is a clean addition once the hierarchy
exists. `TObjectPtr<T>` + GC remains a separate, much larger
decision after that.
