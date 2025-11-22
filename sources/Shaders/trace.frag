#version 450


layout(location = 0) in vec2 v_FragPos;

layout(location = 0) out vec4 FragColor;


layout(std140, binding = 0) uniform Uniform
{
    vec4 u_Resolution;
    vec4 u_Color;
    vec4 u_LightPos;
    vec4 u_LightColor;
    vec4 u_Ambient;
    
    vec4 u_CameraPos;
    vec4 u_CameraDir;       // direction (Z-basis)
    vec4 u_CameraRight;     // X-basis
    vec4 u_CameraUp;        // Y-basis
};

layout(binding = 1) uniform sampler2D u_Texture;


#define MAX_STEPS 128


bool block[8 * 8 * 8] = bool[](
    true, true,  true,  true,  true,  true,  true,  true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, true,  true,  true,  true,  true,  true,  true,

    true,  false, false, false, false, false, false, true,
    false, true,  false, false, false, false, true,  false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, true,  false, false, false, false, true,  false,
    true,  false, false, false, false, false, false, true,

    true,  false, false, false, false, false, false, true,
    false, false, false, false, false, false, false, false,
    false, false, true,  false, false, true,  false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, true,  false, false, true,  false, false,
    false, false, false, false, false, false, false, false,
    true,  false, false, false, false, false, false, true,

    true,  false, false, false, false, false, false, true,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, true,  true,  false, false, false,
    false, false, false, true,  true,  false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    true,  false, false, false, false, false, false, true,

    true,  false, false, false, false, false, false, true,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, true,  true,  false, false, false,
    false, false, false, true,  true,  false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    true,  false, false, false, false, false, false, true,

    true,  false, false, false, false, false, false, true,
    false, false, false, false, false, false, false, false,
    false, false, true,  false, false, true,  false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, true,  false, false, true,  false, false,
    false, false, false, false, false, false, false, false,
    true,  false, false, false, false, false, false, true,

    true,  false, false, false, false, false, false, true,
    false, true,  false, false, false, false, true,  false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false,
    false, true,  false, false, false, false, true,  false,
    true,  false, false, false, false, false, false, true,

    true, true,  true,  true,  true,  true,  true,  true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, false, false, false, false, false, false, true,
    true, true,  true,  true,  true,  true,  true,  true
);


bool GetVoxel(ivec3 pos)
{
    pos += ivec3(0, 0, 2);

    if (all(greaterThanEqual(pos, ivec3(0))) && all(lessThan(pos, ivec3(8))))
        return block[pos.y * 8 * 8 + pos.z * 8 + pos.x];
        
    return false;
}

vec3 GetStepMask(vec3 sideDist)
{
    bvec3 mask;
    bvec3 prio = lessThan(sideDist.xyz, sideDist.yzx);

    mask.x = prio.x && !prio.z;       // (T.x < T.y) && (T.x <= T.z)
    mask.y = prio.y && !prio.x;       // (T.y < T.z) && (T.y <= T.x)
    mask.z = !(mask.x || mask.y);

    return vec3(mask);
}


void main() 
{
    vec2 uv = vec2(v_FragPos.x, 1.0 - v_FragPos.y); // UV-coordinates (from 0 to 1)
    vec2 screenPos = uv * 2.0 - 1.0;                // homogeneous clip space coordinates
	vec3 cameraPlaneU = u_CameraRight.xyz;
	vec3 cameraPlaneV = u_CameraUp.xyz * (u_Resolution.y / u_Resolution.x);
	vec3 rayDir = u_CameraDir.xyz + screenPos.x * cameraPlaneU + screenPos.y * cameraPlaneV;
	vec3 rayPos = u_CameraPos.xyz;

    vec3 color = vec3(0.0);

    /********************************* VOXEL DDA RAY CASTING *********************************/
    // Based on the 3D-DDA-nc method from DOI:10.1118/1.4767755
    const vec3 raySign = sign(rayDir);
    const vec3 deltaDist = 1.0 / rayDir;

    vec3 incr;
    vec3 mapPos = floor(rayPos);
    vec3 sideDist = ((mapPos - rayPos) + 0.5 + raySign * 0.5) * deltaDist;

    for (int i = 0; i < MAX_STEPS; i++)
    {
        incr = GetStepMask(sideDist) * raySign;
        mapPos += incr;
    
        if (GetVoxel(ivec3(mapPos)))
        {
            color = u_Color.xyz;
            break;
        }
        sideDist += incr * deltaDist;
    }
    /****************************************************************************************/

    vec3 normal = -incr;//mask * -raySign;

    // Ambient lighting
    vec3 ambient = u_Ambient.rgb * dot(abs(incr), vec3(0.5, 1.0, 0.75));

    // Diffuse lighting
    vec3 lightDir = normalize(u_LightPos.xyz - mapPos);
    vec3 diffuse = max(dot(normal, lightDir), 0.0) * u_LightColor.rgb;
    color *= (ambient + diffuse);

    // Display normals
    //color *= (normal + 1.0) / 2.0;

    FragColor = vec4(color, 1.0);
}