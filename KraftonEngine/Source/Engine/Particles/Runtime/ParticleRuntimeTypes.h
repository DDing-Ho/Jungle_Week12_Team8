/**
 * @file ParticleRuntimeTypes.h
 * @brief Runtime Particle 데이터 구조 정의.
 *
 * 포함 타입:
 * - FBaseParticle: Particle 1개의 Runtime 상태
 * - FParticleDataContainer: ParticleData / ParticleIndices 메모리 컨테이너
 * - FParticleEventData: 공용 Particle Event Payload
 * - FParticleEventCollideData: Particle Collision Event 데이터
 */

#pragma once

#include "../Assets/ParticleAsset.h"

#include <cstring>

/** Runtime Particle 1개의 기본 데이터 */
struct FBaseParticle
{
    FVector Location;              // 현재 위치
	float   RelativeTime = 0.0f;   // 수명 진행 비율

    FVector Velocity;              // 현재 속도
    float   Lifetime = 1.0f;       // 전체 수명

    FVector BaseVelocity;          // 기준 속도
	float	BaseRotationRate;	   // 초기 회전 속도
	
	FVector	BaseSize;				// 초기 사이즈
	float	RotationRate;			// 현재 회전 속도

    FVector Size = FVector(1.0f, 1.0f, 1.0f);  // 현재 크기
    FColor  Color = FColor::White(); // 현재 색상
    FColor  BaseColor = FColor::White(); // 현재 색상
    int32   SubImageIndex = 0;     // SubUV 프레임 인덱스
};

/** ParticleData와 ParticleIndices를 보관하는 메모리 컨테이너 */
struct FParticleDataContainer
{
    int32   MemBlockSize = 0;             // 전체 메모리 블록 크기
    int32   ParticleDataNumBytes = 0;     // ParticleData 영역 크기
    int32   ParticleIndicesNumShorts = 0; // ParticleIndices 개수
    uint8  *ParticleData = nullptr;       // Particle 데이터 메모리
    uint16 *ParticleIndices = nullptr;    // 활성 Particle 인덱스 배열

    void   Allocate(int32 InParticleStride, int32 InMaxParticleCount); // Particle 메모리 할당
    void   Release();                                                  // Particle 메모리 해제
    uint8 *GetParticleData(int32 ParticleIndex) const;                 // Particle 데이터 주소 반환
};

inline void FParticleDataContainer::Allocate(int32 InParticleStride, int32 InMaxParticleCount)
{
    Release();

    if (InParticleStride <= 0 || InMaxParticleCount <= 0)
        return;

    ParticleDataNumBytes = InParticleStride * InMaxParticleCount;
    ParticleIndicesNumShorts = InMaxParticleCount;
    MemBlockSize = ParticleDataNumBytes + static_cast<int32>(sizeof(uint16) * ParticleIndicesNumShorts);

    ParticleData = new uint8[ParticleDataNumBytes];
    ParticleIndices = new uint16[ParticleIndicesNumShorts];

    std::memset(ParticleData, 0, ParticleDataNumBytes);
    std::memset(ParticleIndices, 0, sizeof(uint16) * ParticleIndicesNumShorts);
}

inline void FParticleDataContainer::Release()
{
    delete[] ParticleData;
    ParticleData = nullptr;

    delete[] ParticleIndices;
    ParticleIndices = nullptr;

    MemBlockSize = 0;
    ParticleDataNumBytes = 0;
    ParticleIndicesNumShorts = 0;
}

inline uint8* FParticleDataContainer::GetParticleData(int32 ParticleIndex) const
{
    if (!ParticleData || ParticleIndex < 0 || ParticleIndicesNumShorts <= 0)
        return nullptr;

    const int32 ParticleStride = ParticleDataNumBytes / ParticleIndicesNumShorts;
    if (ParticleStride <= 0 || ParticleIndex >= ParticleIndicesNumShorts)
        return nullptr;

    return ParticleData + ParticleStride * ParticleIndex;
}

/** Component EventQueue를 통해 전달되는 공용 Particle Event Payload */
struct FParticleEventData
{
    EParticleEventType Type = EParticleEventType::PEET_Custom; // Event 종류
    FName              EventName;                              // 이벤트 이름 태그 (2차 판별용)
    FVector            Location = FVector::ZeroVector;         // Event 발생 위치
    FVector            Velocity = FVector::ZeroVector;         // Event 발생 당시 속도
    FVector            Normal = FVector::ZeroVector;           // 방향성 Event 부가 정보
    int32              SourceEmitterIndex = INDEX_NONE;        // Event를 발생시킨 Emitter 인덱스
    int32              SourceParticleIndex = INDEX_NONE;       // Event를 발생시킨 Particle 인덱스
};

/** Particle Collision Event 데이터 */
struct FParticleEventCollideData
{
    FVector Location;                   // 충돌 위치
    FVector Normal;                     // 충돌 법선
    FVector Velocity;                   // 충돌 직전 속도
    int32   ParticleIndex = INDEX_NONE; // 충돌 Particle 인덱스
};
