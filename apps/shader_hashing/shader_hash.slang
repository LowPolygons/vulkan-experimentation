struct VertexInput
{
    float2 position;
    float3 colour;
};

struct VertexOutput
{
    float3 color;
    float4 sv_position : SV_Position;
};

struct ShaderTexture {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

// Push Constants for frag shader
struct ShaderHashState 
{
  uint32_t sim_width;
  uint32_t sim_height;
  uint32_t win_width;
  uint32_t win_height;

  ShaderTexture* colour_address;

  uint32_t seed_offset;
};

[[vk::push_constant]]
ShaderHashState properties;

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.sv_position = float4(input.position, 0.0, 1.0);
    output.color = input.colour;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    // Convert the pixel position into normalized screen coordinates.
    float2 uv = input.sv_position.xy / float2(properties.win_width, properties.win_height);

    // Map the screen coordinates onto the simulation grid.
    uint sim_x = min(
        uint(uv.x * float(properties.sim_width)),
        properties.sim_width - 1);

    uint sim_y = min(
        uint(uv.y * float(properties.sim_height)),
        properties.sim_height - 1);

    uint index = sim_y * properties.sim_width + sim_x;

    return float4(
        float(properties.colour_address[index].r) / 255.0, 
        float(properties.colour_address[index].g) / 255.0, 
        float(properties.colour_address[index].b) / 255.0, 1.0);
}

uint hash_function(uint state) {
    state ^= 2184902;
    state *= 23842309;
    state *= 12839021;
    state *= 23472390;
    return state;
}
// uint hash_function(uint state) {
//     state ^= 2747636419u;
//     state *= 2654435779u;
//     state ^= state >> 16;
//     state *= 2654435779u;
//     state ^= state >> 16;
//     state *= 2654435779u;
//     return state;
// }

[shader("compute")]
[numthreads(64, 1, 1)]
void compMain(uint3 thread_id: SV_DispatchThreadID) {
    uint target_index = thread_id.x;
    uint sim_size = properties.sim_width * properties.sim_height;

    if (target_index >= sim_size)
        return;

    uint x_pos = target_index % properties.sim_width;
    uint y_pos = target_index / properties.sim_width;

    uint initial_state = (y_pos * properties.sim_width) + x_pos + properties.seed_offset;
    uint new_state = hash_function(initial_state);

    ShaderTexture new_texture = {
        float(new_state) / float(uint.maxValue) * 255,
        float(new_state) / float(uint.maxValue) * 255,
        float(new_state) / float(uint.maxValue) * 255
    };

    properties.colour_address[(y_pos * properties.sim_width) + x_pos] = new_texture;
}
