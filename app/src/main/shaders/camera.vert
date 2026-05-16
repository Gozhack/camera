#version 450

layout(location = 0) out vec2 outTexCoord;

void main() {
    // Standard fullscreen triangle
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
    
    // Rotate 90 degrees clockwise for Android portrait sensor orientation
    // (u, v) -> (v, 1.0 - u)
    outTexCoord = vec2(uv.y, 1.0 - uv.x);
}
