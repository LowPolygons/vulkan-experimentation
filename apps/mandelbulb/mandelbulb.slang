struct VertexInput
{
    float2 position;
    float3 colour;
    float number;
};

struct VertexOutput
{
    float3 color;
    float4 sv_position : SV_Position;
};

struct Circle
{
    float2 center;
    float radius;
}

struct ImageProperties
{
    float win_x;
    float win_y;
    float power;
    float3 ray_origin;
};

bool isInCircle(Circle circle, float2 position)
{
    float2 distance_from_center = position - circle.center;
    float modulus =
        distance_from_center[0] * distance_from_center[0] + distance_from_center[1] * distance_from_center[1];

    return modulus <= circle.radius * circle.radius;
}

float sphere_msd(float3 position, float radius)
{
    return length(position) - radius;
}

float mandelbulb_msd(float3 position, float power) {
    float3 z = position;
    float dr = 1;
    float r;

    for (int i = 0; i < 15; i++) {
        r = length(z);
        if (r > 2)
            break;

        float theta = acos(z.z / r) * power;
        float phi = atan2(z.y, z.x) * power;
        float zr = pow(r, power);
        dr = pow(r, power - 1) * power * dr + 1;

        z = zr * float3(sin(theta) * cos(phi), sin(phi) * sin(theta), cos(theta));

        z += position;
    }
    return 0.5 * log(r) * r / dr;
}


float3 get_norm_ray_direction(float width, float height, float2 input_pos)
{
    float field_of_view = 60.0 * 3.14159265 / 180.0;
    float aspect_ratio = width / height;

    float pp_width = tan(field_of_view / 2.0) * 2.0;
    float pp_height = pp_width / aspect_ratio;

    float pixel_increment_x = pp_width / width;
    float pixel_increment_y = pp_height / height;

    float x = (input_pos.x - width / 2.0) * pixel_increment_x;
    float y = (height / 2.0 - input_pos.y) * pixel_increment_y;

    return normalize(float3(x, y, 1.0));
}

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{

    VertexOutput output;
    // Remember that this is per vertex, and due to interpolation this will create a smooth gradient in the frag shader? i think; the interpolation happens somewhere
    output.sv_position = float4(input.position, 0.0, 1.0);
    if (input.number > 10)
    {
        float r = input.colour[2];
        float g = input.colour[0];
        float b = input.colour[1];
        float3 new_colour = { r, g, b };

        output.color = new_colour;
    }
    else
    {
        output.color = input.colour;
    }
    return output;
}

float3 estimate_normal(float3 p, float epsilon, float power)
{
    float dx = mandelbulb_msd(p + float3(epsilon, 0, 0), power)
             - mandelbulb_msd(p - float3(epsilon, 0, 0), power);

    float dy = mandelbulb_msd(p + float3(0, epsilon, 0), power)
             - mandelbulb_msd(p - float3(0, epsilon, 0), power);

    float dz = mandelbulb_msd(p + float3(0, 0, epsilon), power)
             - mandelbulb_msd(p - float3(0, 0, epsilon), power);

    return normalize(float3(dx, dy, dz));
}

[[vk::push_constant]]
ImageProperties properties;

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target
{

    float3 bulb_colour = {0.8, 0.23, 0.32};
    float4 nothing_colour = { 0.05, 0.05, 0.05, 1.0 };

    float power = 15.0;
    float bailout = 20.0;
    int precision = 150;

    float TOO_FAR = 15;

    float3 ray_dir = get_norm_ray_direction(properties.win_x, properties.win_y, { inVert.sv_position[0], inVert.sv_position[1] });

    float3 ray_position = properties.ray_origin;

    for (int i = 0; i < 10000; i++)
    {
        float distance = mandelbulb_msd(
            ray_position,
            properties.power 
        );

        if (isnan(distance))
            break;

        if (distance < 1e-04)
        {
            float3 normal = estimate_normal(ray_position + distance * ray_dir, 0.01, properties.power);

            normal = (normal + 1.0) / 2.0;

            return float4(normal, 1.0);
        }

        if (distance >= TOO_FAR)
            return nothing_colour;

        ray_position += distance * ray_dir;
    }

    return nothing_colour;

}
