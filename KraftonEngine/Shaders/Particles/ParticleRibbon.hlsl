#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D ParticleTexture : register(t0);

struct VS_Input_ParticleRibbon
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color : COLOR;
};

struct PS_Input_ParticleRibbon
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PS_Input_ParticleRibbon VS(VS_Input_ParticleRibbon input)
{
    PS_Input_ParticleRibbon output;
    output.position = mul(mul(float4(input.position, 1.0f), View), Projection);
    output.uv = input.texcoord;
    output.color = input.color;
    return output;
}

float4 PS(PS_Input_ParticleRibbon input) : SV_TARGET
{
    float4 texColor = ParticleTexture.Sample(LinearWrapSampler, input.uv);
    float4 color = texColor * input.color;

    if (!bIsWireframe && color.a <= 0.001f)
    {
        discard;
    }

    return float4(ApplyWireframe(color.rgb), bIsWireframe ? 1.0f : color.a);
}
