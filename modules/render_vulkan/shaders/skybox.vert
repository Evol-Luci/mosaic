#version 450

layout(location = 0) out vec2 fragNdc;

void main()
{
    // Vertexless fullscreen triangle from gl_VertexIndex, the same idiom as
    // screen_effect.vert. A cube mesh would also work, but it needs a mesh, a vertex
    // buffer and a draw with indices for something that covers the screen anyway.
    vec2 pos = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0, (gl_VertexIndex == 2) ? 3.0 : -1.0);
    fragNdc = pos;
    // z = w puts every fragment exactly on the far plane. Combined with LESS_OR_EQUAL
    // and depth writes off, this fills only pixels nothing else wrote -- so the sky is
    // behind the scene without needing to be drawn first.
    gl_Position = vec4(pos, 1.0, 1.0);
}
