#version 330 core
in vec3 v_position;
in float v_render_mode;
in float v_skeleton_id;
in vec3 vNormal;
in vec3 vWorldPos;

uniform vec3 axis_color;
uniform vec3 lightDir;   // e.g. normalized

out vec4 FragColor;

vec3 rigidBodyColor = vec3(0.15, 1.0, 0.00);
vec3 outlineColor = vec3(1.0, 1.0, 1.0);

// Utility function for color mapping
vec3 mapSkeletonColor(float skeleton_id) {
    // Generate unique colors for different skeletons
    if (skeleton_id < 0.5) return vec3(0.0, 0.7, 1.0);      // Blue
    else if (skeleton_id < 1.5) return vec3(1.0, 0.3, 0.0); // Orange
    else if (skeleton_id < 2.5) return vec3(0.0, 0.8, 0.2); // Green
    else if (skeleton_id < 3.5) return vec3(1.0, 0.0, 0.7); // Pink
    else return vec3(0.8, 0.8, 0.0);                        // Yellow
}

void main()
{
    vec3 baseColor;

    // Base color from skeleton ID
    baseColor = mapSkeletonColor(v_skeleton_id);

    vec3 N = normalize(vNormal);
    float diff = max(dot(N, -lightDir), 0.0);
    vec3 color = (0.5 + 0.7*diff) * baseColor;

    if (v_render_mode < 0.5) {
        // Bone rendering
        // depth shading
        float depthFactor = clamp(1.0 - (v_position.z * 0.2), 0.7, 1.0);
        color *= depthFactor;

        FragColor = vec4(color, 1.0);
    } else if (v_render_mode < 1.5) {
        // Joint rendering 
        // brighter color for joints
        FragColor = vec4(color * 1.3, 1.0);
    } else if (v_render_mode < 2.5) {
        // Minor gridlines
        FragColor = vec4(0.25, 0.25, 0.25, 1.0);
    } else if (v_render_mode < 3.5) {
        // Major gridlines
        FragColor = vec4(0.55, 0.55, 0.55, 1.0);
    } else if (v_render_mode < 4.5) {
        // axis indicator
        FragColor = vec4(axis_color, 1.0);
    } else if (v_render_mode < 5.5) {
        // non-selected rigid body
        FragColor = vec4(rigidBodyColor, 1.0);
    } else {
        // selected rigid body
        FragColor = vec4(outlineColor, 1.0);
    }
}