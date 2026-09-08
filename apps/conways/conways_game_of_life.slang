
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

struct PushConstants
{
    uint32_t buffer_size;
    float *arbitrary_buffer;
}

// Push Constants for frag shader
struct ConwaysState
{
    uint32_t sim_width;
    uint32_t sim_height;
    uint8_t *state_address_a;
    uint8_t *state_address_b;
    uint32_t alternator;
    float win_x;
    float win_y;
    float time;
    float __unused_padding__;
    uint32_t mouse_x;
    uint32_t mouse_y;
    uint32_t mouse_down;
    uint32_t paused;
};

[[vk::push_constant]]
ConwaysState properties;

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{

    VertexOutput output;
    // Remember that this is per vertex, and due to interpolation this will create a smooth gradient in the frag shader? i think; the interpolation happens somewhere
    output.sv_position = float4(input.position, 0.0, 1.0);
    output.color = input.colour;
    return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_Target
{
    // Convert the pixel position into normalized screen coordinates.
    float2 uv = input.sv_position.xy / float2(properties.win_x, properties.win_y);

    // Map the screen coordinates onto the simulation grid.
    uint sim_x = min(
        uint(uv.x * float(properties.sim_width)),
        properties.sim_width - 1);

    uint sim_y = min(
        uint(uv.y * float(properties.sim_height)),
        properties.sim_height - 1);

    uint index = sim_y * properties.sim_width + sim_x;

    // alternator == 1 -> A is current
    // alternator == 0 -> B is current
    uint8_t alive =
        properties.alternator == 1
            ? properties.state_address_a[index]
            : properties.state_address_b[index];

    if (alive == 1)
    {
        return (properties.paused == 0) ? float4(1.0, 1.0, 1.0, 1.0) : float4(1.0, 0.5, 0.5, 1.0);
    }
    return float4(0.0, 0.0, 0.0, 1.0);
}

int[8] get_neighbour_addresses(uint sim_x, uint sim_y)
{
    // Periodic boundary conditions makes this a pain
    int left_index = int(sim_x) - 1;
    int right_index = int(sim_x) + 1;
    int down_index = int(sim_y) - 1;
    int up_index = int(sim_y) + 1;

    if (left_index < 0)
        left_index = int(properties.sim_width) - 1;

    if (right_index >= int(properties.sim_width))
        right_index = 0;

    if (down_index < 0)
        down_index = int(properties.sim_height) - 1;

    if (up_index >= int(properties.sim_height))
        up_index = 0;

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

uint8_t how_many_neighbours_alive(uint sim_x, uint sim_y)
{
    int addresses[8] = get_neighbour_addresses(sim_x, sim_y);

    uint8_t num_alive = 0;

    for (int i = 0; i < 8; i++)
    {
        uint addr = addresses[i];
        if (properties.alternator == 1)
        {
            uint addr = addresses[i];
            if (properties.state_address_b[addr] == 1)
            {
                num_alive += 1;
            }
        }
        else
        {
            if (properties.state_address_a[addr] == 1)
            {
                num_alive += 1;
            }
        }
    }

    return num_alive;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void compMain(uint3 thread_id: SV_DispatchThreadID)
{
    // NOTE: If alternator == 1, then buffer_a is the current and b is prev
    // if it is 0, then buffer_a is prev and b is current

    uint target_index = thread_id.x;
    uint sim_size = properties.sim_width * properties.sim_height;

    if (target_index >= sim_size)
    {
        return;
    }

    uint x_pos = target_index % properties.sim_width;
    uint y_pos = target_index / properties.sim_width;

    uint8_t num_alive_neighbours = how_many_neighbours_alive(x_pos, y_pos);

    uint8_t current_live_or_die_state = (properties.alternator == 1)
                                            ? properties.state_address_b[target_index]
                                            : properties.state_address_a[target_index];
    uint8_t new_live_or_die_state = current_live_or_die_state;

    if (properties.paused == 0)
    {
        if (current_live_or_die_state == 1)
        {
            if (num_alive_neighbours < 2)
            {
                // Under Population
                new_live_or_die_state = 0;
            }
            else if (num_alive_neighbours == 2 || num_alive_neighbours == 3)
            {
                // Just Enough
                new_live_or_die_state = 1;
            }
            else if (num_alive_neighbours > 3)
            {
                // Over Population
                new_live_or_die_state = 0;
            }
        }
        else if (current_live_or_die_state == 0)
        {
            // Reproduction
            if (num_alive_neighbours == 3)
            {
                new_live_or_die_state = 1;
            }
        }

        if (properties.alternator == 1)
        {
            properties.state_address_a[target_index] = new_live_or_die_state;
        }
        else
        {
            properties.state_address_b[target_index] = new_live_or_die_state;
        }
    }
    // Mouse inputs bypasses everything
    if (properties.mouse_down == 1)
    {
        float scaled_x_pos = float(properties.mouse_x) / float(properties.win_x);
        float scaled_y_pos = float(properties.mouse_y) / float(properties.win_y);

        uint x_index = int(scaled_x_pos * float(properties.sim_width));
        uint y_index = int(scaled_y_pos * float(properties.sim_height));

        int[8] addresses = get_neighbour_addresses(x_index, y_index);

        uint8_t *new_state_buffer = (properties.alternator == 1) ? properties.state_address_a : properties.state_address_b;

        new_state_buffer[(y_index * properties.sim_width) + x_index] = 1;
        new_state_buffer[addresses[1]] = 1;
        new_state_buffer[addresses[3]] = 1;
        new_state_buffer[addresses[4]] = 1;
        new_state_buffer[addresses[6]] = 1;
    }
}

