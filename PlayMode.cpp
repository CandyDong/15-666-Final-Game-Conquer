#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"
#include "ColorTextureProgram.hpp"
#include "glm/ext.hpp"
#include "glm/gtx/string_cast.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <queue>

PlayMode::PlayMode(Client &client_) : client(client_) {
	//----- allocate OpenGL resources -----
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
			sizeof(Vertex), //stride
			(GLbyte *)0 + 0 //offset
		);
		glEnableVertexAttribArray(color_texture_program->Position_vec4);
		//[Note that it is okay to bind a vec3 input to a vec4 attribute -- the w component will be filled with 1.0 automatically]

		glVertexAttribPointer(
			color_texture_program->Color_vec4, //attribute
			4, //size
			GL_UNSIGNED_BYTE, //type
			GL_TRUE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 //offset
		);
		glEnableVertexAttribArray(color_texture_program->Color_vec4);

		glVertexAttribPointer(
			color_texture_program->TexCoord_vec2, //attribute
			2, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 + 4*1 //offset
		);
		glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);

		//done referring to vertex_buffer, so unbind it:
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//done setting up vertex array object, so unbind it:
		glBindVertexArray(0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}
	{ //solid white texture:
		//ask OpenGL to fill white_tex with the name of an unused texture object:
		glGenTextures(1, &white_tex);

		//bind that texture object as a GL_TEXTURE_2D-type texture:
		glBindTexture(GL_TEXTURE_2D, white_tex);

		//upload a 1x1 image of solid white to the texture:
		glm::uvec2 size = glm::uvec2(1,1);
		std::vector< glm::u8vec4 > data(size.x*size.y, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		//set filtering and wrapping parameters:
		//(it's a bit silly to mipmap a 1x1 texture, but I'm doing it because you may want to use this code to load different sizes of texture)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		//since texture uses a mipmap and we haven't uploaded one, instruct opengl to make one for us:
		glGenerateMipmap(GL_TEXTURE_2D);

		//Okay, texture uploaded, can unbind it:
		glBindTexture(GL_TEXTURE_2D, 0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}
	init_tiles();
}

PlayMode::~PlayMode() {
	//----- free OpenGL resources -----
	glDeleteBuffers(1, &vertex_buffer);
	vertex_buffer = 0;

	glDeleteVertexArrays(1, &vertex_buffer_for_color_texture_program);
	vertex_buffer_for_color_texture_program = 0;

	glDeleteTextures(1, &white_tex);
	white_tex = 0;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_KEYDOWN) {
		if (evt.key.repeat) {
			//ignore repeats
		}
		else if (evt.key.keysym.sym == SDLK_a) {
			if (dir != right) {
				dir = left;
				send_update = true;
			}
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_d) {
			if (dir != left) {
				dir = right;
				send_update = true;
			}
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_w) {
			if (dir != down) {
				dir = up;
				send_update = true;
			}
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_s) {
			if (dir != up) {
				dir = down;
				send_update = true;
			}
			return true;
		}
	}
	else if (evt.type == SDL_KEYUP) {
		// do nothing
	}

	return false;
}

void PlayMode::update(float elapsed) {
	if (GAME_OVER) {
		return;
	}

	//queue data for sending to server:
	//TODO: send something that makes sense for your game
	if (send_update) {
		//send a two-byte message of type 'b':
		client.connections.back().send('b');
		client.connections.back().send((uint8_t)dir);

		send_update = false;
	}

	//send/receive data:
	client.poll([this](Connection* c, Connection::Event event) {
		if (event == Connection::OnOpen) {
			//std::cout << "[" << c->socket << "] opened" << std::endl;
		}
		else if (event == Connection::OnClose) {
			//std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		}
		else {
			assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); //std::cout.flush();
			//expecting message(s): 'S' + 1-byte [number of players] + [number-of-player] chunks of player info:
			while (c->recv_buffer.size() >= 2) {
				//std::cout << "[" << c->socket << "] recv'd data of size " << c->recv_buffer.size() << ". Current buffer:\n" << hex_dump(c->recv_buffer);
				//std::cout.flush();
				char type = c->recv_buffer[0];
				//std::cout << "type=" << type << std::endl;
				if (type == 'a') {
					uint32_t num_players = uint8_t(c->recv_buffer[1]);
					//std::cout << "num_players=" << num_players << std::endl;
					if (c->recv_buffer.size() < 2 + num_players * 3) break; //if whole message isn't here, can't process
					//whole message *is* here, so set current server message:

					uint8_t byte_index = 2;
					for (uint32_t k = 0; k < num_players; k++) {
						uint8_t id = c->recv_buffer[byte_index++];
						uint8_t x = c->recv_buffer[byte_index++];
						uint8_t y = c->recv_buffer[byte_index++];
						glm::vec2 pos = glm::vec2(x, y);

						auto player = players.find(id);
						if (player == players.end()) create_player(id, pos);
						else update_player(&player->second, pos);
					}
					//and consume this part of the buffer:
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + byte_index);
				}
				else if (type == 'i') {
					// uint8_t local_id = c->recv_buffer[1];
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 2);
				}
				else {
					throw std::runtime_error("Server sent unknown message type '" + std::to_string(type) + "'");
				}
			}
		}
	}, 0.0);
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//vertices will be accumulated into this list and then uploaded+drawn at the end of this function:
	std::vector< Vertex > vertices;

	draw_tiles(vertices);
	draw_players(vertices);
	// draw the borders for debugging purposes
	// draw_borders(hex_to_color_vec(border_color), vertices);

	//compute area that should be visible:
	glm::vec2 scene_min = glm::vec2(-PADDING, -PADDING);
	glm::vec2 scene_max = glm::vec2(GRID_W+PADDING, GRID_H+PADDING);

	//compute window aspect ratio:
	float aspect = drawable_size.x / float(drawable_size.y);
	//we'll scale the x coordinate by 1.0 / aspect to make sure things stay square.

	//compute scale factor for court given that...
	float scale = std::min(
		(2.0f * aspect) / (scene_max.x - scene_min.x), //... x must fit in [-aspect,aspect] ...
		(2.0f) / (scene_max.y - scene_min.y) //... y must fit in [-1,1].
	);

	glm::vec2 center = 0.5f * (scene_max + scene_min);

	//build matrix that scales and translates appropriately:
	glm::mat4 court_to_clip = glm::mat4(
		glm::vec4(scale / aspect, 0.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, scale, 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(-center.x * (scale / aspect), -center.y * scale, 0.0f, 1.0f)
	);

	if (GAME_OVER) {
		//TODO :use DrawLines to overlay some text

	}
	//NOTE: glm matrices are specified in *Column-Major* order,
	// so each line above is specifying a *column* of the matrix(!)

	//---- actual drawing ----
	//clear the color buffer:
	glClearColor(hex_to_color_vec(bg_color).r / 255.0f, 
				hex_to_color_vec(bg_color).g / 255.0f, 
				hex_to_color_vec(bg_color).b / 255.0f, 
				hex_to_color_vec(bg_color).a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	//use alpha blending:
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//don't use the depth test:
	glDisable(GL_DEPTH_TEST);

	//upload vertices to vertex_buffer:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(), GL_STREAM_DRAW); //upload vertices array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color_texture_program as current program:
	glUseProgram(color_texture_program->program);

	//upload OBJECT_TO_CLIP to the proper uniform location:
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(court_to_clip));

	//use the mapping vertex_buffer_for_color_texture_program to fetch vertex data:
	glBindVertexArray(vertex_buffer_for_color_texture_program);

	//bind the solid white texture to location zero so things will be drawn just with their colors:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, white_tex);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));

	//unbind the solid white texture:
	glBindTexture(GL_TEXTURE_2D, 0);

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);	

	GL_ERRORS(); //PARANOIA: print errors just in case we did something wrong.

}

// ------------- helpers -----------------

glm::u8vec4 PlayMode::hex_to_color_vec(int color_hex) {
	return glm::u8vec4((color_hex >> 24) & 0xff, 
						(color_hex >> 16) & 0xff, 
						(color_hex >> 8) & 0xff, 
						(color_hex) & 0xff);
}

void PlayMode::init_tiles() {
	for (int col = 0; col < NUM_COLS; col++) {
		std::vector<Tile> tile_col;
		for (int row = 0; row < NUM_ROWS; row++) {
			tile_col.emplace_back(white_color, 0);
		}
		tiles.push_back(tile_col);
	}
}

void PlayMode::create_player(uint8_t id, glm::uvec2 pos) {
	Player new_player = { id, player_colors[id], pos };

	new_player.prev_pos[0] = pos;
	new_player.prev_pos[1] = pos;
	tiles[(uint8_t)pos.x][(uint8_t)pos.y].color = trail_colors[id];

	players.insert({ id, new_player });
}

void PlayMode::update_player(Player* p, glm::uvec2 pos) {
	uint8_t x = pos.x;
	uint8_t y = pos.y;
	uint8_t id = p->id;

	bool moving = p->pos != pos;
	// position changed, shift it into prev_pos
	if (moving) {
		p->prev_pos[1] = p->prev_pos[0];
		p->prev_pos[0] = p->pos;
	}

	// update player's position
	p->pos = pos;

	// update and trim player's trails
	for (int col = 0; col < NUM_COLS; col++) {
		for (int row = 0; row < NUM_ROWS; row++) {
			if (tiles[col][row].color == trail_colors[id]) {
				tiles[col][row].age++;
				if (tiles[col][row].age >= TRAIL_MAX_LEN)
					tiles[col][row].color = white_color;
			}
		}
	}

	// player enters their own territory
	if (tiles[x][y].color == player_colors[id]) {
		// update player's territory and clear player's trail
		for (int col = 0; col < NUM_COLS; col++) {
			for (int row = 0; row < NUM_ROWS; row++) {
				if (tiles[col][row].color == trail_colors[id]) {
					tiles[col][row].color = player_colors[id];
				}
			}
		}

		uint32_t territory_size = fill_interior(player_colors[id]);

		// check if player has won
		if (territory_size > WIN_THRESHOLD) {
			win_game(id, territory_size);
		}
	}
	// player is moving and hits their own trail
	else if (moving && tiles[x][y].color == trail_colors[id]) {
		// create allowed_tiles -> player's trail - previous tile
		std::vector< glm::uvec2 > allowed_tiles;
		for (int col = 0; col < NUM_COLS; col++) {
			for (int row = 0; row < NUM_ROWS; row++) {
				if (tiles[col][row].color == trail_colors[id]) {
					glm::uvec2 allowed_pos = glm::uvec2(col, row);
					// disconnect loop, so that the shortest path has to go the long way around the loop
					if (allowed_pos != p->prev_pos[0])
						allowed_tiles.emplace_back(col, row);
				}
			}
		}

		// find shortest path "around" the loop
		std::vector<glm::uvec2> path = shortest_path(p->prev_pos[1], pos, allowed_tiles);
		// reconnect loop
		path.push_back(p->prev_pos[0]);

		// clear player's trail
		allowed_tiles.push_back(p->prev_pos[0]); // re-add previous tile
		for (auto allowed_pos : allowed_tiles) {
			if (tiles[(uint8_t)allowed_pos.x][(uint8_t)allowed_pos.y].color == trail_colors[id])
				tiles[(uint8_t)allowed_pos.x][(uint8_t)allowed_pos.y].color = white_color;
		}

		// add loop to territory
		for (glm::uvec2 pos : path) {
			tiles[(uint8_t)pos.x][(uint8_t)pos.y].color = player_colors[id];
		}

		uint32_t territory_size = fill_interior(player_colors[id]);

		// check if player has won
		if (territory_size > WIN_THRESHOLD) {
			win_game(id, territory_size);
		}
	}
	// player hits other player's trail or territory
	else if (tiles[x][y].color != white_color) {
		// clear player's trail
		for (int col = 0; col < NUM_COLS; col++) {
			for (int row = 0; row < NUM_ROWS; row++) {
				if (tiles[col][row].color == trail_colors[id]) {
					tiles[col][row].color = white_color;
				}
			}
		}
	}
	else {
		// update player's trail
		tiles[x][y].color = trail_colors[id];
		tiles[x][y].age = 0;
	}
}

void PlayMode::draw_rectangle(glm::vec2 const &pos,
                        glm::vec2 const &size,
                        glm::u8vec4 const &color,
                        std::vector<Vertex> &vertices) {
    //draw rectangle as two CCW-oriented triangles:
    vertices.emplace_back(glm::vec3(pos.x, pos.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
    vertices.emplace_back(glm::vec3(pos.x+size.x, pos.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
    vertices.emplace_back(glm::vec3(pos.x+size.x, pos.y+size.y, 0.0f), color, glm::vec2(0.5f, 0.5f));

    vertices.emplace_back(glm::vec3(pos.x, pos.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
    vertices.emplace_back(glm::vec3(pos.x+size.x, pos.y+size.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
    vertices.emplace_back(glm::vec3(pos.x, pos.y+size.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
};

void PlayMode::draw_borders(glm::u8vec4 const &color,
							std::vector<Vertex> &vertices) {
	for (int i = 0; i < NUM_ROWS+1; i++) {
		draw_rectangle(glm::vec2(0.0f, i*TILE_SIZE-BORDER_SIZE/2.0f),
					glm::vec2(GRID_W, BORDER_SIZE),
					color, vertices);
	}
	for (int j = 0; j < NUM_COLS+1; j++) {
		draw_rectangle(glm::vec2(j*TILE_SIZE-BORDER_SIZE/2.0f, 0.0f),
					glm::vec2(BORDER_SIZE, GRID_H),
					color, vertices);
	}
}

void PlayMode::draw_tiles(std::vector<Vertex> &vertices) {
	for (int x = 0; x < NUM_COLS; x++) {
		for (int y = 0; y < NUM_ROWS; y++) {
			draw_rectangle(glm::vec2(x * TILE_SIZE, y * TILE_SIZE),
						   glm::vec2(TILE_SIZE, TILE_SIZE),
				           hex_to_color_vec(tiles[x][y].color), vertices);
		}
	}
}

void PlayMode::draw_players(std::vector<Vertex>& vertices) {
	for (auto& [id, player] : players) {
		(void) id; // appease compiler's unused variable warning
		// draw player
		draw_rectangle(glm::vec2(player.pos.x * TILE_SIZE, player.pos.y * TILE_SIZE),
			glm::vec2(TILE_SIZE, TILE_SIZE),
			hex_to_color_vec(player.color),
			vertices);
		draw_rectangle(glm::vec2(player.pos.x * TILE_SIZE + TILE_SIZE / 4, player.pos.y * TILE_SIZE + TILE_SIZE / 4),
			glm::vec2(TILE_SIZE / 2, TILE_SIZE / 2),
			hex_to_color_vec(white_color),
			vertices);
		#ifdef DEBUG_TRAIL
			draw_rectangle(glm::vec2(player.prev_pos[0].x * TILE_SIZE, player.prev_pos[0].y * TILE_SIZE),
				glm::vec2(TILE_SIZE, TILE_SIZE),
				hex_to_color_vec(0xff0000ff),
				vertices);
			draw_rectangle(glm::vec2(player.prev_pos[1].x * TILE_SIZE, player.prev_pos[1].y * TILE_SIZE),
				glm::vec2(TILE_SIZE, TILE_SIZE),
				hex_to_color_vec(0x00ff00ff),
				vertices);
		#endif
	}
}

void PlayMode::win_game(uint8_t id, uint32_t area) {
	GAME_OVER = true;
	winner_id = winner_id;
	winner_score = area;
	std::cout << "game over" << ' ' << (int)winner_id << ' ' << winner_score << '\n';
}

// shortest path using Dijkstra's algorithm (will throw error if no path exists)
std::vector< glm::uvec2 > PlayMode::shortest_path(glm::uvec2 const &start, glm::uvec2 const &end, std::vector< glm::uvec2 > const &allowed_tiles) {
	struct DijkstraPoint {
		uint32_t distance;
		glm::uvec2 pos;
		DijkstraPoint(unsigned int const &_distance, glm::uvec2 const &_pos): distance(_distance), pos(_pos) {}
	};

	struct CompareDijkstraPoint {
		bool operator()(DijkstraPoint const &p1, DijkstraPoint const &p2) {
			return p1.distance > p2.distance;
		}
	};

	std::priority_queue<DijkstraPoint, std::vector<DijkstraPoint>, CompareDijkstraPoint> pqueue;

	std::unordered_map<glm::uvec2, uint32_t> dist;
	std::unordered_map<glm::uvec2, glm::uvec2> prev;

	// populate priority queue
	dist[start] = 0;
	pqueue.push(DijkstraPoint(0, start));
	for (glm::uvec2 pos : allowed_tiles) {
		if (start == pos) {
			continue;
		}
		dist[pos] = (NUM_ROWS + NUM_COLS) * 100; // "infinity" (no distance will ever be this high)
		pqueue.push(DijkstraPoint(dist[pos], pos));
	}

	// neighbor helper function
	auto neighbors = [&](glm::uvec2 p) {
		std::vector<glm::uvec2> neighbors;
		if (p.x + 1 < NUM_COLS && 
			std::find(allowed_tiles.begin(), allowed_tiles.end(), glm::uvec2(p.x+1, p.y)) != allowed_tiles.end())
			neighbors.push_back(glm::uvec2(p.x + 1, p.y));
		if (p.x - 1 >= 0 && 
			std::find(allowed_tiles.begin(), allowed_tiles.end(), glm::uvec2(p.x-1, p.y)) != allowed_tiles.end())
			neighbors.push_back(glm::uvec2(p.x - 1, p.y));
		if (p.y + 1 < NUM_ROWS &&
			std::find(allowed_tiles.begin(), allowed_tiles.end(), glm::uvec2(p.x, p.y+1)) != allowed_tiles.end())
			neighbors.push_back(glm::uvec2(p.x, p.y + 1));
		if (p.y - 1 >= 0 && 
			std::find(allowed_tiles.begin(), allowed_tiles.end(), glm::uvec2(p.x, p.y-1)) != allowed_tiles.end())
			neighbors.push_back(glm::uvec2(p.x, p.y - 1));
		return neighbors;
	};

	// do dijkstra's algorithm
	while (!pqueue.empty()) {
		DijkstraPoint const p = pqueue.top();
		pqueue.pop();

		for (auto const &neighbor : neighbors(p.pos)) {
			uint32_t alt = dist.at(p.pos) + 1;
			if (alt < dist.at(neighbor)) {
				dist[neighbor] = alt;
				prev[neighbor] = p.pos;
				// std::cout << neighbor.x << ", " << neighbor.y << " -> " << p.pos.x << ", " << p.pos.y << std::endl;
				pqueue.push(DijkstraPoint(dist.at(neighbor), neighbor));
			}
		}
	}

	// std::cout << "start " << start.x << ", " << start.y << std::endl;
	// std::cout << "end " << end.x << ", " << end.y << std::endl;

	std::vector<glm::uvec2> shortest_path;
	glm::uvec2 pos = end;
	while (pos != start) {
		shortest_path.push_back(pos);
		// std::cout << pos.x << ", " << pos.y << std::endl;
		if (prev.find(pos) == prev.end()) {
			throw std::runtime_error("No path exists");
		}
		pos = prev.at(pos);
	}
	shortest_path.push_back(start);

	return shortest_path;
}

void PlayMode::floodfill(std::vector<std::vector<uint32_t>> &grid, uint32_t x, uint32_t y, uint32_t new_color, uint32_t old_color) {
	if (new_color == old_color) return;
	if (x < 0 || y < 0 || x >= grid.size() || y >= grid[0].size()) return;
	if (grid[x][y] == old_color) {
		grid[x][y] = new_color;

		floodfill(grid, x + 1, y, new_color, old_color);
		floodfill(grid, x - 1, y, new_color, old_color);
		floodfill(grid, x, y + 1, new_color, old_color);
		floodfill(grid, x, y - 1, new_color, old_color);
	}
};

// fills all regions enclosed by a player's color, returns new size of territory
uint32_t PlayMode::fill_interior(uint32_t color) {
	const uint32_t EMPTY = 0;
	const uint32_t BORDER = 1;
	const uint32_t FILL = 2;

	uint32_t territory_size = 0;

	// create grid, adding a 1 tile border on all sides
	std::vector<std::vector<uint32_t>> tiles_copy;
	for (int x = -1; x < NUM_COLS + 1; x++) {
		std::vector<uint32_t> tile_col;
		for (int y = -1; y < NUM_ROWS + 1; y++) {
			if (y == -1 || y == NUM_ROWS || x == -1 || x == NUM_COLS || tiles[x][y].color != color)
				tile_col.push_back(EMPTY);
			else {
				// tile is in bounds and is the fill player's color
				tile_col.push_back(BORDER);
				territory_size++;
			}
		}
		tiles_copy.push_back(tile_col);
	}

	// floodfill outer area, starting from border (top-left)
	floodfill(tiles_copy, 0, 0, FILL, EMPTY);

	// set all non-filled/interior tiles to color
	for (int x = 0; x < NUM_COLS + 2; x++) {
		for (int y = 0; y < NUM_ROWS + 2; y++) {
			if (tiles_copy[x][y] == EMPTY && x - 1 >= 0 && y - 1 >= 0 && x - 1 < NUM_COLS && y - 1 < NUM_ROWS) {
				tiles[x-1][y-1].color = color;
				territory_size++;
			}
		}
	}

	return territory_size;
}