@module sgl

@include common.glsl

@vs vs
uniform vs_params {
    mat4 mvp;
    mat4 tm;
};
in vec4 position;
in vec2 texcoord0;
in vec4 color0;
in float psize;
out vec4 uv;
out vec4 color;
out vec3 pos_w;
void main() {
    gl_Position = mvp * position;
    pos_w = gl_Position.xyz;
    gl_PointSize = 10.0;
    uv = tm * vec4(texcoord0, 0.0, 1.0);
    color = color0;
}
@end

@fs fs
@include_block common
uniform texture2D tex;
uniform sampler smp;
in vec4 uv;
in vec4 color;
in vec3 pos_w;

layout(location=0) out vec4 frag_color;
layout(location=1) out vec4 frag_pos_w_id;
layout(location=2) out vec4 frag_pos_v_index_type_flags;
layout(location=3) out vec4 frag_normal_uv;

void main() {
    frag_color = vec4((texture(sampler2D(tex, smp), uv.xy) * color).rgb, 1.0);
    frag_pos_w_id = vec4(pos_w, 0.0);
    frag_pos_v_index_type_flags = vec4(vec3(0), intBitsToFloat(RENDER_FLAG_SGL));
    frag_normal_uv = vec4(0);
}
@end

@program program vs fs
