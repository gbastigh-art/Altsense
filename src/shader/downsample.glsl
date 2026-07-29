// see https://learnopengl.com/Guest-Articles/2022/Phys.-Based-Bloom

@module downsample

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
    vec2 srcTexelSize = 1.0 / textureSize(sampler2D(src, smp), 0);
    float x = srcTexelSize.x;
    float y = srcTexelSize.y;

    // Take 13 samples around current texel:
    // a - b - c
    // - j - k -
    // d - e - f
    // - l - m -
    // g - h - i
    // === ('e' is the current texel) ===
    vec4 a = texture(sampler2D(src, smp), vec2(uv.x - 2*x, uv.y + 2*y));
    vec4 b = texture(sampler2D(src, smp), vec2(uv.x,       uv.y + 2*y));
    vec4 c = texture(sampler2D(src, smp), vec2(uv.x + 2*x, uv.y + 2*y));

    vec4 d = texture(sampler2D(src, smp), vec2(uv.x - 2*x, uv.y));
    vec4 e = texture(sampler2D(src, smp), vec2(uv.x,       uv.y));
    vec4 f = texture(sampler2D(src, smp), vec2(uv.x + 2*x, uv.y));

    vec4 g = texture(sampler2D(src, smp), vec2(uv.x - 2*x, uv.y - 2*y));
    vec4 h = texture(sampler2D(src, smp), vec2(uv.x,       uv.y - 2*y));
    vec4 i = texture(sampler2D(src, smp), vec2(uv.x + 2*x, uv.y - 2*y));

    vec4 j = texture(sampler2D(src, smp), vec2(uv.x - x, uv.y + y));
    vec4 k = texture(sampler2D(src, smp), vec2(uv.x + x, uv.y + y));
    vec4 l = texture(sampler2D(src, smp), vec2(uv.x - x, uv.y - y));
    vec4 m = texture(sampler2D(src, smp), vec2(uv.x + x, uv.y - y));

    // Apply weighted distribution:
    // 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
    // a,b,d,e * 0.125
    // b,c,e,f * 0.125
    // d,e,g,h * 0.125
    // e,f,h,i * 0.125
    // j,k,l,m * 0.5
    // This shows 5 square areas that are being sampled. But some of them overlap,
    // so to have an energy preserving downsample we need to make some adjustments.
    // The weights are the distributed, so that the sum of j,k,l,m (e.g.)
    // contribute 0.5 to the final color output. The code below is written
    // to effectively yield this sum. We get:
    // 0.125*5 + 0.03125*4 + 0.0625*4 = 1
    frag_color = e*0.125;
    frag_color += (a+c+g+i)*0.03125;
    frag_color += (b+d+f+h)*0.0625;
    frag_color += (j+k+l+m)*0.125;
}

@end

@program program vs fs
