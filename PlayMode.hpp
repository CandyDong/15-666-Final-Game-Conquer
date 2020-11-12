#include "Mode.hpp"

#include "Connection.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>
#include "GL.hpp"

#include <vector>
#include <deque>
#include <unordered_map>
#include <set>

//#define DEBUG_TRAIL

struct PlayMode : Mode
{
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
	const float BORDER_SIZE = 0.1f * TILE_SIZE;
	const uint8_t TRAIL_MAX_LEN = 50;
	const uint32_t WIN_THRESHOLD = NUM_ROWS * NUM_COLS / 2;

	const float GRID_W = NUM_COLS * TILE_SIZE;
	const float GRID_H = NUM_ROWS * TILE_SIZE;
	const float PADDING = 50.0f;

	const uint32_t bg_color = 0x404040ff;
	const uint32_t white_color = 0xffffffff;
	const uint32_t base_color = 0xe4ded2ff; // initial color of tiles
	const uint32_t border_color = 0xd5cdd8ff;
	const std::vector<uint32_t> player_colors{0x390099ff, 0x9e0059ff, 0xff5400ff, 0xffbd00ff};
	const std::vector<uint32_t> trail_colors{0x8762c5ff, 0xca679fff, 0xffa980ff, 0xffdf83ff};

	//----- game state -----
	bool GAME_OVER = false;
	uint8_t winner_id;
	size_t winner_score = 0;

	struct Tile
	{
		Tile(uint32_t _color, uint8_t _age) : color(_color), age(_age) { }
		uint32_t color;
		uint8_t age; // for trail tiles
	};

	std::vector<std::vector<Tile>> tiles; // logical representation of game state
	std::vector<std::vector<uint32_t>> visual_board; // visual representation of game state

	struct Player
	{
		Player(uint8_t _id, uint32_t _color, glm::uvec2 _pos) : id(_id), color(_color), pos(_pos) {}
		uint8_t id;
		uint32_t color;
		glm::uvec2 pos;
		glm::uvec2 prev_pos[2]; // previous 2 positions (for calculating loops)
		// prev_pos[0] = position 1 new position ago
		// prev_pos[1] = position 2 new positions ago
	};
	std::unordered_map<uint8_t, Player> players;
	uint8_t local_id; // player corresponding to this connection

	enum Dir { left, right, up, down, none };

	//connection to server:
	Client &client;

	// ----- texture ------
	GLuint vertex_buffer = 0;
	GLuint vertex_buffer_for_color_texture_program = 0;

	struct Vertex
	{
		Vertex(glm::vec3 const &Position_, glm::u8vec4 const &Color_, glm::vec2 const &TexCoord_) : Position(Position_), Color(Color_), TexCoord(TexCoord_) {}
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};

	//Solid white texture:
	GLuint white_tex = 0;

	// ------ helpers -------
	glm::u8vec4 hex_to_color_vec(int color_hex);
	void init_tiles();

	void create_player(uint8_t id, glm::uvec2 pos);
	void update_player(Player *p, glm::uvec2 pos);

	void draw_rectangle(glm::vec2 const &pos,
						glm::vec2 const &size,
						glm::u8vec4 const &color,
						std::vector<Vertex> &vertices);
	void draw_borders(glm::u8vec4 const &color,
					  std::vector<Vertex> &vertices);

	void draw_tiles(std::vector<Vertex> &vertices);
	void draw_players(std::vector<Vertex> &vertices);

	void win_game(uint8_t id, uint32_t area);
	std::vector<glm::uvec2> shortest_path(glm::uvec2 const &start, glm::uvec2 const &end, std::vector<glm::uvec2> const &allowed_tiles);
	void floodfill(std::vector<std::vector<uint32_t>> &grid, uint32_t x, uint32_t y, uint32_t new_color, uint32_t old_color);
	uint32_t fill_interior(uint32_t color);
	uint32_t fill_interior_discard_extra(uint32_t trail_color, uint32_t territory_color);
};
