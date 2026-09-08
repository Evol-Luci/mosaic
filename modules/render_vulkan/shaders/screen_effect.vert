#version 450

layout(location = 0) out vec2 fragUV;

void main()
{
    // Fullscreen triangle from gl_VertexIndex (0,1,2).
    vec2 pos = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0, (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
    fragUV = pos * 0.5 + 0.5;
}
