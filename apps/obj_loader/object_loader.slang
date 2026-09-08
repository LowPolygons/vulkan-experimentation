struct VSInput {
    float3 inPosition;
    float2 tex_coord;
    float3 colour;
};

struct VertexOutput
{
    float3 color;
    float4 sv_position : SV_Position;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

struct ObjectLoaderPushConstant {
    uint32_t win_width;
    uint32_t win_height;
    UniformBuffer* ubo;
};

[[vk::push_constant]]
ObjectLoaderPushConstant properties;

[shader("vertex")]
VertexOutput vertMain(VSInput input) {
    VertexOutput output;
    // There is only one
    UniformBuffer ubo = *properties.ubo;

    float4 pre_processed_pos = float4(input.inPosition, 1.0);

    output.sv_position = mul(ubo.proj, mul(ubo.view, mul(ubo.model, pre_processed_pos)));
    output.color = abs(normalize(input.colour));
    
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput vertIn) : SV_TARGET {
    return float4(vertIn.color, 1.0);
}
