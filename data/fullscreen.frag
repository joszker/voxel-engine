#version 450


layout (location = 0) in vec2 v_FragPos;

layout(location = 0) out vec4 FragColor;

void main() 
{
    FragColor = vec4(v_FragPos, 1.0, 1.0);
}