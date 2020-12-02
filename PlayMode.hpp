#include "Mode.hpp"

#include "Connection.hpp"
#include "Sound.hpp"

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
	const uint8_t NUM_ROWS = 20;
	const uint8_t NUM_COLS = 40;
	const float TILE_SIZE = 20.0f;
	const float BORDER_SIZE = 0.1f * TILE_SIZE;
	const uint8_t TRAIL_MAX_LEN = 50;
	const uint8_t TRAIL_POWERUP_LEN = 20;
	const uint32_t WIN_THRESHOLD = NUM_ROWS * NUM_COLS / 2;

	const float GRID_W = NUM_COLS * TILE_SIZE;
	const float GRID_H = NUM_ROWS * TILE_SIZE;
	const float PADDING = 50.0f;
	const glm::uvec2 WINDOW_SIZE = glm::uvec2(1280, 720);

	enum PowerupType { speed, trail, no_powerup };
	// ll, rr, uu, dd are for player with speed powerup
	enum Dir { left, right, up, down, ll, rr, uu, dd, none };

	const uint32_t white_color = 0xffffffff;
	const uint32_t black_color = 0x000000ff;
	// const uint32_t base_color = 0x381d35ff; // initial color of tiles
	const uint32_t base_color = 0x0a1c29ff;
	// const uint32_t bg_color = 0x404040ff;
	const uint32_t bg_color = black_color;
	const uint32_t border_color = 0xd5cdd8ff;
	const std::unordered_map<PowerupType, uint32_t> powerup_colors{{speed, 0xcb5ab2ff},
																	{trail, 0x88c7ffff}};
	const std::vector<uint32_t> player_colors{0x390099ff, 0xffbd00ff, 0xff5400ff, 0x9e0059ff};
	const std::vector<uint32_t> trail_colors{0x8762c5ff,  0xffdf83ff, 0xffa980ff, 0xca679fff};

	//----- game state -----
	float total_elapsed = 0.0f;

	enum GameState { QUEUEING, IN_GAME };
	GameState gameState = QUEUEING;
	uint8_t lobby_size = 0;

	bool GAME_OVER = false;
	uint8_t winner_id;
	size_t winner_score = 0;

	int n_powerups = 2;
	float powerup_cd = 10.0f;
	bool start_cd = true;
	struct Powerup {
		Powerup(PowerupType _type) : type(_type) { }
		PowerupType type;
		float frame = 0.0f;
	};

	struct Tile
	{
		Tile(uint32_t _color, uint8_t _age) : color(_color), age(_age) { }
		uint32_t color;
		uint8_t age; // for trail tiles
		Powerup powerup = Powerup(no_powerup);
	};

	std::vector<std::vector<Tile>> tiles; // logical representation of game state
	std::vector<std::vector<uint32_t>> visual_board; // visual representation of game state
	uint8_t horizontal_border, vertical_border; // "walls"
	uint8_t start_countdown = 0;

	struct Player
	{
		Player(uint8_t _id, uint32_t _color, glm::uvec2 _pos) : id(_id), color(_color), pos(_pos) {}
		uint8_t id;
		uint32_t color;
		uint32_t area = 0;
		glm::uvec2 pos;
		glm::uvec2 prev_pos[2]; // previous 2 positions (for calculating loops)
		PowerupType powerup_type = no_powerup;
		Dir dir = none; // current facing direction for sprite rendering
		// prev_pos[0] = position 1 new position ago
		// prev_pos[1] = position 2 new positions ago
		std::shared_ptr< Sound::PlayingSample > walk_sound = nullptr;
		float walk_frame = 1.0f;
	};
	std::unordered_map<uint8_t, Player> players;
	uint8_t local_id; // player corresponding to this connection

	//connection to server:
	Client &client;

	// ----- texture ------
	GLuint vertex_buffer = 0;
	GLuint vertex_buffer_for_color_texture_program = 0;
	GLuint sprite_tex = 0;
	glm::vec2 sprite_sheet_size;
	const float SPRITE_SIZE = 16.0f;

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

	void create_player(uint8_t id, Dir dir, glm::uvec2 pos);
	void update_player(Player *p, Dir dir, glm::uvec2 pos, float elapsed);
	void update_sound(Player* p, bool moving, float elapsed);

	void draw_rectangle(glm::vec2 const &pos,
						glm::vec2 const &size,
						glm::u8vec4 const &color,
						std::vector<Vertex> &vertices);
	void draw_borders(glm::u8vec4 const &color,
					  std::vector<Vertex> &vertices);

	void draw_tiles(std::vector<Vertex> &vertices);
	void draw_players(std::vector<Vertex> &vertices);
	void draw_texture(std::vector< Vertex >& vertices, glm::vec2 pos, glm::vec2 size, glm::vec2 tilepos, glm::vec2 tilesize, glm::u8vec4 color);
	void draw_text(std::vector< Vertex >& vertices, std::string msg, glm::vec2 anchor, glm::u8vec4 color);

	void new_powerup();
	void update_powerup(float elapsed);

	void win_game(uint8_t id, uint32_t area);
	std::vector<glm::uvec2> shortest_path(glm::uvec2 const &start, glm::uvec2 const &end, std::vector<glm::uvec2> const &allowed_tiles);
	void floodfill(std::vector<std::vector<uint32_t>> &grid, uint32_t x, uint32_t y, uint32_t new_color, uint32_t old_color);
	void fill_interior(uint32_t color, uint32_t &delta_size, uint32_t &total_size);
	void update_areas();
};
