#include "DrawBloom.hpp"
#include "BloomProgram.hpp"

#include "gl_errors.hpp"
#include "glm/ext.hpp"
#include "glm/gtx/string_cast.hpp"

#include <glm/gtc/type_ptr.hpp>

static GLuint vertex_buffer = 0;
static GLuint vertex_buffer_for_bloom_program = 0;

static Load< void > setup_buffers(LoadTagDefault, [](){
	{ //set up vertex buffer:
		glGenBuffers(1, &vertex_buffer);
		//for now, buffer will be un-filled.
	}

	{ //vertex array mapping buffer for color_program:
		//ask OpenGL to fill vertex_buffer_for_color_program with the name of an unused vertex array object:
		glGenVertexArrays(1, &vertex_buffer_for_bloom_program);

		//set vertex_buffer_for_color_program as the current vertex array object:
		glBindVertexArray(vertex_buffer_for_bloom_program);

		//set vertex_buffer as the source of glVertexAttribPointer() commands:
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

		glVertexAttribPointer(
			bloom_program->Position_vec4, //attribute
			3, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(DrawBloom::Vertex), //stride
			(GLbyte *)0 + offsetof(DrawBloom::Vertex, Position) //offset
		);
		glEnableVertexAttribArray(bloom_program->Position_vec4);
		//[Note that it is okay to bind a vec3 input to a vec4 attribute -- the w component will be filled with 1.0 automatically]

		glVertexAttribPointer(
			bloom_program->Color_vec4, //attribute
			4, //size
			GL_UNSIGNED_BYTE, //type
			GL_TRUE, //normalized
			sizeof(DrawBloom::Vertex), //stride
			(GLbyte *)0 + offsetof(DrawBloom::Vertex, Color) //offset
		);
		glEnableVertexAttribArray(bloom_program->Color_vec4);

		glVertexAttribPointer(
			bloom_program->UV_vec2, 
			2, 
			GL_FLOAT, 
			GL_FALSE, 
			sizeof(DrawBloom::Vertex), 
			(GLbyte *)0 + offsetof(DrawBloom::Vertex, UV) //offset
		);
		glEnableVertexAttribArray(bloom_program->UV_vec2);

		//done referring to vertex_buffer, so unbind it:
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//done setting up vertex array object, so unbind it:
		glBindVertexArray(0);
	}

	GL_ERRORS(); //PARANOIA: make sure nothing strange happened during setup
});

DrawBloom::~DrawBloom() {
	if (attribs.empty()) return;

	//upload vertices to vertex_buffer:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, attribs.size() * sizeof(attribs[0]), attribs.data(), GL_STREAM_DRAW); //upload attribs array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color_program as current program:
	glUseProgram(bloom_program->program);

	//upload OBJECT_TO_CLIP to the proper uniform location:
	glUniformMatrix4fv(bloom_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(world_to_clip));

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);

	//use the mapping vertex_buffer_for_color_program to fetch vertex data:
	glBindVertexArray(vertex_buffer_for_bloom_program);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(attribs.size()));

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);
}

DrawBloom::DrawBloom(glm::mat4 const &world_to_clip_) : world_to_clip(world_to_clip_) {
}

void DrawBloom::draw_rectangle(glm::vec2 const &pos,
                        float const &size,
						glm::vec4 const &uv_rect, 
                        glm::u8vec4 const &color) {
    //draw rectangle as two CCW-oriented triangles:
	float radius = size/2.0f;
	attribs.emplace_back(glm::vec3(pos.x - radius, pos.y - radius, 0.0f), color, glm::vec2(uv_rect.x, uv_rect.y));
	attribs.emplace_back(glm::vec3(pos.x + radius, pos.y - radius, 0.0f), color, glm::vec2(uv_rect.x+uv_rect.z, uv_rect.y));
	attribs.emplace_back(glm::vec3(pos.x + radius, pos.y + radius, 0.0f), color, glm::vec2(uv_rect.x+uv_rect.z, uv_rect.y+uv_rect.w));

	attribs.emplace_back(glm::vec3(pos.x - radius, pos.y - radius, 0.0f), color, glm::vec2(uv_rect.x, uv_rect.y));
	attribs.emplace_back(glm::vec3(pos.x + radius, pos.y + radius, 0.0f), color, glm::vec2(uv_rect.x+uv_rect.z, uv_rect.y+uv_rect.w));
	attribs.emplace_back(glm::vec3(pos.x - radius, pos.y + radius, 0.0f), color, glm::vec2(uv_rect.x, uv_rect.y+uv_rect.w));
};

void DrawBloom::draw(glm::vec2 const &pos, float const &size, glm::u8vec4 const &color) {
	draw_rectangle(pos, size, glm::vec4(-1.0f, -1.0f, 2.0f, 2.0f), color);
}