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
				send_update = DIR;
			}
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_d) {
			if (dir != left) {
				dir = right;
				send_update = DIR;
			}
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_w) {
			if (dir != down) {
				dir = up;
				send_update = DIR;
			}
			return true;
		}
		else if (evt.key.keysym.sym == SDLK_s) {
			if (dir != up) {
				dir = down;
				send_update = DIR;
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
	if (send_update == DIR) {
		//send a two-byte message of type 'b':
		client.connections.back().send('b');
		client.connections.back().send((uint8_t)dir);
		send_update = NONE;
	} else if (send_update == TERRITORY) {
		// inform another player to remove the territory
		client.connections.back().send('t');
		uint32_t map_size = 0;
		assert(!territories_to_update.empty());
		for (auto p : territories_to_update) {
			map_size += 1;
			map_size += 4*(p.second.size());
		}
		send_uint32(&(client.connections.back()), map_size);
		send_map(&(client.connections.back()), territories_to_update);
		territories_to_update.clear();
		send_update = NONE;
	}

	//send/receive data:
	client.poll([this, elapsed](Connection* c, Connection::Event event) {
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
						else update_player(&player->second, pos, elapsed);
					}
					//and consume this part of the buffer:
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + byte_index);
				}
				else if (type == 'i') {
					local_id = c->recv_buffer[1];
					std::cout << "Player " << std::to_string(local_id) << " connected!!" << std::endl;
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 2);
				}
				else if (type == 't') {
					size_t start = 1;
					uint32_t map_size; recv_uint32(c->recv_buffer, start, map_size);
					if (c->recv_buffer.size() < map_size) break; //if whole message isn't here, can't process
					std::unordered_map<uint8_t, std::vector<glm::uvec2>> territories_to_remove;
					recv_map(c->recv_buffer, start, territories_to_remove);
					for (auto p : territories_to_remove) {
						uint8_t player_id = p.first;
						RemoveTerritories(&(players.find(player_id)->second), p.second);
					}
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + start);
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
		std::vector<uint32_t> tile_col;
		for (int row = 0; row < NUM_ROWS; row++) {
			tile_col.push_back(white_color);
		}
		tiles.push_back(tile_col);
	}
}

void PlayMode::create_player(uint8_t id, glm::uvec2 pos) {
	Player new_player = { id, player_colors[id], pos };

	new_player.trail.emplace_back(std::make_pair(pos, 0.0f));
	tiles[(uint8_t)pos.x][(uint8_t)pos.y] = trail_colors[id];

	players.insert({ id, new_player });
}

// ===============================================================================

// =========================== game state update utils start ============================
/**
 * update player state (trail, territory, board...)
 * to keep everthing consistent, update data structures 
 * and redraw the board based on the data structures at the end of this function
 * by calling update_board()
 **/
void PlayMode::update_player(Player* p, glm::uvec2 pos, float elapsed) {
	// CASE 1: if player's pos is not changed, do nothing 
	if (p->pos == pos) { 
		update_trail(p, elapsed);
		update_board(p);
		return; 
	}

	uint8_t x = pos.x;
	uint8_t y = pos.y;
	uint8_t id = p->id;

	// update player's position
	p->pos = pos;
	// CASE 2: player enters their own territory
	if (tiles[x][y] == player_colors[id]) { 
		// checks whether a new territory can be formed
		uint32_t index; // index of the connected territory in the territory list
		bool formed = FormsNewTerritory(p->territories, pos, p->trail.front().first, index);
		if (formed && !p->trail.empty()) {
			std::unordered_set<glm::uvec2> old_t = p->territories[index];
			std::unordered_set<glm::uvec2> new_t;
			for (auto trail : p->trail) { new_t.insert(trail.first); } // insert the trail
			
			find_interior(p->territories, new_t); // populate new_t with the interior formed by the enclosed loop we found
			InsertTerritory(p->territories, new_t); // insert new_t into player's territory list
		}
		p->trail.clear();
	}
	// CASE 3: player hits their own trail
	else if (tiles[x][y] == trail_colors[id]) {
		assert(p->trail.size() >= 2);
		std::unordered_set<glm::uvec2> new_t; 
		// construct the vector containing tiles that will be used to search for the shortest loop
		// do not include the previous position (trail.back()) 
		// because it connects with another tile in the trail, which prevents us from finding the shortest path
		std::vector< glm::uvec2 > allowed_tiles;
		for (auto t : p->trail) {
			if (t.first == p->trail.back().first) { continue; }
			allowed_tiles.emplace_back(t.first);
		}
		std::vector<glm::uvec2> path = shortest_path(p->trail[p->trail.size() - 2].first, pos, allowed_tiles);
		assert(!path.empty());
		new_t.insert(path.begin(), path.end()); // insert the shortest loop that we have formed
		new_t.insert(p->trail.back().first); // reconnect loop
		p->trail.clear(); // clear player's trail

		find_interior(p->territories, new_t); // populate new_t with the interior formed by the enclosed loop we found
		InsertTerritory(p->territories, new_t); // insert new_t into player's territory list
	}
	// CASE 4: player hits other player's trail or territory
	else if (tiles[x][y] != white_color) { p->trail.clear(); }
	// CASE 5: player is on an empty tile
	else { p->trail.emplace_back(std::make_pair(pos, 0.0f)); } // update player's trail 

	update_trail(p, elapsed);
	update_board(p);

	// game over
	if (p->territories.size() > NUM_COLS * NUM_ROWS / 2) {
		GAME_OVER = true;
		winner_id = id;
		// TODO: this should be changed to the total area of p's territory
		winner_score = p->territories.size(); 
		std::cout << "game over" << ' ' << (int)winner_id << ' ' << winner_score << '\n';
	}
}

/**
 * trim any too-old locations from back of trail
 **/
void PlayMode::update_trail(Player *p, float elapsed) {
	//age up all locations in the trail:
	for (auto &pair : p->trail) { pair.second += elapsed; }
	while (p->trail.size() >= 1 && p->trail[0].second > TRAIL_MAX_AGE) {
		p->trail.pop_front();
	}
}

/**
 * fill the board with correct colors based on player data
 **/ 
void PlayMode::update_board(Player *p) {
	// start with a fresh state: remove all old tiles of the player (trail, territory)
	for (int x = 0; x < NUM_COLS; x++) {
		for (int y = 0; y < NUM_ROWS; y++) {
			if (tiles[x][y] == p->color || tiles[x][y] == trail_colors[p->id]) { tiles[x][y] = white_color; }
		}
	}
	// update tiles in the trail with trail color
	for (std::pair<glm::uvec2, float> t : p->trail) { tiles[t.first.x][t.first.y] = trail_colors[p->id]; }
	// update tiles in the territory with territory color
	for (auto territory : p->territories) {
		for (glm::uvec2 p_t : territory) { tiles[p_t.x][p_t.y] = player_colors[p->id]; }
	}
}
// =========================== game state update utils end =================================

// =========================== draw utils start ============================================
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
				           hex_to_color_vec(tiles[x][y]), vertices);
		}
	}
}

void PlayMode::draw_players(std::vector<Vertex>& vertices) {
	for (auto& [id, player] : players) {
		// draw player
		draw_rectangle(glm::vec2(player.pos.x * TILE_SIZE, player.pos.y * TILE_SIZE),
			glm::vec2(TILE_SIZE, TILE_SIZE),
			hex_to_color_vec(player.color),
			vertices);
		draw_rectangle(glm::vec2(player.pos.x * TILE_SIZE + TILE_SIZE / 4, player.pos.y * TILE_SIZE + TILE_SIZE / 4),
			glm::vec2(TILE_SIZE / 2, TILE_SIZE / 2),
			hex_to_color_vec(white_color),
			vertices);
	}
}
// =========================== draw utils end ============================================

// =========================== territory utils start =============================
/** 
 * shortest path using Dijkstra's algorithm (will throw error if no path exists)
 **/
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

/**
 * 
 * find all tiles enclosed by the trail / existing territory
 * and save them in new_t
 **/
void PlayMode::find_interior(std::vector<std::unordered_set<glm::uvec2>> &territories, 
							std::unordered_set<glm::uvec2> &new_t) {
	const uint32_t EMPTY = 0; const uint32_t BORDER = 1; const uint32_t FILL = 2;

	// create a copy of the grid with all tiles marked EMPTY, 
	// adding a 1 tile border on all sides
	std::vector<std::vector<uint32_t>> tiles_copy;
	for (int x = -1; x < NUM_COLS + 1; x++) {
		std::vector<uint32_t> tile_col;
		for (int y = -1; y < NUM_ROWS + 1; y++) {
			tile_col.push_back(EMPTY);
		}
		tiles_copy.push_back(tile_col);
	}

	// get the territory vector
	std::vector<glm::uvec2> entire_territory = GetEntireTerritory(territories); 

	// add the trail stored in new_t to the territory
	entire_territory.insert(entire_territory.end(), new_t.begin(), new_t.end());

	// add the current territories to the grid copy as BORDER
	for (const glm::uvec2 &position: entire_territory) {
		tiles_copy[(uint32_t) position.x + 1][(uint32_t) position.y + 1] = BORDER;
	}

	// floodfill outer area, starting from border (top-left)
	// which makes all tiles except the territories to be set as FILL
	floodfill(tiles_copy, 0, 0, FILL, EMPTY);

	// set all non-filled/interior tiles to territory_color
	// now all the tiles marked EMPTY should be the new territory to add
	for (int x = 0; x < NUM_COLS + 2; x++) {
		for (int y = 0; y < NUM_ROWS + 2; y++) {
			if (tiles_copy[x][y] == EMPTY && x - 1 >= 0 && y - 1 >= 0 && x - 1 < NUM_COLS && y - 1 < NUM_ROWS) {
				glm::uvec2 pos = glm::uvec2(x-1, y-1);
				new_t.insert(pos);

				// if the tile is included in another player's territory,
				// it should be removed from that player's territory
				std::vector<uint32_t>::const_iterator it = std::find(player_colors.begin(), player_colors.end(), tiles[x-1][y-1]);
				if ((it != player_colors.end()) && ((it-player_colors.begin()) != local_id)) {
					uint8_t old_player_id = (uint8_t)(it - player_colors.begin());
					Player *old_player = &(players.at(old_player_id));
					// remove pos from old player's territory and send updates to the player
					RemoveFromTerritory(old_player->territories, pos, old_player_id);
				}
			}
		}
	}
}

/**
 * check if the point is inside the territories
 * if so remove the point from territory
 * if the territory is empty, remove it from the list
 * player_id is the id of the player whose territory should be removed from the baord
 **/
void PlayMode::RemoveFromTerritory(std::vector<std::unordered_set<glm::uvec2>> &territories,
									glm::uvec2 pos,
									uint8_t player_id) {
	uint32_t index;
	bool check = InsideTerritory(territories, pos, index);
	if (!check) { return; }

	std::unordered_set<glm::uvec2> *t = &(territories[index]);
	t->erase(pos); // erase by value
	// erase empty territory from the list
	territories.erase(std::remove_if(
				territories.begin(),
				territories.end(),
				[](std::unordered_set<glm::uvec2> s) { return s.empty(); }),
    			territories.end());

	// send update to the player whose territory is removed
	send_update = TERRITORY;
	if (territories_to_update.find(player_id) == territories_to_update.end()) {
		territories_to_update.insert({player_id, std::vector<glm::uvec2>{}});
	} 
	territories_to_update[player_id].emplace_back(pos);
}

/** 
 * remove coords from player p's territory
 **/
void PlayMode::RemoveTerritories(Player *p, std::vector<glm::uvec2> coords) {
	for (glm::uvec2 coord : coords) {
		uint32_t index;
		bool check = InsideTerritory(p->territories, coord, index);
		if (!check) { return; }

		std::unordered_set<glm::uvec2> *t = &(p->territories[index]);
		t->erase(coord); // erase by value
		p->territories.erase(std::remove_if(
				p->territories.begin(),
				p->territories.end(),
				[](std::unordered_set<glm::uvec2> s) { return s.empty(); }),
    			p->territories.end());
	}
}

/**
 * returns true if the pos is inside one of the territory in the territory list
 * saves the index of the territory in the territories list
 * false if the pos is not inside any of the territory in the territory list
 **/
bool PlayMode::InsideTerritory(const std::vector<std::unordered_set<glm::uvec2>> &territories, 
								glm::uvec2 pos,
								uint32_t &index) {
	std::vector<std::unordered_set<glm::uvec2>>::const_iterator it;
	for (it = territories.begin(); it != territories.end(); it++) {
		std::unordered_set<glm::uvec2> territory = *it;
		if (territory.find(pos) != territory.end()) { 
			index = it - territories.begin();
			return true; 
		}
	}
	return false;
}

/**
 * helper function for the case where player enters its territory
 * returns true if the trail forms a closed loop with an existing territory
 * which must be the same territory which [pos] is in
 **/ 
bool PlayMode::FormsNewTerritory(const std::vector<std::unordered_set<glm::uvec2>> &territories, 
								glm::uvec2 pos,
								glm::uvec2 r_pos,
								uint32_t &index) {
	// find the territory pos is in
	uint32_t p_index;
	bool result = InsideTerritory(territories, pos, p_index);
	assert(result);

	for (glm::ivec2 dir : DIRECTIONS) {
		glm::ivec2 neighbor = (glm::ivec2)r_pos + dir;
		if (neighbor.x < 0 || neighbor.x >= NUM_COLS) { continue; }
		if (neighbor.y < 0 || neighbor.y >= NUM_ROWS) { continue; }

		uint32_t t_index; // saves the index of the territory if the point is contained
		if (InsideTerritory(territories, neighbor, t_index)) {
			if (t_index == p_index) { 
				index = p_index;
				return true; 
			}
		}
	}
	return false;
}

/**
 * inserts the new territory into the territory list
 * the set new_t must only contain tiles that are already connected!!!
 * merge territories if the newly added territory connects with the existing territory
 **/
void PlayMode::InsertTerritory(std::vector<std::unordered_set<glm::uvec2>> &territories, 
								std::unordered_set<glm::uvec2> &new_t) {
	assert(IsIsland(new_t));

	auto IsConnected = [=, &new_t](std::unordered_set<glm::uvec2> old_t) {
		std::unordered_set<glm::uvec2> seen;
		std::deque<glm::uvec2> queue;
		queue.emplace_back(*new_t.begin());

		while (!queue.empty()) {
			glm::ivec2 cur_coord = (glm::ivec2)queue.front();
			queue.pop_front();
			seen.insert(cur_coord);

			for(glm::ivec2 dir : DIRECTIONS) {
				glm::ivec2 nex_coord = cur_coord + dir;

				// keep this sanity check in case we change the DIRECTIONS vector in the future
				if (cur_coord == nex_coord) { continue; } 

				if (nex_coord.x < 0 || nex_coord.x >= NUM_COLS) { continue; }
				if (nex_coord.y < 0 || nex_coord.y >= NUM_ROWS) { continue; }
				if (seen.find((glm::uvec2)nex_coord) != seen.end()) { continue; }

				if (old_t.find((glm::uvec2)nex_coord) != old_t.end()) {
					// neighbor is in the old territory, which means the two territories are connected
					return true;
				}
				if (new_t.find((glm::uvec2)nex_coord) != new_t.end()) {
					// neighbor is a point in the new territory, add to queue
					queue.emplace_back(nex_coord);
				}
			}
		}
		return false;
	};

	// find all existing territories that connects with the new territory
	std::vector<uint32_t> indices; assert(indices.empty());
	std::vector<std::unordered_set<glm::uvec2>>::const_iterator it;
	for (it = territories.begin(); it != territories.end(); it++) {
		std::unordered_set<glm::uvec2> old_t = *it;
		if (IsConnected(old_t)) {
			indices.emplace_back(it-territories.begin());
		}
	}
	
	// if no old_t is connected with the new_t, append new_t to the territories list
	if (indices.empty()) {
		territories.emplace_back(new_t);
		return;
	}

	// merge the connected territories
	MergeTerritories(territories, indices, new_t);
}

/**
 * merge connected territories and update the territories list
 * the indices vector contains the indices of the territory that is connected with 
 * the new territory
 **/ 
void PlayMode::MergeTerritories(std::vector<std::unordered_set<glm::uvec2>> &territories, 
								std::vector<uint32_t> &indices,
								std::unordered_set<glm::uvec2> &new_t) {
	if (indices.empty()) { return; }

	std::unordered_set<glm::uvec2> merged_t;

	// erase items backwards so as no to invalidate the indices
	auto reverse_sort = [](int32_t i, int32_t j) { return (i>j); };
	std::sort(indices.begin(), indices.end(), reverse_sort);

	// insert and remove the connected territories at the same time
	for (uint32_t ind : indices) {
		std::unordered_set<glm::uvec2> old_t = territories[ind];
		merged_t.insert(old_t.begin(), old_t.end());
		territories.erase(territories.begin()+ind);
	}

	merged_t.insert(new_t.begin(), new_t.end());

	// insert the merged territory to the territory list
	territories.emplace_back(merged_t);
}

/**
 * returns all territories in a vector
 **/
std::vector<glm::uvec2> PlayMode::GetEntireTerritory(const std::vector<std::unordered_set<glm::uvec2>> &territories) {
	std::vector<glm::uvec2> result;
	for (auto territory : territories) {
		for (glm::uvec2 p : territory) {
			result.emplace_back(p);
		}
	}
	return result;
}

/**
 * check if the tiles in the set are already connected to each other
 * (forms an island)
 **/
bool PlayMode::IsIsland(const std::unordered_set<glm::uvec2> &coords) {
	std::unordered_set<glm::uvec2> seen;

	std::deque<glm::uvec2> queue;
	queue.emplace_back(*coords.begin());

	while (!queue.empty()) {
		glm::ivec2 cur_coord = (glm::ivec2)queue.front();
		queue.pop_front();
		seen.insert(cur_coord);

		// iterate through the neighbors, which should all be 
		// contained in the coords set, or the set contains
		// disconnected coords (does not form an island)
		for (glm::ivec2 dir : DIRECTIONS) {
			glm::ivec2 nex_coord = cur_coord + dir;

			// keep this sanity check in case we change the DIRECTIONS vector in the future
			if (cur_coord == nex_coord) { continue; } 
			
			if (nex_coord.x < 0 || nex_coord.x >= NUM_COLS) { continue; }
			if (nex_coord.y < 0 || nex_coord.y >= NUM_ROWS) { continue; }
			if (seen.find(nex_coord) != seen.end()) { continue; }

			// add valid nex_coord to the queue if it's in coords
			if (coords.find(nex_coord) != coords.end()) {
				queue.emplace_back(nex_coord);
			}
		}
	}

	return seen.size() == coords.size();
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
// =========================== territory utils end ===============================

// =========================== networking utils start ============================
uint8_t PlayMode::get_nth_byte(uint8_t n, uint32_t num) {
	return uint8_t((num >> (8*n)) & 0xff);
}
void PlayMode::send_uint32(Connection *c, uint32_t num) {
	c->send(get_nth_byte(3, num));
	c->send(get_nth_byte(2, num));
	c->send(get_nth_byte(1, num));
	c->send(get_nth_byte(0, num));
}
void PlayMode::recv_uint32(std::vector< char > buffer, size_t &start, uint32_t &result) {
	result = uint32_t(((buffer[start] & 0xff) << 24) | 
						((buffer[start+1] & 0xff) << 16) | 
						((buffer[start+2] & 0xff) << 8) | 
						(buffer[start+3] & 0xff));
	start += 4;
}
void PlayMode::send_vector(Connection *c, std::vector< glm::uvec2 > data) {
	send_uint32(c, data.size());
	for (auto &vec : data) {
		send_uint32(c, vec.x);
		send_uint32(c, vec.y);
	}
}
void PlayMode::recv_vector(std::vector<char> buffer, size_t &start, std::vector< glm::uvec2 > &data) {
	uint32_t data_size;
	recv_uint32(buffer, start, data_size);
	for (uint32_t i = 0; i < data_size; i++) {
		uint32_t x; recv_uint32(buffer, start, x);
		uint32_t y; recv_uint32(buffer, start, y);
		data.emplace_back(glm::uvec2(x, y));
	}
}
void PlayMode::send_map(Connection *c, std::unordered_map<uint8_t, std::vector<glm::uvec2>> map) {
	send_uint32(c, map.size());
	for (std::pair<uint8_t, std::vector<glm::uvec2>> p : map) {
		c->send(p.first); 
		send_vector(c, p.second);
	}
}
void PlayMode::recv_map(std::vector<char> buffer, size_t &start, std::unordered_map<uint8_t, std::vector<glm::uvec2>> &map) {
	uint32_t map_size;
	recv_uint32(buffer, start, map_size);
	for (uint32_t i = 0; i < map_size; i++) {
		uint8_t id = buffer[start++];
		std::vector<glm::uvec2> data; recv_vector(buffer, start, data);
		map.insert({id, data});
	}
}
// =========================== networking utils end ==============================