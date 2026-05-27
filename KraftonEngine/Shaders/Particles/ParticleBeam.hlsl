#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D ParticleTexture : register(t0);

struct VS_Input_Beam
{
    // Per-vertex
    float2 corner : POSITION;
    float2 texcoord : TEXCOORD0;

    // Per-instance. Keep this order in sync with FBeamParticleInstanceVertex.
    float3 instanceSegmentStart : INSTANCESEGMENTSTART;
    float3 instanceSegmentEnd : INSTANCESEGMENTEND;
    float instanceBeamWidth : INSTANCEBEAMWIDTH;
    float instanceU0 : INSTANCEU0;
    float instanceU1 : INSTANCEU1;
    float4 instanceColor : INSTANCECOLOR;
};

struct PS_Input_Beam
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

PS_Input_Beam VS(VS_Input_Beam input)
{
    PS_Input_Beam output;

    float4 startView = mul(float4(input.instanceSegmentStart, 1.0f), View);
    float4 endView = mul(float4(input.instanceSegmentEnd, 1.0f), View);

    float2 segmentDir = endView.xy - startView.xy;
    float segmentLenSq = dot(segmentDir, segmentDir);
    float2 dir = segmentLenSq > 1e-6f
        ? segmentDir * rsqrt(segmentLenSq)
        : float2(1.0f, 0.0f);
    float2 normal = float2(-dir.y, dir.x);

    float4 viewPos = lerp(startView, endView, input.corner.x);
    viewPos.xy += normal * input.corner.y * input.instanceBeamWidth;

    output.position = mul(viewPos, Projection);
    output.uv = float2(lerp(input.instanceU0, input.instanceU1, input.corner.x), input.texcoord.y);
    output.color = input.instanceColor;
    return output;
}

float4 PS(PS_Input_Beam input) : SV_TARGET
{
    float4 texColor = ParticleTexture.Sample(LinearClampSampler, input.uv);
    float4 color = texColor * input.color;

    if (!bIsWireframe && color.a <= 0.001f)
    {
        discard;
    }

    return float4(ApplyWireframe(color.rgb), bIsWireframe ? 1.0f : color.a);
}
