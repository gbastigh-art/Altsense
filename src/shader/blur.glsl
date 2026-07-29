@module blur

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
uniform texture2D image;

in vec2 uv;

uniform fs_params {
    int horizontal;
};

layout(location=0) out vec4 frag_color;

#define RADIUS    3

void main() {
    const vec2 size = textureSize(sampler2D(image, smp), 0);

    vec4 result = vec4(0.0, 0.0, 0.0, 0.0);

    const float
        w0 = 0.5135 / pow(RADIUS, 0.96),
        coeff = 1.0 / (2.0 * RADIUS * RADIUS);

    vec2 p = uv;
    int x, y;
    float d;

    if (horizontal == 1) {
        for (d = 1.0 / size.x, x = -RADIUS, p.x += x * d;
             x <= RADIUS;
             x += 1, p.x += d) {
            result += texture(sampler2D(image, smp), p) * w0 * exp((-x * x) * coeff);
        }
    } else {
        for (d = 1.0 / size.y, y = -RADIUS, p.y += y * d;
             y <= RADIUS;
             y += 1, p.y += d) {
            result += texture(sampler2D(image, smp), p) * w0 * exp((-y * y) * coeff);
        }
    }
 
     frag_color = result;
}

@end

@program program vs fs
