@module edge

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

layout(location=0) out float frag_color;

void make_kernel(out vec4 n[9], vec2 coord) {
    const vec2 unit = 1.0 / textureSize(sampler2D(image, smp), 0);

	float w = unit.x, h = unit.y;

	n[0] = texture(sampler2D(image, smp), coord + vec2( -(w * 2), -(h * 2)));
	n[1] = texture(sampler2D(image, smp), coord + vec2(0.0, -h));
	n[2] = texture(sampler2D(image, smp), coord + vec2(  w, -h));
	n[3] = texture(sampler2D(image, smp), coord + vec2( -w, 0.0));
	n[4] = texture(sampler2D(image, smp), coord);
	n[5] = texture(sampler2D(image, smp), coord + vec2(  w, 0.0));
	n[6] = texture(sampler2D(image, smp), coord + vec2( -w, h));
	n[7] = texture(sampler2D(image, smp), coord + vec2(0.0, h));
	n[8] = texture(sampler2D(image, smp), coord + vec2(  w, h));

    for (int i = 0; i < 9; i++) {
        const int id = floatBitsToInt(n[i].y);
        const int extra = floatBitsToInt(n[i].x);
        n[i] = vec4(((id >> 16) == _LT_ENTITY) && ((extra & MRF_ENEMY) != 0) ? 1 : 0);
    }
}

void main() {
	vec4 n[9];
	make_kernel(n, uv);

	vec4 sobel_edge_h = n[2] + (2.0*n[5]) + n[8] - (n[0] + (2.0*n[3]) + n[6]);
  	vec4 sobel_edge_v = n[0] + (2.0*n[1]) + n[2] - (n[6] + (2.0*n[7]) + n[8]);
	vec4 sobel = sqrt((sobel_edge_h * sobel_edge_h) + (sobel_edge_v * sobel_edge_v));

	frag_color = dot(sobel.rgb, vec3(1));
}

@end

@program program vs fs
