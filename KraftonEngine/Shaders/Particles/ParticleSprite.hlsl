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
};

struct PS_Input_ParticleSprite
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
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
    output.uv = input.texcoord;
    output.color = input.instanceColor;
    return output;
}

float4 PS(PS_Input_ParticleSprite input) : SV_TARGET
{
    float4 texColor = ParticleTexture.Sample(LinearClampSampler, input.uv);

    float4 color = texColor * input.color;

    if (!bIsWireframe && color.a <= 0.001f)
    {
        discard;
    }

    return float4(ApplyWireframe(color.rgb), bIsWireframe ? 1.0f : color.a);
}
