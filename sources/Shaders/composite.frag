#version 450


layout(location = 0) in vec2 v_FragPos;
layout(location = 0) out vec4 FragColor;


layout(binding = 0) uniform Data
{
    vec3 Color;
    float Tiling;

} u_Data;

layout(binding = 1) uniform sampler2D u_Texture;


void main()
{
    vec2 uv = mod(v_FragPos * u_Data.Tiling, vec2(1.0));

    FragColor = vec4(u_Data.Color * texture(u_Texture, uv).rgb, 1.0);
}