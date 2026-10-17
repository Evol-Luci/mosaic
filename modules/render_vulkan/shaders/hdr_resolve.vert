#version 450

void main()
{
    // Fullscreen triangle from gl_VertexIndex, the same three vertices screen_effect.vert
    // uses. ITS OWN SHADER, and not a reuse of that one, for a single reason: it emits NO
    // UV. screen_effect.vert derives its UV from the clip position, which silently
    // inherits the negative-height (Y-flipped) viewport every pass in this backend sets,
    // so anything sampling with it reads upside down. A radially symmetric vignette never
    // revealed that. A photograph of the scene would. hdr_resolve.frag takes its UV from
    // gl_FragCoord instead, which is framebuffer space and so correct by construction.
    vec2 pos = vec2((gl_VertexIndex == 1) ? 3.0 : -1.0, (gl_VertexIndex == 2) ? 3.0 : -1.0);
    gl_Position = vec4(pos, 0.0, 1.0);
}
