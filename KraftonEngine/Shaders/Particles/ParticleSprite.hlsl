#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D ParticleTexture : register(t0);

struct VS_Input_ParticleSprite
{
    float3 corner : POSITION;
    float2 texcoord : TEXCOORD0;
    float3 instancePosition : INSTANCEPOSITION;
    float instanceRotation : INSTANCEROTATION;
    float4 instanceSize : INSTANCESIZE;
    float4 instanceColor : INSTANCECOLOR;
    float4 instanceUVRegionA : INSTANCEUVREGIONA;
    float4 instanceUVRegionB : INSTANCEUVREGIONB;
    float instanceSubUVLerp : INSTANCESUBUVLERP;
};

struct PS_Input_ParticleSprite
{
    float4 position : SV_POSITION;
    float2 uvA : TEXCOORD0;
    float2 uvB : TEXCOORD1;
    float subUVLerp : TEXCOORD2;
    float4 color : COLOR0;
};

PS_Input_ParticleSprite VS(VS_Input_ParticleSprite input)
{
    PS_Input_ParticleSprite output;

    float2 local = input.corner.xy * input.instanceSize.xy;
    float s = sin(input.instanceRotation);
    float c = cos(input.instanceRotation);
    float2 rotated = float2(
        local.x * c - local.y * s,
        local.x * s + local.y * c);

    float4 viewPos = mul(float4(input.instancePosition, 1.0f), View);
    viewPos.xy += rotated;

    output.position = mul(viewPos, Projection);
    output.uvA = input.instanceUVRegionA.xy + input.texcoord * input.instanceUVRegionA.zw;
    output.uvB = input.instanceUVRegionB.xy + input.texcoord * input.instanceUVRegionB.zw;
    output.subUVLerp = input.instanceSubUVLerp;
    output.color = input.instanceColor;
    return output;
}

float4 PS(PS_Input_ParticleSprite input) : SV_TARGET
{
    float4 texColorA = ParticleTexture.Sample(LinearClampSampler, input.uvA);
    float4 texColorB = ParticleTexture.Sample(LinearClampSampler, input.uvB);
    float4 texColor = lerp(texColorA, texColorB, saturate(input.subUVLerp));

    float4 color = texColor * input.color;

    if (!bIsWireframe && color.a <= 0.001f)
    {
        discard;
    }

    return float4(ApplyWireframe(color.rgb), bIsWireframe ? 1.0f : color.a);
}
