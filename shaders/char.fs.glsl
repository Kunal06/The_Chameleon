#version 330

// from vertex shader
in vec2 texcoord;

out vec3 vcolor;
out vec2 vpos;

// application data
uniform sampler2D sampler0;
uniform vec3 fcolor;

// color change
uniform float color_change;
uniform bool is_alive;

// output color
layout(location = 0) out vec4 color;

void main()
{
	color = vec4(fcolor, 1.0) * texture(sampler0, vec2(texcoord.x, texcoord.y));
	vec3 char_color = color.xyz;
	if (is_alive) {
		if (color_change == 1.0) {
			char_color = vec3(color.x * 1.00, color.y * 0.50, color.z * 0.50);
		} else if (color_change == 2.0) {
			char_color = vec3(color.x * 0.50, color.y * 1.00, color.z * 0.50);
		} else if (color_change == 3.0) {
			char_color = vec3(color.x * 0.50, color.y * 0.50, color.z * 1.00);
		} else if (color_change == 4.0) {
			char_color = vec3(color.x * 1.00, color.y * 1.00, color.z * 0.50);
		}
	}
	else {
		char_color = vec3(0.00, 0.00, 0.00);
	}
	color.xyz = char_color;
}
