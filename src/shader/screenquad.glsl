@module screenquad

@include ctypes.glsl

@vs vs
in vec2 position;
in vec2 texcoord0;

uniform vs_params {
	mat4 model;
    mat4 view;
    mat4 proj;
};

out vec2 uv;
void main() {
    gl_Position = proj * view * model * vec4(position, 0.0, 1.0);
    uv = texcoord0;
}
@end

@fs fs

uniform sampler smp;
uniform texture2D tex;

uniform fs_params {
    vec4 tint;

    // 0..1, where 0 is opaque and 1 is transparent
    float transparency;
};

in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = texture(sampler2D(tex, smp), uv);
    frag_color.rgb = mix(frag_color.rgb, tint.rgb, tint.a);
    frag_color.a *= 1.0 - transparency;
}
@end

@program program vs fs
