#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include "GL.hpp"

#include <string>
#include <vector>

struct DrawBackground {
	//Start drawing; will remember world_to_clip matrix:
	DrawBackground(glm::mat4 const &world_to_clip);

	void draw(glm::vec2 const &size, glm::u8vec4 const &color);

	//Finish drawing (push attribs to GPU):
	~DrawBackground();
	
	
    glm::mat4 world_to_clip;
	struct Vertex
	{
		Vertex(glm::vec3 const &Position_, glm::u8vec4 const &Color_, glm::vec2 const &TexCoord_) : 
				Position(Position_), Color(Color_), TexCoord(TexCoord_) {}
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	std::vector< Vertex > attribs;
};


