#include "Mode.hpp"

#include "Connection.hpp"

#include <glm/glm.hpp>
#include "GL.hpp"

#include <vector>
#include <deque>
#include <unordered_map>

struct PlayMode : Mode {
	PlayMode(Client &client);
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- constants ------
	const uint8_t NUM_ROWS = 50;
	const uint8_t NUM_COLS = 80;
	const float TILE_SIZE = 20.0f;
	const float BORDER_SIZE = 0.1*TILE_SIZE;

	const float GRID_W = NUM_COLS*TILE_SIZE;
	const float GRID_H = NUM_ROWS*TILE_SIZE;
	const float PADDING = 50.0f;
	
	const int bg_color = 0x404040ff;
	const int white_color = 0xffffffff;
	const int base_color = 0xe4ded2ff; // initial color of tiles
	const int border_color = 0xd5cdd8ff;
	const std::vector<uint32_t> player_colors{0x390099ff, 0x9e0059ff, 0xff0054ff, 0xff5400ff, 0xffbd00ff};

	enum class TileType {EMPTY_TILE = 0, PLAYER_TILE};

	//----- game state -----
	//map:
	struct Tile {
		Tile(TileType _type, uint8_t _index, glm::vec2 _pos,glm::u8vec4 _color): 
			type(_type), index(_index), pos(_pos), color(_color) { }
		TileType type = TileType::EMPTY_TILE;
		uint8_t index = -1; // always -1 for EMPTY_TILE, player No. for PLAYER_TILE
		glm::vec2 pos;
		glm::u8vec4 color; // white for EMPTY_TILE, else the same as player color
	};
	std::vector<Tile> tiles;

	struct Player {
		Player(uint8_t _id, std::string _name, glm::u8vec4 _color, uint8_t _row, uint8_t _col):
				id(_id), name(_name), color(_color), row(_row), col(_col) { }
		uint8_t id;
		std::string name;
		glm::u8vec4 color;
		uint8_t row;
		uint8_t col;
	};
	std::unordered_map<uint8_t, Player> players;

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} left, right, down, up;

	//connection to server:
	Client &client;

	// ----- texture ------
	GLuint vertex_buffer = 0;
	GLuint vertex_buffer_for_color_texture_program = 0;

	struct Vertex {
		Vertex(glm::vec3 const &Position_, glm::u8vec4 const &Color_, glm::vec2 const &TexCoord_) :
			Position(Position_), Color(Color_), TexCoord(TexCoord_) { }
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};

	//Solid white texture:
	GLuint white_tex = 0;

	// ------ helpers -------
	glm::u8vec4 hex_to_color_vec(int color_hex);
	void init_tiles();
	void draw_rectangle(glm::vec2 const &pos, 
                        	glm::vec2 const &size, 
                        	glm::u8vec4 const &color, 
                        	std::vector<Vertex> &vertices);
	void draw_borders(glm::u8vec4 const &color, 
					std::vector<Vertex> &vertices);

	void draw_tiles(std::vector<Vertex> &vertices);
	
};
