
struct VSInput
{
    float2 in_position;
    float3 in_color;
};

struct VertexOutput
{
    float3 color;
    float4 sv_position : SV_position;
};

[shader("vertex")]
VertexOutput vert_main(VSInput input)
{
    VertexOutput output;
    output.sv_position = float4(input.in_position, 0.0, 1.0);
    output.color = input.in_color;
    return output;
}

[shader("fragment")]
float4 frag_main(VertexOutput in_vert) : SV_Target
{
    float3 color = in_vert.color;
    return float4(color, 1.0);
}
