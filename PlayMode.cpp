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
		} else if (evt.key.keysym.sym == SDLK_a) {
			left.downs += 1;
			left.pressed = true;
			send_update = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			send_update = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			send_update = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			send_update = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			send_update = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			send_update = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			send_update = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			send_update = true;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {
	auto player = players.find(local_id);
	if (player != players.end()) { // new player
		Player *local_player = &player->second;
		glm::uvec2 pos = local_player->pos;

		// check on 1 to avoid windows signed/unsigned mismatch error
		if (left.pressed && pos.x >= 1) { pos.x--; }
		else if (right.pressed && pos.x < NUM_COLS - 1) { pos.x++; }
		else if (up.pressed && pos.y < NUM_ROWS - 1) { pos.y++; }
		else if (down.pressed && pos.y >= 1) { pos.y--; }

		// std::cout << "new position: " + glm::to_string(pos) + ", prev position: " + 
		// 		glm::to_string(local_player->pos) << std::endl;

		//reset button press counters:
		left.downs = 0;
		right.downs = 0;
		up.downs = 0;
		down.downs = 0;

		// update player's position
		if (local_player->pos == pos) { 
			// std::cout << "we did not move, do nothing" << std::endl;
		} // we've hit a wall, do nothing
		else {
			if (tiles[pos.x][pos.y] == player_colors[local_id]) { // player enters their own territory
				// std::cout << "player enters their own territory" << std::endl;
				// update player's territory and clear player's trail
				for (auto t : local_player->trail) {
					if (std::find(local_player->territory.begin(), 
									local_player->territory.end(), t.first) 
									== local_player->territory.end()) {
						local_player->territory.emplace_back(t.first);
					}
				}
				local_player->trail.clear();

				fill_interior(local_player->territory);
				// // check if the trail and the territory can form an enclosed loop
				// if (local_player->trail.size() >= 1) {
				// 	std::vector<glm::uvec2> allowed_tiles = {pos};
				// 	for (auto t : local_player->territory) {
				// 		if (std::find(allowed_tiles.begin(), allowed_tiles.end(), t) == allowed_tiles.end()) {
				// 			allowed_tiles.emplace_back(t);
				// 		}
				// 	}
				// 	if (std::find(allowed_tiles.begin(), allowed_tiles.end(), local_player->trail.) == allowed_tiles.end()) {
				// 		allowed_tiles.emplace_back(t);
				// 	}

				// 	try {
				// 		shortest_path(pos, local_player->trail[0].first, allowed_tiles);
				// 		// path does exist
				// 		fill_interior(local_player->territory);
				// 		// update player's territory and clear player's trail
				// 		for (auto& trail : local_player->trail) {
				// 			glm::uvec2 trail_pos = trail.first;
				// 			local_player->territory.emplace_back(trail_pos);
				// 		}
				// 		local_player->trail.clear();
				// 	} catch (std::exception& e) {
				// 		std::cout << e.what() << std::endl;
				// 		local_player->trail.clear();
				// 	}	
				// }
			} else if (tiles[pos.x][pos.y] == trail_colors[local_id]) { // player hits their own trail
				// std::cout << "player hits their own trail" << std::endl;
				std::vector<glm::uvec2> loop; // find the shortest loop that we have formed
				assert(local_player->trail.size() >= 2);
				std::vector< glm::uvec2 > allowed_tiles;
				for (auto t : local_player->trail) {
					if (t.first == local_player->trail.back().first) {
						continue;
					}
					if (std::find(allowed_tiles.begin(), allowed_tiles.end(), t.first) == allowed_tiles.end()) {
						allowed_tiles.emplace_back(t.first);
					}
				}

				std::vector<glm::uvec2> path = shortest_path(local_player->trail[local_player->trail.size() - 2].first, 
															pos, 
															allowed_tiles);
				loop.insert(loop.end(), path.begin(), path.end());
				// reconnect loop
				loop.push_back(local_player->trail.back().first);
				// clear player's trail
				local_player->trail.clear();

				// add loop to territory
				for (glm::uvec2 p : loop) {
					if (std::find(local_player->territory.begin(), local_player->territory.end(), p) 
						== local_player->territory.end()) {
						local_player->territory.emplace_back(p);
					}
				}
				
				fill_interior(local_player->territory);
			} else if (tiles[pos.x][pos.y] != white_color) { // player hits other player's trail or territory
				// std::cout << "player hits other player's trail or territory" << std::endl;
				// clear player's trail
				local_player->trail.clear();
			} else {
				// std::cout << "update player's trail: " + glm::to_string(pos) << std::endl;
				// update player's trail
				local_player->trail.emplace_back(std::make_pair(pos, 0.0f));
			}
			local_player->pos = pos;
		}

		//age up all locations in the trail:
		for (auto& trail : local_player->trail) {
			trail.second += elapsed;
		}

		//trim any too-old locations from back of trail:
		while (local_player->trail.size() >= 1 && local_player->trail.at(0).second > TRAIL_MAX_AGE) {
			local_player->trail.pop_front();
			send_update = true;
		}

		update_tiles();

		if (send_update) {
			//queue data for sending to server:
			client.connections.back().send('b');
			size_t packet_size = get_packet_size(&client.connections.back(), *local_player);
			// std::cout << "client sending a packet of size: " + std::to_string(packet_size) << std::endl;
			send_uint32(&client.connections.back(), packet_size);
			send_uint32(&client.connections.back(), local_player->pos.x);
			send_uint32(&client.connections.back(), local_player->pos.y);
			// std::cout << "territory size = " + std::to_string(local_player->territory.size()) << std::endl;
			send_vector(&client.connections.back(), local_player->territory);
			// std::cout << "trail size = " + std::to_string(local_player->trail.size()) << std::endl;
			send_uint32(&client.connections.back(), local_player->trail.size());
			for (auto trail : local_player->trail) {
				send_uint32(&client.connections.back(), trail.first.x);
				send_uint32(&client.connections.back(), trail.first.y);
			}
			send_update = false;
		}
	}

	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			//std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			//std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); //std::cout.flush();
			//expecting message(s): 'u' + 4-byte [packet_size]
			while (c->recv_buffer.size() >= 5) {
				char type = c->recv_buffer[0];
				if (type == 'u') {
					size_t byte_index = 1;
					size_t packet_size;
					recv_uint32(c->recv_buffer, byte_index, packet_size);
					// std::cout << "Type=" << type << ", packet size=" + std::to_string(packet_size) << std::endl;
					// std::cout << "[" << c->socket << "] recv'd data of size " << c->recv_buffer.size() << ". Current buffer:\n" << hex_dump(c->recv_buffer);
					// std::cout.flush();

					if (c->recv_buffer.size() < 5+packet_size) break; //if whole message isn't here, can't process
					//whole message *is* here, so set current server message:

					while(byte_index < packet_size) {
						uint8_t id = c->recv_buffer[byte_index++];
						size_t x; size_t y;
						recv_uint32(c->recv_buffer,byte_index, x);
						recv_uint32(c->recv_buffer,byte_index, y);
						
						auto player = players.find(id);
						if (player == players.end()) { // new player
							Player new_player = {id, player_colors[id], glm::uvec2(x, y)};
							recv_territory(c->recv_buffer, byte_index, (&new_player)->territory);
							recv_trail(c->recv_buffer, byte_index, (&new_player)->trail);
							players.insert({id,  new_player});
						} else {
							Player *p = &player->second;
							p->territory.clear();
							p->trail.clear();
							p->pos = glm::uvec2(x,y);
							recv_territory(c->recv_buffer, byte_index, p->territory);
							recv_trail(c->recv_buffer, byte_index, p->trail);
						}
					}
					//and consume this part of the buffer:
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + byte_index);
				} else if (type == 'i') {
					std::cout.flush();

					local_id = c->recv_buffer[1];
					size_t start = 2; size_t x; size_t y;
					recv_uint32(c->recv_buffer, start, x);
					recv_uint32(c->recv_buffer, start, y);

					Player local_player = {local_id, player_colors[local_id], glm::uvec2(x, y)};

					players.insert({local_id, local_player});
					std::cout << "local_id=" + std::to_string(local_id) + 
								", pos=" + glm::to_string(glm::uvec2(x, y)) << std::endl;
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
void PlayMode::recv_uint32(std::vector< char > buffer, size_t &start, size_t &result) {
	result = uint32_t(((buffer[start] & 0xff) << 24) | 
						((buffer[start+1] & 0xff) << 16) | 
						((buffer[start+2] & 0xff) << 8) | 
						(buffer[start+3] & 0xff));
	start += 4;
}

void PlayMode::recv_territory(std::vector< char > buffer, size_t &start, std::vector< glm::uvec2 > &territory) {
	size_t territory_size;
	recv_uint32(buffer, start, territory_size);
	for (int i = 0; i < territory_size; i++) {
		size_t x;
		recv_uint32(buffer, start, x);
		size_t y;
		recv_uint32(buffer, start, y);
		territory.emplace_back(glm::uvec2(x, y));
	}
}

void PlayMode::recv_trail(std::vector< char > buffer, size_t &start, std::deque< std::pair<glm::uvec2, float> > &trail) {
	size_t trail_size;
	recv_uint32(buffer, start, trail_size);
	for (int i = 0; i < trail_size; i++) {
		size_t x;
		recv_uint32(buffer, start, x);
		size_t y;
		recv_uint32(buffer, start, y);
		trail.emplace_back(std::make_pair(glm::uvec2(x, y), 0.0f)); // we do not care about the trail age of other players
	}
}

size_t PlayMode::get_packet_size(Connection *c, Player local_player) {
	size_t total_size = 0;
	total_size += 8; //pos
	total_size += 4;
	total_size += 8*(local_player.territory.size());
	total_size += 4;
	total_size += 8*(local_player.trail.size());
	return total_size;
}

uint8_t PlayMode::get_nth_byte(uint8_t n, size_t num) {
	return uint8_t((num >> (8*n)) & 0xff);
}

void PlayMode::send_uint32(Connection *c, size_t num) {
	c->send(get_nth_byte(3, num));
	c->send(get_nth_byte(2, num));
	c->send(get_nth_byte(1, num));
	c->send(get_nth_byte(0, num));
}

void PlayMode::send_vector(Connection *c, std::vector< glm::uvec2 > data) {
	send_uint32(c, data.size());
	for (auto &vec : data) {
		send_uint32(c, vec.x);
		send_uint32(c, vec.y);
	}
}

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

void PlayMode::update_tiles() {
	for (int col = 0; col < NUM_COLS; col++) {
		for (int row = 0; row < NUM_ROWS; row++) {
			tiles[col][row] = white_color;
		}
	}
	for (auto p : players) {
		Player player = p.second;
		tiles[player.pos.x][player.pos.y] = player_colors[player.id];
		update_trails(player.trail, trail_colors[player.id]);
		update_territory(player.territory, player_colors[player.id]);
	}
}

void PlayMode::update_trails(std::deque< std::pair<glm::uvec2, float> > trail, uint32_t color) {
	for (auto t : trail) {
		glm::uvec2 trail_pos = t.first;
		tiles[trail_pos.x][trail_pos.y] = color;
	}
}

void PlayMode::update_territory(std::vector< glm::uvec2 > territory, uint32_t color) {
	for (glm::uvec2 v : territory) {
		tiles[v.x][v.y] = color;
	}
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

// fills all regions enclosed by a territory
void PlayMode::fill_interior(std::vector<glm::uvec2> &territory) {
	if (territory.size() == 0) return;
	else {
		const uint32_t EMPTY = 0;
		const uint32_t BORDER = 1;
		const uint32_t FILL = 2;

		// uint32_t territory_color = tiles[(uint32_t) territory[0].x][(uint32_t) territory[0].y];

		// create grid, adding a 1 tile border on all sides
		std::vector<std::vector<uint32_t>> tiles_copy;
		for (int x = -1; x < NUM_COLS + 1; x++) {
			std::vector<uint32_t> tile_col;
			for (int y = -1; y < NUM_ROWS + 1; y++) {
				tile_col.push_back(EMPTY);
			}
			tiles_copy.push_back(tile_col);
		}
		// add territory
		for (const glm::uvec2 &position: territory) {
			tiles_copy[(uint32_t) position.x + 1][(uint32_t) position.y + 1] = BORDER;
		}

		// floodfill outer area, starting from border (top-left)
		floodfill(tiles_copy, 0, 0, FILL, EMPTY);

		// set all non-filled/interior tiles to territory_color
		for (int x = 0; x < NUM_COLS + 2; x++) {
			for (int y = 0; y < NUM_ROWS + 2; y++) {
				if (tiles_copy[x][y] == EMPTY && x - 1 >= 0 && y - 1 >= 0 && x - 1 < NUM_COLS && y - 1 < NUM_ROWS) {
					glm::uvec2 pos = glm::uvec2(x-1, y-1);
					if (std::find(territory.begin(), territory.end(), pos) == territory.end()) {
						territory.emplace_back(pos);
					}
					// tiles[x-1][y-1] = territory_color;
				}
			}
		}
	}
}

// // rebuild each player's territory vector based on the tile grid
// void PlayMode::recalculate_territory() {
// 	for (auto &[id, player] : players) {
// 		(void) id; // avoid unused variable
// 		player.territory.clear();
// 	}

// 	for (int x = 0; x < NUM_COLS; x++) {
// 		for (int y = 0; y < NUM_ROWS; y++) {
// 			for (int i = 0; i < player_colors.size(); i++) {
// 				if (tiles[x][y] == player_colors[i]) {
// 					players.at(i).territory.push_back(glm::vec2(x, y));
// 				}
// 			}
// 		}
// 	}
// }

// // rebuild each player's trail vector based on the tile grid
// void PlayMode::recalculate_trails() {
// 	for (auto &[id, player] : players) {
// 		(void) id; // avoid unused variable
// 		player.trail.clear();
// 	}

// 	for (int x = 0; x < NUM_COLS; x++) {
// 		for (int y = 0; y < NUM_ROWS; y++) {
// 			for (int i = 0; i < trail_colors.size(); i++) {
// 				if (tiles[x][y] == trail_colors[i]) {
// 					players.at(i).trail.push_back(glm::vec2(x, y));
// 				}
// 			}
// 		}
// 	}
// }