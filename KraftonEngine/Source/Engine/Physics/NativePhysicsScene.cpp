#include "Physics/NativePhysicsScene.h"
#include "Collision/CollisionMath.h"
#include "Component/PrimitiveComponent.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"

#include <algorithm>

void FNativePhysicsScene::Initialize(UWorld* InWorld)
{
	World = InWorld;
}

void FNativePhysicsScene::Shutdown()
{
	RegisteredComponents.clear();
	BodyStates.clear();
	PreviousOverlaps.clear();
	CurrentOverlaps.clear();
	PreviousBlockPairs.clear();
	CurrentBlockPairs.clear();
	World = nullptr;
}

void FNativePhysicsScene::RegisterComponent(UPrimitiveComponent* Comp)
{
	if (!Comp) return;

	for (UPrimitiveComponent* Existing : RegisteredComponents)
	{
		if (Existing == Comp) return;
	}
	RegisteredComponents.push_back(Comp);
	FBodyState& State = BodyStates[Comp];
	State.Mass = Comp->GetMass() > 0.0f ? Comp->GetMass() : 1.0f;
	State.CenterOfMassLocal = Comp->GetCenterOfMass();
}

void FNativePhysicsScene::RebuildBody(UPrimitiveComponent* Comp)
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return; // 등록 안 됨 — skip
	// Native는 SimulatePhysics/ObjectType/Response를 매 Tick에서 컴포넌트로부터 직접 읽으므로
	// BodyState의 Mass/COM만 갱신.
	It->second.Mass = (Comp->GetMass() > 0.0f) ? Comp->GetMass() : 1.0f;
	It->second.CenterOfMassLocal = Comp->GetCenterOfMass();
}

void FNativePhysicsScene::UnregisterComponent(UPrimitiveComponent* Comp)
{
	if (!Comp) return;

	auto It = std::find(RegisteredComponents.begin(), RegisteredComponents.end(), Comp);
	if (It == RegisteredComponents.end()) return;
	RegisteredComponents.erase(It);
	BodyStates.erase(Comp);

	// PreviousOverlaps에서 이 컴포넌트를 포함하는 쌍 제거 + EndOverlap 발화
	auto PairIt = PreviousOverlaps.begin();
	while (PairIt != PreviousOverlaps.end())
	{
		if (PairIt->A == Comp || PairIt->B == Comp)
		{
			UPrimitiveComponent* Other = (PairIt->A == Comp) ? PairIt->B : PairIt->A;

			if (Other->GetGenerateOverlapEvents())
			{
				AActor* CompOwner = Comp->GetOwner();
				Other->NotifyComponentEndOverlap(Other, CompOwner, Comp, 0);
			}

			PairIt = PreviousOverlaps.erase(PairIt);
		}
		else
		{
			++PairIt;
		}
	}

	// CurrentOverlaps에서도 제거
	auto CurIt = CurrentOverlaps.begin();
	while (CurIt != CurrentOverlaps.end())
	{
		if (CurIt->A == Comp || CurIt->B == Comp)
			CurIt = CurrentOverlaps.erase(CurIt);
		else
			++CurIt;
	}

	// BlockPairs에서도 제거
	auto EraseFromSet = [Comp](std::unordered_set<FOverlapPair>& Set) {
		auto It = Set.begin();
		while (It != Set.end())
		{
			if (It->A == Comp || It->B == Comp)
				It = Set.erase(It);
			else
				++It;
		}
	};
	EraseFromSet(PreviousBlockPairs);
	EraseFromSet(CurrentBlockPairs);
}

void FNativePhysicsScene::Tick(float DeltaTime)
{
	if (!World) return;

	// ── 힘 적분 + 중력: bSimulatePhysics인 컴포넌트에 적용 ──
	for (UPrimitiveComponent* Comp : RegisteredComponents)
	{
		if (!Comp->GetSimulatePhysics()) continue;

		FBodyState& State = BodyStates[Comp];

		// 외부 힘/토크 적분
		float InvMass = (State.Mass > 0.0f) ? (1.0f / State.Mass) : 0.0f;
		State.Velocity = State.Velocity + State.AccumulatedForce * InvMass * DeltaTime;
		State.AngularVelocity = State.AngularVelocity + State.AccumulatedTorque * InvMass * DeltaTime;

		// 중력
		State.Velocity.Z += GravityZ * DeltaTime;

		FVector Pos = Comp->GetWorldLocation();
		Pos = Pos + State.Velocity * DeltaTime;
		Comp->SetWorldLocation(Pos);

		// 프레임 끝 누적값 초기화
		State.AccumulatedForce = { 0, 0, 0 };
		State.AccumulatedTorque = { 0, 0, 0 };
	}

	CurrentOverlaps.clear();
	CurrentBlockPairs.clear();

	// Brute-force O(N²)
	const int32 Count = static_cast<int32>(RegisteredComponents.size());
	for (int32 i = 0; i < Count; ++i)
	{
		for (int32 j = i + 1; j < Count; ++j)
		{
			UPrimitiveComponent* A = RegisteredComponents[i];
			UPrimitiveComponent* B = RegisteredComponents[j];

			if (A->GetOwner() == B->GetOwner()) continue;

			ECollisionResponse Resp = UPrimitiveComponent::GetMinResponse(A, B);
			if (Resp == ECollisionResponse::Ignore) continue;

			// Broad-phase: AABB
			FBoundingBox BoundsA = A->GetWorldBoundingBox();
			FBoundingBox BoundsB = B->GetWorldBoundingBox();
			if (!FCollisionMath::AABBvsAABB(BoundsA.Min, BoundsA.Max, BoundsB.Min, BoundsB.Max))
				continue;

			// Narrow-phase
			FHitResult Hit;
			if (!FCollisionMath::TestComponentPair(A, B, Hit))
				continue;

			if (Resp == ECollisionResponse::Block)
			{
				CurrentBlockPairs.insert(FOverlapPair{ A, B });

				// Hit 이벤트는 첫 접촉 시에만 발화 (PhysX eNOTIFY_TOUCH_FOUND 방식)
				if (PreviousBlockPairs.find(FOverlapPair{ A, B }) == PreviousBlockPairs.end())
				{
					FVector NormalImpulse = Hit.ImpactNormal * Hit.PenetrationDepth;

					FHitResult HitA = Hit;
					HitA.HitComponent = B;
					HitA.HitActor = B->GetOwner();
					A->NotifyComponentHit(A, B->GetOwner(), B, NormalImpulse, HitA);

					FHitResult HitB = Hit;
					HitB.HitComponent = A;
					HitB.HitActor = A->GetOwner();
					HitB.ImpactNormal = Hit.ImpactNormal * -1.0f;
					HitB.WorldNormal = Hit.WorldNormal * -1.0f;
					B->NotifyComponentHit(B, A->GetOwner(), A, NormalImpulse * -1.0f, HitB);
				}

				// ── Block 위치 보정 + 속도 보정 ──
				// TestComponentPair 내부에서 A/B가 swap될 수 있음.
				// Hit.ImpactNormal은 내부 A→B 방향. Hit.HitComponent가 내부 B.
				// 따라서 Hit.HitComponent == (우리의)B 면 Normal은 A→B — A를 밀 방향은 반대.
				//         Hit.HitComponent == (우리의)A 면 swap됐으므로 Normal은 B→A — A를 밀 방향은 그대로.
				bool bASimulates = A->GetSimulatePhysics();
				bool bBSimulates = B->GetSimulatePhysics();
				FVector PushA; // A를 밀어내는 방향 (B에서 멀어지는 방향)
				if (Hit.HitComponent == B)
					PushA = Hit.ImpactNormal * -1.0f;
				else
					PushA = Hit.ImpactNormal;
				FVector Normal = PushA;
				float Depth = Hit.PenetrationDepth;

				// Baumgarte stabilization + slop
				constexpr float Slop = 0.01f;
				constexpr float BaumgarteBeta = 0.2f;
				float CorrectionDepth = (std::max)(0.0f, Depth - Slop);

				if (CorrectionDepth > 0.0f)
				{
					float BiasVelocity = (BaumgarteBeta / DeltaTime) * CorrectionDepth;

					if (bASimulates && bBSimulates)
					{
						FVector Correction = Normal * (BiasVelocity * DeltaTime * 0.5f);
						A->SetWorldLocation(A->GetWorldLocation() + Correction);
						B->SetWorldLocation(B->GetWorldLocation() - Correction);
					}
					else if (bASimulates)
					{
						A->SetWorldLocation(A->GetWorldLocation() + Normal * (BiasVelocity * DeltaTime));
					}
					else if (bBSimulates)
					{
						B->SetWorldLocation(B->GetWorldLocation() - Normal * (BiasVelocity * DeltaTime));
					}
				}

				// 속도 반사: (1 + e) 로 반발계수 적용 (e=0 완전비탄성, e=1 완전탄성)
				constexpr float Restitution = 0.2f;
				if (bASimulates)
				{
					FBodyState& StateA = BodyStates[A];
					float VdotN = StateA.Velocity.X * Normal.X + StateA.Velocity.Y * Normal.Y + StateA.Velocity.Z * Normal.Z;
					if (VdotN < 0.0f)
						StateA.Velocity = StateA.Velocity - Normal * (VdotN * (1.0f + Restitution));
				}
				if (bBSimulates)
				{
					FBodyState& StateB = BodyStates[B];
					FVector NegNormal = Normal * -1.0f;
					float VdotN = StateB.Velocity.X * NegNormal.X + StateB.Velocity.Y * NegNormal.Y + StateB.Velocity.Z * NegNormal.Z;
					if (VdotN < 0.0f)
						StateB.Velocity = StateB.Velocity - NegNormal * (VdotN * (1.0f + Restitution));
				}
			}
			else if (Resp == ECollisionResponse::Overlap)
			{
				if (A->GetGenerateOverlapEvents() || B->GetGenerateOverlapEvents())
				{
					CurrentOverlaps.insert(FOverlapPair{ A, B });
				}
			}
		}
	}

	// Begin Overlap
	for (const FOverlapPair& Pair : CurrentOverlaps)
	{
		if (PreviousOverlaps.find(Pair) == PreviousOverlaps.end())
		{
			FHitResult DummyHit;

			if (Pair.A->GetGenerateOverlapEvents())
				Pair.A->NotifyComponentBeginOverlap(Pair.A, Pair.B->GetOwner(), Pair.B, 0, false, DummyHit);

			if (Pair.B->GetGenerateOverlapEvents())
				Pair.B->NotifyComponentBeginOverlap(Pair.B, Pair.A->GetOwner(), Pair.A, 0, false, DummyHit);
		}
	}

	// End Overlap
	for (const FOverlapPair& Pair : PreviousOverlaps)
	{
		if (CurrentOverlaps.find(Pair) == CurrentOverlaps.end())
		{
			if (Pair.A->GetGenerateOverlapEvents())
				Pair.A->NotifyComponentEndOverlap(Pair.A, Pair.B->GetOwner(), Pair.B, 0);

			if (Pair.B->GetGenerateOverlapEvents())
				Pair.B->NotifyComponentEndOverlap(Pair.B, Pair.A->GetOwner(), Pair.A, 0);
		}
	}

	// End Hit
	for (const FOverlapPair& Pair : PreviousBlockPairs)
	{
		if (CurrentBlockPairs.find(Pair) == CurrentBlockPairs.end())
		{
			Pair.A->NotifyComponentEndHit(Pair.A, Pair.B->GetOwner(), Pair.B);
			Pair.B->NotifyComponentEndHit(Pair.B, Pair.A->GetOwner(), Pair.A);
		}
	}

	PreviousOverlaps = CurrentOverlaps;
	PreviousBlockPairs = CurrentBlockPairs;
}

// ============================================================
// Force / Torque
// ============================================================

void FNativePhysicsScene::AddForce(UPrimitiveComponent* Comp, const FVector& Force)
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return;
	It->second.AccumulatedForce = It->second.AccumulatedForce + Force;
}

void FNativePhysicsScene::AddForceAtLocation(UPrimitiveComponent* Comp, const FVector& Force, const FVector& WorldLocation)
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return;

	It->second.AccumulatedForce = It->second.AccumulatedForce + Force;

	// COM = Comp의 world origin + 컴포넌트 회전 적용한 local offset
	FVector COMWorld = Comp->GetWorldLocation()
		+ Comp->GetWorldMatrix().ToQuat().RotateVector(It->second.CenterOfMassLocal);
	FVector R = WorldLocation - COMWorld;
	FVector Torque;
	Torque.X = R.Y * Force.Z - R.Z * Force.Y;
	Torque.Y = R.Z * Force.X - R.X * Force.Z;
	Torque.Z = R.X * Force.Y - R.Y * Force.X;
	It->second.AccumulatedTorque = It->second.AccumulatedTorque + Torque;
}

void FNativePhysicsScene::AddTorque(UPrimitiveComponent* Comp, const FVector& Torque)
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return;
	It->second.AccumulatedTorque = It->second.AccumulatedTorque + Torque;
}

// ============================================================
// Velocity
// ============================================================

FVector FNativePhysicsScene::GetLinearVelocity(UPrimitiveComponent* Comp) const
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return { 0, 0, 0 };
	return It->second.Velocity;
}

void FNativePhysicsScene::SetLinearVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return;
	It->second.Velocity = Vel;
}

FVector FNativePhysicsScene::GetAngularVelocity(UPrimitiveComponent* Comp) const
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return { 0, 0, 0 };
	return It->second.AngularVelocity;
}

void FNativePhysicsScene::SetAngularVelocity(UPrimitiveComponent* Comp, const FVector& Vel)
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return;
	It->second.AngularVelocity = Vel;
}

// ============================================================
// Mass
// ============================================================

void FNativePhysicsScene::SetMass(UPrimitiveComponent* Comp, float Mass)
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return;
	It->second.Mass = (Mass > 0.0f) ? Mass : 1.0f;
}

float FNativePhysicsScene::GetMass(UPrimitiveComponent* Comp) const
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return 1.0f;
	return It->second.Mass;
}

void FNativePhysicsScene::SetCenterOfMass(UPrimitiveComponent* Comp, const FVector& LocalOffset)
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return;
	It->second.CenterOfMassLocal = LocalOffset;
}

FVector FNativePhysicsScene::GetCenterOfMass(UPrimitiveComponent* Comp) const
{
	auto It = BodyStates.find(Comp);
	if (It == BodyStates.end()) return { 0, 0, 0 };
	return It->second.CenterOfMassLocal;
}

// ============================================================
// Raycast — brute-force AABB ray test
// ============================================================

bool FNativePhysicsScene::Raycast(const FVector& Start, const FVector& Dir, float MaxDist, FHitResult& OutHit,
	ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	// Inverse direction for slab test
	FVector InvDir;
	InvDir.X = (Dir.X != 0.0f) ? (1.0f / Dir.X) : 1e30f;
	InvDir.Y = (Dir.Y != 0.0f) ? (1.0f / Dir.Y) : 1e30f;
	InvDir.Z = (Dir.Z != 0.0f) ? (1.0f / Dir.Z) : 1e30f;

	float ClosestDist = MaxDist;
	bool bFound = false;

	for (UPrimitiveComponent* Comp : RegisteredComponents)
	{
		if (IgnoreActor && Comp->GetOwner() == IgnoreActor) continue;

		// Channel filter: 응답이 TraceChannel에 대해 Block이 아니면 skip
		// (overlap/ignore 응답인 trigger volume 등은 raycast 결과에서 제외)
		if (Comp->GetCollisionResponseToChannel(TraceChannel) != ECollisionResponse::Block) continue;

		FBoundingBox Box = Comp->GetWorldBoundingBox();

		// Ray-AABB slab test
		float tMin = (Box.Min.X - Start.X) * InvDir.X;
		float tMax = (Box.Max.X - Start.X) * InvDir.X;
		if (tMin > tMax) { float tmp = tMin; tMin = tMax; tMax = tmp; }

		float tyMin = (Box.Min.Y - Start.Y) * InvDir.Y;
		float tyMax = (Box.Max.Y - Start.Y) * InvDir.Y;
		if (tyMin > tyMax) { float tmp = tyMin; tyMin = tyMax; tyMax = tmp; }

		if ((tMin > tyMax) || (tyMin > tMax)) continue;
		if (tyMin > tMin) tMin = tyMin;
		if (tyMax < tMax) tMax = tyMax;

		float tzMin = (Box.Min.Z - Start.Z) * InvDir.Z;
		float tzMax = (Box.Max.Z - Start.Z) * InvDir.Z;
		if (tzMin > tzMax) { float tmp = tzMin; tzMin = tzMax; tzMax = tmp; }

		if ((tMin > tzMax) || (tzMin > tMax)) continue;
		if (tzMin > tMin) tMin = tzMin;

		if (tMin < 0.0f) tMin = 0.0f;
		if (tMin >= ClosestDist) continue;

		ClosestDist = tMin;
		bFound = true;

		OutHit.bHit = true;
		OutHit.Distance = tMin;
		OutHit.HitComponent = Comp;
		OutHit.HitActor = Comp->GetOwner();
		OutHit.WorldHitLocation = Start + Dir * tMin;

		// 박스의 어느 면을 때렸는지 찾아서 반사/마찰/충돌 이벤트에 쓸 법선을 만듦
		FVector Normal = FVector::ZeroVector;
		if (tMin == tzMin)
			Normal.Z = (Dir.Z > 0.0f) ? -1.0f : 1.0f;
		else if (tMin == tyMin)
			Normal.Y = (Dir.Y > 0.0f) ? -1.0f : 1.0f;
		else
			Normal.X = (Dir.X > 0.0f) ? -1.0f : 1.0f;
		OutHit.ImpactNormal  = Normal;
		OutHit.WorldNormal   = Normal;
		OutHit.ShapeLocation = OutHit.WorldHitLocation; // Raycast: shape 위치 = 표면 접촉점
	}

	return bFound;
}

// ============================================================
// SphereSweepApprox_ExpandedAABB — Minkowski sum: AABB를 Radius만큼 팽창 후 ray test
//
// Fast approximation of sphere sweep against AABB.
// Expands target AABB by Radius and raycasts the sphere center (Minkowski sum).
// Cheap and stable for particle collision, but conservative near edges/corners.
//
// WorldHitLocation : 표면 접촉점 (구 표면이 AABB 원본 면에 닿은 지점, ≈ SphereCenter - Normal * Radius)
// ShapeLocation    : 충돌 당시 구 중심 위치
// ============================================================

bool FNativePhysicsScene::SphereSweepApprox_ExpandedAABB(const FVector& Start, const FVector& End, float Radius, FHitResult& OutHit,
	ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	FVector Move = End - Start;
	float MoveDist = Move.X * Move.X + Move.Y * Move.Y + Move.Z * Move.Z;
	if (MoveDist < 1e-8f)
		return false;
	MoveDist = sqrtf(MoveDist);

	FVector Dir;
	Dir.X = Move.X / MoveDist;
	Dir.Y = Move.Y / MoveDist;
	Dir.Z = Move.Z / MoveDist;

	FVector InvDir;
	InvDir.X = (Dir.X != 0.0f) ? (1.0f / Dir.X) : 1e30f;
	InvDir.Y = (Dir.Y != 0.0f) ? (1.0f / Dir.Y) : 1e30f;
	InvDir.Z = (Dir.Z != 0.0f) ? (1.0f / Dir.Z) : 1e30f;

	float ClosestDist = MoveDist;
	bool bFound = false;

	for (UPrimitiveComponent* Comp : RegisteredComponents)
	{
		if (IgnoreActor && Comp->GetOwner() == IgnoreActor) continue;
		if (Comp->GetCollisionResponseToChannel(TraceChannel) != ECollisionResponse::Block) continue;

		FBoundingBox Box = Comp->GetWorldBoundingBox();

		// AABB를 Radius만큼 팽창 (Minkowski sum)
		// 팽창된 AABB에 대한 ray test = 구가 원본 AABB를 건드리는 시점 계산
		const float ExMin_X = Box.Min.X - Radius, ExMax_X = Box.Max.X + Radius;
		const float ExMin_Y = Box.Min.Y - Radius, ExMax_Y = Box.Max.Y + Radius;
		const float ExMin_Z = Box.Min.Z - Radius, ExMax_Z = Box.Max.Z + Radius;

		float txMin = (ExMin_X - Start.X) * InvDir.X;
		float txMax = (ExMax_X - Start.X) * InvDir.X;
		if (txMin > txMax) { float t = txMin; txMin = txMax; txMax = t; }

		float tyMin = (ExMin_Y - Start.Y) * InvDir.Y;
		float tyMax = (ExMax_Y - Start.Y) * InvDir.Y;
		if (tyMin > tyMax) { float t = tyMin; tyMin = tyMax; tyMax = t; }

		if (txMin > tyMax || tyMin > txMax) continue;

		float tzMin = (ExMin_Z - Start.Z) * InvDir.Z;
		float tzMax = (ExMax_Z - Start.Z) * InvDir.Z;
		if (tzMin > tzMax) { float t = tzMin; tzMin = tzMax; tzMax = t; }

		// tEnter = 진입 시점, 어느 축이 마지막으로 진입했는지 추적
		int HitAxis = 0;
		float tEnter = txMin;
		if (tyMin > tEnter) { tEnter = tyMin; HitAxis = 1; }
		if (tzMin > tEnter) { tEnter = tzMin; HitAxis = 2; }

		float tExit = txMax < tyMax ? txMax : tyMax;
		if (tzMax < tExit) tExit = tzMax;

		if (tEnter > tExit) continue;
		if (tEnter < 0.0f) tEnter = 0.0f;
		if (tEnter >= ClosestDist) continue;

		FVector Normal = FVector::ZeroVector;
		if      (HitAxis == 0) Normal.X = (Dir.X > 0.0f) ? -1.0f : 1.0f;
		else if (HitAxis == 1) Normal.Y = (Dir.Y > 0.0f) ? -1.0f : 1.0f;
		else                   Normal.Z = (Dir.Z > 0.0f) ? -1.0f : 1.0f;

		ClosestDist = tEnter;
		bFound = true;

		// 구 중심 위치 (충돌 당시)
		FVector SphereCenter;
		SphereCenter.X = Start.X + Dir.X * tEnter;
		SphereCenter.Y = Start.Y + Dir.Y * tEnter;
		SphereCenter.Z = Start.Z + Dir.Z * tEnter;

		OutHit.bHit = true;
		OutHit.Distance = tEnter;
		OutHit.HitComponent = Comp;
		OutHit.HitActor = Comp->GetOwner();
		OutHit.ShapeLocation    = SphereCenter;                           // 구 중심
		OutHit.WorldHitLocation = SphereCenter - Normal * Radius;         // 표면 접촉점
		OutHit.ImpactNormal = Normal;
		OutHit.WorldNormal  = Normal;
	}

	return bFound;
}

// ============================================================
// SphereSweep — 정확한 구-AABB sweep (3-phase)
//
// Phase 1: 면(Face) — 확장 평면에 ray test, 타격점이 해당 면 Voronoi 영역 내인지 확인
// Phase 2: 엣지(Edge) — 각 엣지를 축으로 하는 반지름 R 원기둥에 ray test
// Phase 3: 꼭짓점(Corner) — 각 꼭짓점 중심 반지름 R 구에 ray test.
//
// ShapeLocation    : 충돌 당시 구 중심 위치
// WorldHitLocation : 실제 표면 접촉점
// ============================================================

bool FNativePhysicsScene::SphereSweep(const FVector& Start, const FVector& End, float Radius, FHitResult& OutHit,
	ECollisionChannel TraceChannel, const AActor* IgnoreActor) const
{
	FVector Move = End - Start;
	float MoveDist2 = Move.X*Move.X + Move.Y*Move.Y + Move.Z*Move.Z;
	if (MoveDist2 < 1e-8f) return false;
	const float MoveDist = sqrtf(MoveDist2);
	const float DX = Move.X / MoveDist, DY = Move.Y / MoveDist, DZ = Move.Z / MoveDist;
	const float R2 = Radius * Radius;

	float ClosestDist = MoveDist;
	bool bFound = false;

	for (UPrimitiveComponent* Comp : RegisteredComponents)
	{
		if (IgnoreActor && Comp->GetOwner() == IgnoreActor) continue;
		if (Comp->GetCollisionResponseToChannel(TraceChannel) != ECollisionResponse::Block) continue;

		FBoundingBox Box = Comp->GetWorldBoundingBox();

		// 축 인덱스 기반 연산을 위해 배열로 접근
		const float S[3]    = { Start.X, Start.Y, Start.Z };
		const float D[3]    = { DX, DY, DZ };
		const float BMin[3] = { Box.Min.X, Box.Min.Y, Box.Min.Z };
		const float BMax[3] = { Box.Max.X, Box.Max.Y, Box.Max.Z };

		float tBest = ClosestDist;
		float NX = 0.f, NY = 0.f, NZ = 0.f;
		bool bHitThis = false;

		// best hit 후보 갱신 헬퍼
		auto TryHit = [&](float t, float nx, float ny, float nz)
		{
			if (t >= 0.0f && t < tBest)
			{
				tBest = t; NX = nx; NY = ny; NZ = nz;
				bHitThis = true;
			}
		};

		// ── Phase 1: Face tests ──────────────────────────────────────────────
		// 각 축의 min/max 면 평면을 Radius만큼 밀어낸 위치에서 ray test.
		// 타격점이 나머지 두 축의 [BMin, BMax] 범위 내에 있으면 면 Voronoi 영역 ✓
		// 방향 제한(D[ax] 부호)으로 내부에서 반대 면을 잘못 감지하는 것을 방지한다.
		for (int ax = 0; ax < 3; ++ax)
		{
			const int a1 = (ax+1)%3, a2 = (ax+2)%3;

			// Min 면 — 구가 -ax 방향에서 접근할 때만 유효 (D[ax] > 0)
			if (D[ax] > 1e-8f)
			{
				const float t = (BMin[ax] - Radius - S[ax]) / D[ax];
				if (t >= 0.0f && t < tBest)
				{
					const float p1 = S[a1] + t*D[a1], p2 = S[a2] + t*D[a2];
					if (p1 >= BMin[a1] && p1 <= BMax[a1] && p2 >= BMin[a2] && p2 <= BMax[a2])
					{
						float n[3] = {0,0,0}; n[ax] = -1.0f;
						TryHit(t, n[0], n[1], n[2]);
					}
				}
			}

			// Max 면 — 구가 +ax 방향에서 접근할 때만 유효 (D[ax] < 0)
			if (D[ax] < -1e-8f)
			{
				const float t = (BMax[ax] + Radius - S[ax]) / D[ax];
				if (t >= 0.0f && t < tBest)
				{
					const float p1 = S[a1] + t*D[a1], p2 = S[a2] + t*D[a2];
					if (p1 >= BMin[a1] && p1 <= BMax[a1] && p2 >= BMin[a2] && p2 <= BMax[a2])
					{
						float n[3] = {0,0,0}; n[ax] = 1.0f;
						TryHit(t, n[0], n[1], n[2]);
					}
				}
			}
		}

		// ── Phase 2: Edge tests ──────────────────────────────────────────────
		// AABB의 12개 엣지를 축으로 하는 무한 원기둥(반지름 R)에 ray test.
		// 타격점이 엣지 축 방향의 [BMin, BMax] 내에 있으면 엣지 Voronoi 영역 ✓
		for (int edgeAx = 0; edgeAx < 3; ++edgeAx)
		{
			const int a1 = (edgeAx+1)%3, a2 = (edgeAx+2)%3;
			for (int s1 = 0; s1 < 2; ++s1)
			for (int s2 = 0; s2 < 2; ++s2)
			{
				const float ec1 = s1 ? BMax[a1] : BMin[a1]; // 엣지의 a1 좌표
				const float ec2 = s2 ? BMax[a2] : BMin[a2]; // 엣지의 a2 좌표

				// 원기둥 방정식: (S[a1]+t*D[a1]-ec1)² + (S[a2]+t*D[a2]-ec2)² = R²
				const float da1 = S[a1] - ec1, da2 = S[a2] - ec2;
				const float qa = D[a1]*D[a1] + D[a2]*D[a2];
				if (qa < 1e-10f) continue; // 엣지 축과 거의 평행 → 원기둥 교차 없음

				const float qb   = da1*D[a1] + da2*D[a2];
				const float qc   = da1*da1 + da2*da2 - R2;
				const float disc = qb*qb - qa*qc;
				if (disc < 0.0f) continue;

				// 진입(entering) root만 사용 — 이미 통과한 경우는 제외
				const float t = (-qb - sqrtf(disc)) / qa;
				if (t < 0.0f || t >= tBest) continue;

				// 엣지 축 방향 범위 확인
				const float hitOnAx = S[edgeAx] + t * D[edgeAx];
				if (hitOnAx < BMin[edgeAx] || hitOnAx > BMax[edgeAx]) continue;

				// 법선: 엣지에서 구 중심 방향 (원기둥 반지름 방향)
				float ha1 = S[a1] + t*D[a1] - ec1;
				float ha2 = S[a2] + t*D[a2] - ec2;
				float len = sqrtf(ha1*ha1 + ha2*ha2);
				if (len < 1e-8f)
				{
					// 구 중심이 엣지 위에 정확히 있는 경우 — 모서리 방향으로 fallback
					ha1 = s1 ? 1.0f : -1.0f;
					ha2 = s2 ? 1.0f : -1.0f;
					len = sqrtf(2.0f);
				}
				ha1 /= len; ha2 /= len;

				float n[3] = {0,0,0};
				n[a1] = ha1; n[a2] = ha2;
				TryHit(t, n[0], n[1], n[2]);
			}
		}

		// ── Phase 3: Corner tests ────────────────────────────────────────────
		// AABB의 8개 꼭짓점을 중심으로 하는 구(반지름 R)에 ray test.
		// 타격점이 세 축 모두 AABB 외부에 있어야 꼭짓점 Voronoi 영역 ✓
		for (int cx = 0; cx < 2; ++cx)
		for (int cy = 0; cy < 2; ++cy)
		for (int cz = 0; cz < 2; ++cz)
		{
			const float CX = cx ? BMax[0] : BMin[0];
			const float CY = cy ? BMax[1] : BMin[1];
			const float CZ = cz ? BMax[2] : BMin[2];

			// 구 방정식: |Start + t*Dir - corner|² = R²
			// Dir이 단위 벡터이므로 a = 1
			const float dx = S[0]-CX, dy = S[1]-CY, dz = S[2]-CZ;
			const float b    = dx*D[0] + dy*D[1] + dz*D[2];
			const float c    = dx*dx + dy*dy + dz*dz - R2;
			const float disc = b*b - c;
			if (disc < 0.0f) continue;

			// 진입(entering) root만 사용
			const float t = -b - sqrtf(disc);
			if (t < 0.0f || t >= tBest) continue;

			// 꼭짓점 Voronoi 영역 확인: 타격 시 구 중심이 세 축 모두 AABB 외부
			const float hx = S[0]+t*D[0], hy = S[1]+t*D[1], hz = S[2]+t*D[2];
			if (cx == 0 ? (hx > BMin[0]) : (hx < BMax[0])) continue;
			if (cy == 0 ? (hy > BMin[1]) : (hy < BMax[1])) continue;
			if (cz == 0 ? (hz > BMin[2]) : (hz < BMax[2])) continue;

			// 법선: 꼭짓점에서 구 중심 방향
			float nx = hx-CX, ny = hy-CY, nz = hz-CZ;
			const float nlen = sqrtf(nx*nx + ny*ny + nz*nz);
			if (nlen > 1e-8f) { nx /= nlen; ny /= nlen; nz /= nlen; }
			else { nx = cx?1.f:-1.f; ny = cy?1.f:-1.f; nz = cz?1.f:-1.f; const float nl=sqrtf(3.f); nx/=nl; ny/=nl; nz/=nl; }

			TryHit(t, nx, ny, nz);
		}

		// ── 결과 기록 ────────────────────────────────────────────────────────
		if (bHitThis)
		{
			ClosestDist = tBest;
			bFound = true;

			const FVector SphereCenter(Start.X + DX*tBest, Start.Y + DY*tBest, Start.Z + DZ*tBest);
			const FVector Normal(NX, NY, NZ);

			OutHit.bHit           = true;
			OutHit.Distance       = tBest;
			OutHit.HitComponent   = Comp;
			OutHit.HitActor       = Comp->GetOwner();
			OutHit.ShapeLocation  = SphereCenter;
			OutHit.WorldHitLocation = SphereCenter - Normal * Radius;
			OutHit.ImpactNormal   = Normal;
			OutHit.WorldNormal    = Normal;
		}
	}

	return bFound;
}
