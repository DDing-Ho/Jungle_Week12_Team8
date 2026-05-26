# Cascade Particle System 구현 상태 검토 보고서

검토 기준:
- 코드상 선언만으로 구현 완료로 판단하지 않고, 실제 실행 경로에서 사용되는지 확인했다.
- 런타임, Particle Editor, 저장/로드 경로를 분리해서 평가했다.
- Unreal Cascade 원본 모듈 구성은 Epic 문서 기준으로 대조했다.

참고 문서:
- [Particle System Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/particle-system-reference?application_version=4.27)
- [Location Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/location-modules?application_version=4.27)
- [Color Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/color-modules?application_version=4.27)
- [Size Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/size-modules?application_version=4.27)
- [Acceleration Modules](https://dev.epicgames.com/documentation/unreal-engine/acceleration-modules?application_version=4.27)
- [Attractor Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/attractor-modules?application_version=4.27)
- [Beam Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/beam-modules?application_version=4.27)

## 프로젝트 구현 모듈

### Core / Spawn

| 모듈명 | 런타임 구현 상태 | 런타임 동작 여부 | Editor 노출 여부 | Editor 편집 가능 여부 | 저장/로드 지원 | 근거 코드 위치 | 비고 |
|:---|:---:|:---:|:---:|:---:|:---:|:---|:---|
| Required | 부분 구현 | 부분 구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:83`<br>`ParticleEmitterInstance.cpp:270`<br>`ParticleSystemEditorWidget.cpp:966` | Material은 렌더 경로에 반영됨. `SortMode`, `TranslucencySortPriority`는 실제 정렬/우선순위 적용 경로 확인 안 됨. `EmitterType`은 런타임에서 Required가 아니라 TypeData를 봄. |
| Spawn | 부분 구현 | 부분 구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleEmitterInstance.cpp:197`<br>`ParticleModules.cpp:112` | `SpawnRate`는 동작. `BurstCount`는 저장/편집 가능하지만 런타임에서 주석 처리되어 미동작. |
| Lifetime | 구현됨 | 구현됨 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:169`<br>`ParticleEmitterInstance.cpp:67`<br>`ParticleSystemEditorWidget.cpp:817` | 현재 기준 가장 연결이 완성된 모듈. |
| Location | 부분 구현 | 부분 구현 | 구현됨 | 부분 구현 | 구현됨 | `ParticleModules.cpp:198`<br>`ParticleSystemEditorWidget.cpp:833` | `LocationDist`가 있으면 적용됨. 새로 추가한 모듈은 Distribution이 null일 수 있음. `SphereRadius`, `CylinderRadius`, `CylinderHeight`는 저장/편집만 되고 Spawn 로직에서 미사용. |
| Velocity | 부분 구현 | 부분 구현 | 구현됨 | 부분 구현 | 구현됨 | `ParticleModules.cpp:225`<br>`ParticleSystemEditorWidget.cpp:854` | `VelocityDist`가 있으면 적용됨. 새 기본 에셋/새 모듈에서 Distribution null 문제가 있어 편집/저장 전후 기본값이 달라질 가능성 있음. |
| Color | 부분 구현 | 부분 구현 | 구현됨 | 부분 구현 | 구현됨 | `ParticleModules.cpp:254`<br>`ParticleSystemEditorWidget.cpp:875` | Initial Color와 Color Over Life가 하나의 모듈처럼 동작. Scale Color/Life는 없음. Distribution null 초기화 이슈 있음. |
| Size | 부분 구현 | 부분 구현 | 구현됨 | 부분 구현 | 구현됨 | `ParticleModules.cpp:308`<br>`ParticleSystemEditorWidget.cpp:897` | Size Over Life는 절대값 세팅 방식. Cascade의 Size By Life/Scale/Speed 계열은 미구현. Distribution null 초기화 이슈 있음. |

### Motion

| 모듈명 | 런타임 구현 상태 | 런타임 동작 여부 | Editor 노출 여부 | Editor 편집 가능 여부 | 저장/로드 지원 | 근거 코드 위치 | 비고 |
|:---|:---:|:---:|:---:|:---:|:---:|:---|:---|
| Rotation | 미구현 | 미구현 | 구현됨 | 부분 구현 | 구현됨 | `ParticleModules.cpp:354`<br>`ParticleSystemEditorWidget.cpp:918` | 함수는 있지만 실제 회전 값을 Particle에 쓰지 않음. |
| Rotation Rate | 부분 구현 | 부분 구현 | 구현됨 | 부분 구현 | 구현됨 | `ParticleModules.cpp:379`<br>`ParticleRenderData.cpp:9` | Sprite 회전은 반영됨. Mesh 회전 속도 필드는 저장/편집 가능하나 Mesh 경로 미완성. |
| Acceleration | 미구현 | 미구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:401`<br>`ParticleEmitterInstance.cpp:67` | Spawn/Update override 없음. 속도에 가속도 적용 안 됨. |
| Attractor | 미구현 | 미구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:410` | Target/Strength/Radius 저장만 됨. |
| Orbit | 미구현 | 미구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:418` | 렌더 위치 오프셋/궤도 회전 적용 없음. |

### Collision / Event / Expression

| 모듈명 | 런타임 구현 상태 | 런타임 동작 여부 | Editor 노출 여부 | Editor 편집 가능 여부 | 저장/로드 지원 | 근거 코드 위치 | 비고 |
|:---|:---:|:---:|:---:|:---:|:---:|:---|:---|
| Collision | 미구현 | 미구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:429`<br>`ParticleEmitterInstance.cpp:352` | 충돌 검사, bounce/friction, collision event 모두 미연결. |
| Kill | 미구현 | 미구현 | 구현됨 | 부분 구현 | 구현됨 | `ParticleModules.cpp:438` | Lifetime 만료 kill은 공통 로직이고 Kill 모듈 조건은 적용 안 됨. `KillBox`는 UPROPERTY가 아니라 Editor 편집 불확실. |
| Event Generator | 미구현 | 미구현 | 구현됨 | 부분 구현 | 구현됨 | `ParticleModules.cpp:451`<br>`ParticleEmitterInstance.cpp:352` | `GeneratedEvents`는 UPROPERTY가 아니라 일반 Details 편집 불가 가능성이 큼. |
| Event Receiver Spawn | 미구현 | 미구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:457` | Event 수신/스폰 처리 없음. |
| Event Receiver Kill All | 미구현 | 미구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:466` | Event 수신/전체 제거 처리 없음. |
| SubUV | 미구현 | 미구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:478`<br>`ParticleSprite.hlsl:23` | `SubImageIndex` 필드는 있으나 모듈이 값을 세팅하지 않고 렌더러/셰이더도 atlas frame을 사용하지 않음. |
| Light | 미구현 | 미구현 | 구현됨 | 부분 구현 | 구현됨 | `ParticleModules.cpp:488` | Particle Light 생성/렌더 연동 없음. `LightColor`는 UPROPERTY가 아니라 편집 불확실. |
| Vector Field | 미구현 | 미구현 | 구현됨 | 부분 구현 | 부분 구현 | `ParticleModules.cpp:496` | Asset 참조는 placeholder만 저장. 이동 적용 없음. |
| Camera | 미구현 | 미구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:505` | Camera offset 적용 없음. |
| Parameter | 미구현 | 미구현 | 구현됨 | 구현됨 | 구현됨 | `ParticleModules.cpp:511` | Material/dynamic parameter 전달 경로 없음. |

### TypeData

| 모듈명 | 런타임 구현 상태 | 런타임 동작 여부 | Editor 노출 여부 | Editor 편집 가능 여부 | 저장/로드 지원 | 근거 코드 위치 | 비고 |
|:---|:---:|:---:|:---:|:---:|:---:|:---|:---|
| TypeData Sprite | 구현됨 | 구현됨 | 구현됨 | 확인 불가 | 구현됨 | `ParticleSystemComponent.cpp:159`<br>`ParticleRenderData.cpp:9`<br>`ParticleSceneProxy.cpp:190` | 추가 설정값이 거의 없어 편집 가능 여부는 실질적으로 해당 없음. |
| TypeData Mesh | 부분 구현 | 미구현 | 부분 구현 | 미구현 | 구현됨 | `ParticleEmitterInstance.cpp:382`<br>`ParticleRenderData.cpp:53`<br>`ParticleSceneProxy.cpp:218` | 직렬화/런타임 데이터 일부만 있음. 실제 렌더링 안 됨. Editor에서 TypeData 변경/mesh 선택 UI 없음. |
| TypeData Beam | 미구현 | 미구현 | 부분 구현 | 부분 구현 | 구현됨 | `ParticleModules.cpp:539`<br>`ParticleBeam.hlsl:1`<br>`ParticleRenderData.cpp:66` | TypeData가 있어도 base emitter로 생성되어 Beam replay/render로 이어지지 않음. |
| TypeData Ribbon | 미구현 | 미구현 | 부분 구현 | 부분 구현 | 구현됨 | `ParticleModules.cpp:548`<br>`ParticleRibbon.hlsl:1`<br>`ParticleRenderData.cpp:79` | Trail strip 생성/소스 모듈/렌더링 모두 없음. |

## Unreal 원본 대비 누락 모듈

| 모듈명 | 런타임 구현 상태 | 런타임 동작 여부 | Editor 노출 여부 | Editor 편집 가능 여부 | 저장/로드 지원 | 근거 코드 위치 | 비고 |
|:---|:---:|:---:|:---:|:---:|:---:|:---|:---|
| Unreal 원본 Spawn Per Unit | 미구현 | 미구현 | 미구현 | 미구현 | 미구현 | `ParticleTypes.h:117`<br>Epic Spawn Modules 문서 | 이동 거리 기반 spawn 없음. |
| Unreal 원본 Location 세부 모듈군 | 미구현 | 미구현 | 미구현 | 미구현 | 미구현 | `ParticleTypes.h:119`<br>Epic Location Modules 문서 | World Offset, Bone/Socket, Direct, Emitter Init/Direct, Cylinder/Sphere Seeded, Triangle, Skel Vert/Surf, Source Movement 없음. |
| Unreal 원본 Velocity 세부 모듈군 | 미구현 | 미구현 | 미구현 | 미구현 | 미구현 | `ParticleTypes.h:119` | Velocity Cone, Inherit Parent, Velocity Over Life, Seeded 등 없음. |
| Unreal 원본 Size 세부 모듈군 | 부분 구현 | 부분 구현 | 부분 구현 | 부분 구현 | 부분 구현 | `ParticleModules.cpp:308`<br>Epic Size Modules 문서 | Initial Size에 가까운 기능만 있음. Size By Life/Scale/Speed/Seeded는 없음. |
| Unreal 원본 Color 세부 모듈군 | 부분 구현 | 부분 구현 | 부분 구현 | 부분 구현 | 부분 구현 | `ParticleModules.cpp:254`<br>Epic Color Modules 문서 | Initial Color/Color Over Life 일부를 단일 모듈로 처리. Scale Color/Life, Seeded는 없음. |
| Unreal 원본 Acceleration 세부 모듈군 | 미구현 | 미구현 | 부분 구현 | 부분 구현 | 부분 구현 | `ParticleModules.cpp:401`<br>Epic Acceleration Modules 문서 | Acceleration, Const Acceleration, Drag, Drag Scale/Life, Acceleration Over Life 모두 실제 update 없음. |
| Unreal 원본 Attractor 세부 모듈군 | 미구현 | 미구현 | 부분 구현 | 부분 구현 | 부분 구현 | `ParticleModules.cpp:410`<br>Epic Attractor Modules 문서 | Line/Particle/Point/Point Gravity 없음. |
| Unreal 원본 Beam 모듈군 | 미구현 | 미구현 | 미구현 | 미구현 | 미구현 | Epic Beam Modules 문서<br>`ParticleTypes.h:117` | Beam Modifier, Noise, Source, Target 없음. |
| Unreal 원본 Orientation / Axis Lock / Pivot / Mesh Material / Trail Source / GPU TypeData | 미구현 | 미구현 | 미구현 | 미구현 | 미구현 | `ParticleTypes.h:117` | 프로젝트 enum/class에 대응 항목 없음. |

## 요약

### 1. 완전히 구현된 모듈 목록

- `Lifetime`
- `TypeData Sprite`
  - 단, `TypeData Sprite`는 별도 편집값이 거의 없다는 조건부 평가다.

### 2. 런타임만 구현된 모듈 목록

- 뚜렷하게 없음.
- 대부분 Editor/Serialize 경로와 함께 있거나, 런타임이 부분 구현 상태다.

### 3. Editor만 구현된 모듈 목록

- `Acceleration`
- `Attractor`
- `Orbit`
- `Collision`
- `Kill`
- `Event Generator`
- `Event Receiver Spawn`
- `Event Receiver Kill All`
- `SubUV`
- `Light`
- `Vector Field`
- `Camera`
- `Parameter`

위 모듈들은 Editor 노출 및 저장/로드 경로는 있으나, 실제 런타임 효과가 없거나 거의 없다.

### 4. 런타임과 Editor 모두 미구현인 모듈 목록

- Unreal 원본 `Spawn Per Unit`
- Unreal 원본 Location 세부 모듈군
  - `World Offset`
  - `Bone/Socket Location`
  - `Direct Location`
  - `Emitter Init Loc`
  - `Emitter Direct Loc`
  - `Cylinder (Seeded)`
  - `Sphere (Seeded)`
  - `Triangle`
  - `Skeletal Mesh Vertex/Surface Location`
  - `Source Movement`
- Unreal 원본 Velocity 세부 모듈군
  - `Velocity Cone`
  - `Inherit Parent`
  - `Velocity Over Life`
  - Seeded 계열
- Unreal 원본 Beam 모듈군
  - `Beam Modifier`
  - `Beam Noise`
  - `Beam Source`
  - `Beam Target`
- Unreal 원본 Orientation / Axis Lock / Pivot / Mesh Material / Trail Source / GPU TypeData 계열

### 5. 우선적으로 구현 또는 수정해야 할 항목

1. `Lifetime` 외 Distribution 기반 모듈의 null 초기화 문제 수정
   - `Velocity`, `Location`, `Color`, `Size`, `Rotation`, `RotationRate`는 새 모듈 추가 직후 Distribution 객체가 null일 수 있다.
   - Editor에서 값 편집이 막히거나, 저장/로드 후 기본값이 달라질 가능성이 있다.

2. `Spawn.BurstCount` 구현 또는 UI 숨김
   - 현재 저장/편집은 되지만 런타임에서 주석 처리되어 동작하지 않는다.

3. `Acceleration`, `Collision`, `Kill`, `Event`, `SubUV` 런타임 연결
   - Editor에서 노출되는 핵심 모듈이 실제 효과를 내지 않기 때문에 사용자 혼란 가능성이 크다.

4. TypeData 전환 UI와 Mesh/Beam/Ribbon 렌더 경로 구현
   - `TypeData Mesh/Beam/Ribbon` 클래스와 Serialize는 있으나 실제 렌더 경로가 미완성이다.
   - `ParticleSceneProxy::UpdatePerViewport()`가 Sprite가 아닌 emitter를 건너뛰고 있다.

5. Required 모듈의 적용 범위 정리
   - `Material`은 반영되지만 `SortMode`, `TranslucencySortPriority`, Required의 `EmitterType`은 실제 동작과 UI 기대가 맞지 않을 수 있다.

### 6. 검토 중 확인이 어려웠던 부분과 추가 확인 방법

- 실제 에디터 실행, 저장 후 재오픈, 렌더 프레임 캡처는 수행하지 않았다.
- 추가 확인 방법:
  - 새 `ParticleSystem` 생성
  - 각 모듈 추가 및 값 변경
  - Save
  - Load
  - Preview/Level 배치
  - 런타임 파티클 데이터와 렌더 데이터가 바뀌는지 자동 검증
- 테스트로는 다음 항목을 권장한다.
  - `FParticleSystemAssetManager` 저장/로드 round-trip 테스트
  - `UParticleLODLevel::CacheModules()` 결과 검증
  - `FParticleEmitterInstance::Tick()` 후 particle state 검증
  - `FParticleSceneProxy` 렌더 데이터 staging 검증
