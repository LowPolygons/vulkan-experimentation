static float2 positions[3] = float2[](
    float2(0.0, -0.8),
    float2(0.8, 0.8),
    float2(-0.8, 0.8)
);

static float3 colors[3] = float3[](
    float3(1.0, 1.0, 0.0),
    float3(0.0, 1.0, 1.0),
    float3(1.0, 0.0, 1.0)
);


struct VertexInput {
    float2 position;
    float3 colour;
    float number;
};

struct VertexOutput {
    float3 color;
    float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input) {
    VertexOutput output;
    // Remember that this is per vertex, and due to interpolation this will create a smooth gradient in the frag shader? i think; the interpolation happens somewhere
    output.sv_position = float4(input.position, 0.0, 1.0);
    if (input.number > 10) {
        float r = input.colour[2];
        float g = input.colour[0];
        float b = input.colour[1];
        float3 new_colour = {r, g, b};

        output.color = new_colour;
    } else {
        output.color = input.colour;
    }
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target
{
    float3 color = inVert.color;
    return float4(color, 1.0);
}
