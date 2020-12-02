#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include "GL.hpp"

#include <string>
#include <vector>


struct DrawBloom {
	//Start drawing; will remember world_to_clip matrix:
	DrawBloom(glm::mat4 const &world_to_clip);

	void draw(glm::vec2 const &pos, float const &size, glm::u8vec4 const &color);
    void draw_rectangle(glm::vec2 const &pos,
                        float const &size,
                        glm::vec4 const &uv_rect,
                        glm::u8vec4 const &color);

	//Finish drawing (push attribs to GPU):
	~DrawBloom();

    glm::mat4 world_to_clip;
	struct Vertex {
		Vertex(glm::vec3 const &Position_, glm::u8vec4 const &Color_, glm::vec2 const &UV_) : 
                Position(Position_), Color(Color_), UV(UV_) { }
		glm::vec3 Position;
		glm::u8vec4 Color;
        glm::vec2 UV;
	};
	std::vector< Vertex > attribs;
};


