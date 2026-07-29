// see https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom

@module upsample

@include ctypes.glsl
@include common.glsl

@vs vs
@include_block common

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
@include_block common

uniform sampler smp;
uniform texture2D src;

in vec2 uv;

layout(location=0) out vec4 frag_color;

void main() {
       // The filter kernel is applied with a radius, specified in texture
    // coordinates, so that the radius will vary across mip resolutions.
    float x = 0.005;
    float y = 0.005;

    // Take 9 samples around current texel:
    // a - b - c
    // d - e - f
    // g - h - i
    // === ('e' is the current texel) ===
    vec4 a = texture(sampler2D(src, smp), vec2(uv.x - x, uv.y + y));
    vec4 b = texture(sampler2D(src, smp), vec2(uv.x,     uv.y + y));
    vec4 c = texture(sampler2D(src, smp), vec2(uv.x + x, uv.y + y));

    vec4 d = texture(sampler2D(src, smp), vec2(uv.x - x, uv.y));
    vec4 e = texture(sampler2D(src, smp), vec2(uv.x,     uv.y));
    vec4 f = texture(sampler2D(src, smp), vec2(uv.x + x, uv.y));

    vec4 g = texture(sampler2D(src, smp), vec2(uv.x - x, uv.y - y));
    vec4 h = texture(sampler2D(src, smp), vec2(uv.x,     uv.y - y));
    vec4 i = texture(sampler2D(src, smp), vec2(uv.x + x, uv.y - y));

    // Apply weighted distribution, by using a 3x3 tent filter:
    //  1   | 1 2 1 |
    // -- * | 2 4 2 |
    // 16   | 1 2 1 |
    frag_color = e*4.0;
    frag_color += (b+d+f+h)*2.0;
    frag_color += (a+c+g+i);
    frag_color *= 1.0 / 16.0; 
}

@end

@program program vs fs
