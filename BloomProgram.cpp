#include "BloomProgram.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

Load< BloomProgram > bloom_program(LoadTagEarly);

BloomProgram::BloomProgram() {
	//Compile vertex and fragment shaders using the convenient 'gl_compile_program' helper function:
	program = gl_compile_program(
		//vertex shader:
		"#version 330\n"
		"uniform mat4 OBJECT_TO_CLIP;\n"
		"in vec4 Position;\n"
		"in vec4 Color;\n"
		"in vec2 UV;\n"
		"out vec4 color;\n"
		"out vec4 position;\n"
		"out vec2 uv;\n"
		"void main() {\n"
		"	gl_Position = OBJECT_TO_CLIP * Position;\n"
		"	color = Color;\n"
		"   position = Position;\n"
		"   uv = UV;\n"
		"}\n"
	,
		//fragment shader:
		"#version 330\n"
		"in vec4 color;\n"
		"in vec4 position;\n"
		"in vec2 uv;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"   float distance = length(uv);"
		"	fragColor = vec4(color.rgb, color.a*(pow(0.01, distance) - 0.01));\n"
		"}\n"
	);
	//As you can see above, adjacent strings in C/C++ are concatenated.
	// this is very useful for writing long shader programs inline.

	//look up the locations of vertex attributes:
	Position_vec4 = glGetAttribLocation(program, "Position");
	Color_vec4 = glGetAttribLocation(program, "Color");
	UV_vec2 = glGetAttribLocation(program, "UV");

	//look up the locations of uniforms:
	OBJECT_TO_CLIP_mat4 = glGetUniformLocation(program, "OBJECT_TO_CLIP");
}

BloomProgram::~BloomProgram() {
	glDeleteProgram(program);
	program = 0;
}

