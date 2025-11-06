#version 450


layout(location = 0) in vec2 v_FragPos;
layout(location = 0) out vec4 FragColor;



void main() 
{
    float dist = length(vec2(0.5) - v_FragPos);

    FragColor = vec4(vec3(1.0 - step(0.25, dist)), 1.0);
}