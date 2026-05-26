#include "Common/Functions.hlsli"
#include "Common/SystemSamplers.hlsli"

Texture2D ParticleTexture : register(t0);

struct VS_Input_ParticleMesh
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float2 texcoord : TEXTCOORD;
    float4 tangent : TANGENT;

    float4 instanceTransform0 : INSTANCETRANSFORM0;
    float4 instanceTransform1 : INSTANCETRANSFORM1;
    float4 instanceTransform2 : INSTANCETRANSFORM2;
    float4 instanceTransform3 : INSTANCETRANSFORM3;
    float4 instanceColor : INSTANCECOLOR;
};

struct VS_Output_ParticleMesh
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float3 normal : TEXCOORD1;
    float4 tangent : TEXCOORD2;
};

struct PS_Input_ParticleMesh
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4x4 BuildInstanceTransform(VS_Input_ParticleMesh input)
{
    return float4x4(
        input.instanceTransform0,
        input.instanceTransform1,
        input.instanceTransform2,
        input.instanceTransform3);
}

VS_Output_ParticleMesh VS(VS_Input_ParticleMesh input)
{
    VS_Output_ParticleMesh output;

    float4x4 instanceTransform = BuildInstanceTransform(input);
    float4 worldPos = mul(float4(input.position, 1.0f), instanceTransform);

    output.position = mul(mul(worldPos, View), Projection);
    output.uv = input.texcoord;
    output.color = input.color * input.instanceColor;
    output.normal = mul(float4(input.normal, 0.0f), instanceTransform).xyz;
    output.tangent = float4(mul(float4(input.tangent.xyz, 0.0f), instanceTransform).xyz, input.tangent.w);
    return output;
}

float4 PS(PS_Input_ParticleMesh input) : SV_TARGET
{
    float4 texColor = ParticleTexture.Sample(LinearWrapSampler, input.uv);
    float4 color = texColor * input.color;

    if (!bIsWireframe && color.a <= 0.001f)
    {
        discard;
    }

    return float4(ApplyWireframe(color.rgb), bIsWireframe ? 1.0f : color.a);
}
