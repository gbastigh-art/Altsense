@module clear

@include ctypes.glsl

@vs vs
in vec2 position;
in vec2 texcoord0;

uniform vs_params {
	mat4 model;
    mat4 view;
    mat4 proj;
};

void main() {
    gl_Position = proj * view * model * vec4(position, 0.0, 1.0);
}
@end

@fs fs

out vec4 frag_color;

void main() {
    frag_color = vec4(0);
    gl_FragDepth = 1;
}
@end

@program program vs fs
