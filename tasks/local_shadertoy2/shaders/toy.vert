#version 450

layout (location = 0) out VS_OUT {
    vec2 pos;
} vOut;

out gl_PerVertex { vec4 gl_Position; };
void main(void)
{
    vOut.pos = 
        vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2) * 2.0 - 
        vec2(1.0);

    gl_Position = vec4(vOut.pos, 0.0, 1.0);
}