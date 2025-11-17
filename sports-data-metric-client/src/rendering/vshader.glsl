#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
uniform mat4 mvp_matrix;
uniform int render_mode;
uniform float skeleton_id;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

out vec3 vNormal;
out vec3 vWorldPos;
out vec3 v_position;
out float v_render_mode;
out float v_skeleton_id;

void main()
{

    // Pass position and render mode to fragment shader
    v_position = a_position;
    v_render_mode = float(render_mode);
    v_skeleton_id = skeleton_id;
    
    vec4 worldPos = model * vec4(a_position, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal   = mat3(transpose(inverse(model))) * a_normal;
    gl_Position = proj * view * worldPos;

}