#pragma pack_matrix(row_major)

cbuffer Constants : register(b0)
{
    float4x4 g_ViewProj;
    float4x4 g_Model;
    float4 g_Color;
    float4 g_LightDir;
};

struct VSInput
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
};

struct PSInput
{
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float4 color : COLOR;
};

PSInput main_vs(VSInput input)
{
    PSInput output;
    float4 world = mul(float4(input.pos, 1.0), g_Model);
    output.pos = mul(world, g_ViewProj);
    output.normal = normalize(mul(input.normal, (float3x3)g_Model));
    output.color = g_Color;
    return output;
}

float4 main_ps(PSInput input) : SV_Target
{
    float3 n = normalize(input.normal);
    float3 l = normalize(-g_LightDir.xyz);
    float diffuse = max(dot(n, l), 0.0);
    float3 color = input.color.rgb * (0.25 + 0.75 * diffuse);
    return float4(color, 1.0);
}
