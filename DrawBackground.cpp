#include "DrawBackground.hpp"
#include "data_path.hpp"
#include "load_save_png.hpp"
#include "ColorTextureProgram.hpp"

#include "gl_errors.hpp"
#include "glm/ext.hpp"
#include "glm/gtx/string_cast.hpp"

#include <glm/gtc/type_ptr.hpp>

static GLuint vertex_buffer = 0;
static GLuint vertex_buffer_for_color_texture_program = 0;
static GLuint tex = 0;
static glm::vec2 sheet_size;

static Load< void > setup_buffers(LoadTagDefault, [](){
	{ //vertex buffer:
		glGenBuffers(1, &vertex_buffer);
		//for now, buffer will be un-filled.

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}
	{ //vertex array mapping buffer for color_texture_program:
		//ask OpenGL to fill vertex_buffer_for_color_texture_program with the name of an unused vertex array object:
		glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);

		//set vertex_buffer_for_color_texture_program as the current vertex array object:
		glBindVertexArray(vertex_buffer_for_color_texture_program);

		//set vertex_buffer as the source of glVertexAttribPointer() commands:
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

		//set up the vertex array object to describe arrays of PongMode::Vertex:
		glVertexAttribPointer(
			color_texture_program->Position_vec4, //attribute
			3, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(DrawBackground::Vertex), //stride
			(GLbyte *)0 + 0 //offset
		);
		glEnableVertexAttribArray(color_texture_program->Position_vec4);
		//[Note that it is okay to bind a vec3 input to a vec4 attribute -- the w component will be filled with 1.0 automatically]

		glVertexAttribPointer(
			color_texture_program->Color_vec4, //attribute
			4, //size
			GL_UNSIGNED_BYTE, //type
			GL_TRUE, //normalized
			sizeof(DrawBackground::Vertex), //stride
			(GLbyte *)0 + 4*3 //offset
		);
		glEnableVertexAttribArray(color_texture_program->Color_vec4);

		glVertexAttribPointer(
			color_texture_program->TexCoord_vec2, //attribute
			2, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(DrawBackground::Vertex), //stride
			(GLbyte *)0 + 4*3 + 4*1 //offset
		);
		glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);

		//done referring to vertex_buffer, so unbind it:
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//done setting up vertex array object, so unbind it:
		glBindVertexArray(0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}
	
	{ // load tileset texture
		std::vector< glm::u8vec4 > data;
		glm::uvec2 size(0, 0);
		load_png(data_path("background.png"), &size, &data, LowerLeftOrigin);
		sheet_size = size;
		std::cout << "background sheet size: " << glm::to_string(sheet_size) << std::endl;

		glGenTextures(1, &tex);

		glBindTexture(GL_TEXTURE_2D, tex);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glBindTexture(GL_TEXTURE_2D, 0);

		GL_ERRORS();
	}
});

DrawBackground::~DrawBackground() {
	if (attribs.empty()) return;
	//---- actual drawing ----

	//upload vertices to vertex_buffer:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, attribs.size() * sizeof(attribs[0]), attribs.data(), GL_STREAM_DRAW); //upload vertices array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color_texture_program as current program:
	glUseProgram(color_texture_program->program);

	//upload OBJECT_TO_CLIP to the proper uniform location:
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(world_to_clip));

	//use the mapping vertex_buffer_for_color_texture_program to fetch vertex data:
	glBindVertexArray(vertex_buffer_for_color_texture_program);

	//bind the solid white texture to location zero so things will be drawn just with their colors:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(attribs.size()));

	//unbind the solid white texture:
	glBindTexture(GL_TEXTURE_2D, 0);

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);

}

DrawBackground::DrawBackground(glm::mat4 const &world_to_clip_) : world_to_clip(world_to_clip_) {
}

void DrawBackground::draw(glm::vec2 const &size, glm::u8vec4 const &color) {
	float aspect = size.y/size.x;
	// std::cout << "tilepos: " + glm::to_string(tilepos) + ", tilesize: " + glm::to_string(tilesize) << std::endl;
	attribs.emplace_back(glm::vec3(0.0f, 0.0f, 0.0f), color, glm::vec2(0.0f, 0.0f));
	attribs.emplace_back(glm::vec3(size.x, 0.0f, 0.0f), color, glm::vec2(1.0f, 0.0f));
	attribs.emplace_back(glm::vec3(size.x, size.y, 0.0f), color, glm::vec2(1.0f, 1.0f*aspect));

	attribs.emplace_back(glm::vec3(0.0f, 0.0f, 0.0f), color, glm::vec2(0.0f, 0.0f));
	attribs.emplace_back(glm::vec3(size.x, size.y, 0.0f), color, glm::vec2(1.0f, 1.0f*aspect));
	attribs.emplace_back(glm::vec3(0.0f, size.y, 0.0f), color, glm::vec2(0.0f, 1.0f*aspect));
}