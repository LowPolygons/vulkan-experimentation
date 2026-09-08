struct VertexInput
{
    float2 position;
};

struct VertexOutput
{
    float3 color;
    float4 sv_position : SV_Position;
};

struct TextureColour
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t __padding__;
};

struct Slime
{
    float2 position;
    uint32_t family_index;
    float angle;
}
uint hash_function(uint state)
{
    state ^= 2747636419u;
    state *= 2654435779u;
    state ^= state >> 16;
    state *= 2654435779u;
    state ^= state >> 16;
    state *= 2654435779u;
    return state;
}

struct SlimeState
{
    uint32_t sim_width;
    uint32_t sim_height;
    uint32_t win_width;
    uint32_t win_height;

    Slime *slime_address;
    uint32_t num_slimes;

    TextureColour *texture_mesh_a;
    TextureColour *texture_mesh_b;
    uint32_t alternator;

    float tick;
};

int[8] get_neighbour_colours(uint sim_x, uint sim_y)
{
    // Periodic boundary conditions makes this a pain
    int left_index = int(sim_x) - 1;
    int right_index = int(sim_x) + 1;
    int down_index = int(sim_y) - 1;
    int up_index = int(sim_y) + 1;

    if (left_index < 0)
        left_index = 0;

    if (right_index >= int(properties.sim_width))
        right_index = properties.sim_width - 1;

    if (down_index < 0)
        down_index = 0;

    if (up_index >= int(properties.sim_height))
        up_index = int(properties.sim_height) - 1;

    uint sim_width = properties.sim_width;
    uint sim_height = properties.sim_height;

    int addresses[8] = {
        (down_index * sim_width) + left_index,
        (down_index * sim_width) + sim_x,
        (down_index * sim_width) + right_index,
        (sim_y * sim_width) + left_index,
        (sim_y * sim_width) + right_index,
        (up_index * sim_width) + left_index,
        (up_index * sim_width) + sim_x,
        (up_index * sim_width) + right_index,
    };

    return addresses;
}

[[vk::push_constant]]
SlimeState properties;

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
    VertexOutput output;
    output.sv_position = float4(input.position, 0.0, 1.0);
    output.color = float3(0.0,0.0,0.0);
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

    TextureColour *current_state = (properties.alternator == 0) ? properties.texture_mesh_b : properties.texture_mesh_a;

    return float4(
        float(current_state[index].r) / 255.0,
        float(current_state[index].g) / 255.0,
        float(current_state[index].b) / 255.0, 1.0);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void compTextureMain(uint3 thread_id: SV_DispatchThreadID)
{
    // return;
    uint target_index = thread_id.x;
    uint sim_size = properties.sim_width * properties.sim_height;

    if (target_index >= sim_size)
        return;

    uint x_pos = target_index % properties.sim_width;
    uint y_pos = target_index / properties.sim_width;

    TextureColour *previous_state = (properties.alternator == 1) ? properties.texture_mesh_b : properties.texture_mesh_a;
    TextureColour *current_state = (properties.alternator == 0) ? properties.texture_mesh_b : properties.texture_mesh_a;

    int[8] neighbour_addresses = get_neighbour_colours(x_pos, y_pos);

    TextureColour current_cell_colour = previous_state[target_index];
    current_cell_colour.r = uint8_t(float(current_cell_colour.r) * 0.5);
    current_cell_colour.g = uint8_t(float(current_cell_colour.g) * 0.5);
    current_cell_colour.b = uint8_t(float(current_cell_colour.b) * 0.5);

    uint r = uint(current_cell_colour.r);
    uint g = uint(current_cell_colour.g);
    uint b = uint(current_cell_colour.b);

    for (int i = 0; i < 8; i++)
    {
        TextureColour neighbour_colour = previous_state[neighbour_addresses[i]];
        r += uint(neighbour_colour.r);
        g += uint(neighbour_colour.g);
        b += uint(neighbour_colour.b);
    }

    r = r / 9;
    g = g / 9;
    b = b / 9;

    current_state[target_index] = TextureColour(uint8_t(r), uint8_t(g), uint8_t(b), 0);
}

TextureColour get_family_colour(uint32_t colour_index)
{
    if (colour_index == 1)
    {
        return TextureColour(255, 255, 255, 0);
    }
    return TextureColour(255, 200, 200, 0);
}

float sense(Slime slime, float sensor_angle_offset, float sensor_distance)
{
    float sensor_angle = slime.angle + sensor_angle_offset;
    float2 sensor_dir = float2(cos(sensor_angle), sin(sensor_angle));
    int2 sensor_center =
        int2(slime.position + sensor_dir * sensor_distance);
    float sum = 0;
    TextureColour *prev_state = (properties.alternator == 1) ? properties.texture_mesh_b : properties.texture_mesh_a;
    for (int offset_x = -2; offset_x < 2; offset_x++)
    {
        for (int offset_y = -2; offset_y < 2; offset_y++)
        {
            int2 pos = sensor_center + int2(offset_x, offset_y);
            if (pos.x >= 0 && pos.x < properties.sim_width && pos.y >= 0 && pos.y < properties.sim_height)
            {
                sum += prev_state[pos.y * properties.sim_width + pos.x].r;
            }
        }
    }
    return sum;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void compSlimeMain(uint3 thread_id: SV_DispatchThreadID)
{
    uint target_index = thread_id.x;

    if (target_index >= properties.num_slimes)
        return;

    Slime slime = properties.slime_address[target_index];
    uint random = hash_function(uint(slime.position.y * properties.sim_width) + uint(slime.position.x) + hash_function(target_index));

    const float move_speed = 4;
    const float sensor_distance = 4.0;
    const float sensor_angle = 3.14159265 / 6.0;

    const float turn_speed = 0.5;

    float random_steer = float(random) / float(uint.maxValue);

    float forward = sense(slime, 0.0, sensor_distance);
    float left = sense(slime, sensor_angle, sensor_distance);
    float right = sense(slime, -sensor_angle, sensor_distance);

    float angle = slime.angle;

    if (forward > left && forward > right)
    {
        // Continue forward.
    }
    else if (left > right)
    {
        angle += turn_speed;
    }
    else if (right > left)
    {
        angle -= turn_speed;
    }
    else
    {
        angle += (random_steer - 0.5) * turn_speed;
    }

    float2 direction = float2(cos(angle), sin(angle));
    float2 new_pos = slime.position + direction * move_speed;

    if (new_pos.x < 0 || new_pos.x >= properties.sim_width || new_pos.y < 0 || new_pos.y >= properties.sim_height)
    {
        new_pos.x = min(float(properties.sim_width) - 0.01, max(0, new_pos.x));
        new_pos.y = min(float(properties.sim_height) - 0.01, max(0, new_pos.y));
        angle = (float(random) / float(uint.maxValue)) * 2 * 3.14159265;
    }

    TextureColour *current_state = (properties.alternator == 0) ? properties.texture_mesh_b : properties.texture_mesh_a;
    uint x = uint(new_pos.x);
    uint y = uint(new_pos.y);
    Slime new_slime = Slime(new_pos, slime.family_index, angle);
    properties.slime_address[target_index] = new_slime;

    current_state[y * properties.sim_width + x] = get_family_colour(slime.family_index);
    // InterlockedAdd(current_state[y * properties.sim_width + x].r, get_family_colour(slime.family_index).r);
    // InterlockedAdd(current_state[y * properties.sim_width + x].g, get_family_colour(slime.family_index).g);
    // InterlockedAdd(current_state[y * properties.sim_width + x].b, get_family_colour(slime.family_index).b);
}
