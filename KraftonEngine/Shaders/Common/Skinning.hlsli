#ifndef SKINNING_HLSL
#define SKINNING_HLSL

// b6: SkeletalMesh 렌더링 옵션
cbuffer SkeletalRenderBuffer : register(b6)
{
    uint SkinningMode;
    uint HeatmapMode;
    int SelectedBoneIndex;
    float HeatmapIntensity;
};

StructuredBuffer<float4x4> SkinMatrices : register(t26);

float4x4 BuildSkinMatrix(int4 boneIndices, float4 boneWeights)
{
    float4x4 skinMatrix = (float4x4)0;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        int boneIndex = boneIndices[i];
        float boneWeight = boneWeights[i];
        if (boneIndex >= 0 && boneWeight > 0.0f)
        {
            skinMatrix += SkinMatrices[boneIndex] * boneWeight;
        }
    }

    return skinMatrix;
}

float GetSkinWeightSum(int4 boneIndices, float4 boneWeights)
{
    float weightSum = 0.0f;

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        if (boneIndices[i] >= 0 && boneWeights[i] > 0.0f)
        {
            weightSum += boneWeights[i];
        }
    }

    return weightSum;
}

#endif // SKINNING_HLSL
