#include "PlayMode.hpp"

#include "DrawBloom.hpp"
#include "DrawLines.hpp"
#include "DrawBackground.hpp"

#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"
#include "load_save_png.hpp"
#include "ColorTextureProgram.hpp"
#include "glm/ext.hpp"
#include "glm/gtx/string_cast.hpp"

#include "Sound.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>
#include <queue>

Load< Sound::Sample > background_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("background_track.opus"));
});

Load< Sound::Sample > walk_sample_0(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("walk_0.opus"));
});

Load< Sound::Sample > walk_sample_1(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("walk_1.opus"));
});

Load< Sound::Sample > connect_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("connect.opus"));
});

Load< Sound::Sample > success_sample(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("success.opus"));
});

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
	// { //solid white texture:
	// 	//ask OpenGL to fill white_tex with the name of an unused texture object:
	// 	glGenTextures(1, &white_tex);

	// 	//bind that texture object as a GL_TEXTURE_2D-type texture:
	// 	glBindTexture(GL_TEXTURE_2D, white_tex);

	// 	//upload a 1x1 image of solid white to the texture:
	// 	glm::uvec2 size = glm::uvec2(1,1);
	// 	std::vector< glm::u8vec4 > data(size.x*size.y, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
	// 	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

	// 	//set filtering and wrapping parameters:
	// 	//(it's a bit silly to mipmap a 1x1 texture, but I'm doing it because you may want to use this code to load different sizes of texture)
	// 	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	// 	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// 	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	// 	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	// 	//since texture uses a mipmap and we haven't uploaded one, instruct opengl to make one for us:
	// 	glGenerateMipmap(GL_TEXTURE_2D);

	// 	//Okay, texture uploaded, can unbind it:
	// 	glBindTexture(GL_TEXTURE_2D, 0);

	// 	GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	// }
	{ // load tileset texture
		std::vector< glm::u8vec4 > data;
		glm::uvec2 size(0, 0);
		load_png(data_path("sprite.png"), &size, &data, LowerLeftOrigin);
		sprite_sheet_size = size;
		std::cout << "sprite sheet size: " << glm::to_string(sprite_sheet_size) << std::endl;

		glGenTextures(1, &sprite_tex);

		glBindTexture(GL_TEXTURE_2D, sprite_tex);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glBindTexture(GL_TEXTURE_2D, 0);

		GL_ERRORS();
	}
	
	init_tiles();

	// start background music 
	Sound::loop(*background_sample, 0.1f, 0.0f);
}

PlayMode::~PlayMode() {
	//----- free OpenGL resources -----
	glDeleteBuffers(1, &vertex_buffer);
	vertex_buffer = 0;

	glDeleteVertexArrays(1, &vertex_buffer_for_color_texture_program);
	vertex_buffer_for_color_texture_program = 0;

	// glDeleteTextures(1, &white_tex);
	// white_tex = 0;
	glDeleteTextures(1, &sprite_tex);
	sprite_tex = 0;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	return false;
}

void PlayMode::update(float elapsed) {
	if (GAME_OVER) {
		return;
	}

	update_powerup(elapsed);

	//queue data for sending to server:
	const uint8_t *key = SDL_GetKeyboardState(NULL);
	Dir dir = none;
	if (key[SDL_SCANCODE_LEFT] || key[SDL_SCANCODE_A]) {
		if (players.at(local_id).powerup_type == speed) dir = ll;
		else dir = left;
	} else if (key[SDL_SCANCODE_RIGHT] || key[SDL_SCANCODE_D]) {
		if (players.at(local_id).powerup_type == speed) dir = rr;
		else dir = right;
	} else if (key[SDL_SCANCODE_UP] || key[SDL_SCANCODE_W]) {
		if (players.at(local_id).powerup_type == speed) dir = uu;
		else dir = up;
	} else if (key[SDL_SCANCODE_DOWN] || key[SDL_SCANCODE_S]) {
		if (players.at(local_id).powerup_type == speed) dir = dd;
		else dir = down;
	}

	//send a two-byte message of type 'b':
	client.connections.back().send('b');
	client.connections.back().send((uint8_t)dir);

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
					if (c->recv_buffer.size() < 2 + num_players * 4) break; //if whole message isn't here, can't process
					//whole message *is* here, so set current server message:

					uint8_t byte_index = 2;
					for (uint32_t k = 0; k < num_players; k++) {
						uint8_t id = c->recv_buffer[byte_index++];
						uint8_t dir = c->recv_buffer[byte_index++];
						uint8_t x = c->recv_buffer[byte_index++];
						uint8_t y = c->recv_buffer[byte_index++];
						glm::vec2 pos = glm::vec2(x, y);

						auto player = players.find(id);
						if (player == players.end()) {
							Sound::play(*connect_sample, 1.0f, 0.0f);
							create_player(id, (PlayMode::Dir)dir, pos);
						}
						else {
							Player* p = &player->second;
							// std::cout << p->pos.x << ' ' << p->pos.y << ' ' << pos.x << ' ' << pos.y << '\n';
							if (std::abs((int)p->pos.x - (int)pos.x) > 1 ||
								std::abs((int)p->pos.y - (int)pos.y) > 1) { // moved 2 tiles
								update_player(p, (PlayMode::Dir)dir, glm::vec2((p->pos.x + pos.x) / 2,
									                       (p->pos.y + pos.y) / 2), elapsed);
							}
							update_player(p, (PlayMode::Dir)dir, pos, elapsed);
						}
					}
					//and consume this part of the buffer:
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + byte_index);
				}
				else if (type == 'g') {
					if (c->recv_buffer.size() < 3) break; //if whole message isn't here, can't process

					horizontal_border = c->recv_buffer[1];
					vertical_border = c->recv_buffer[2];

					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 3);
				}
				else if (type == 'i') {
					if (c->recv_buffer.size() < 2) break; //if whole message isn't here, can't process
					local_id = c->recv_buffer[1];
					std::cout << "local_id: " + std::to_string(local_id) << std::endl;
					gameState = IN_GAME;
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 2);
				}
				else if (type == 's') {
					if (c->recv_buffer.size() < 2) break; //if whole message isn't here, can't process
					start_countdown = c->recv_buffer[1];
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 2);
				}
				else if (type == 'q') {
					if (c->recv_buffer.size() < 2) break; //if whole message isn't here, can't process
					lobby_size = c->recv_buffer[1];
					c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 2);
				}
				else {
					throw std::runtime_error("Server sent unknown message type '" + std::to_string(type) + "'");
				}
			}
		}
	}, 0.0);

	// update powerup
	if (powerup_cd > 0.0f) {
		powerup_cd -= elapsed;
	}
	else {
		new_powerup();
		powerup_cd = 10.0f;
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//vertices will be accumulated into this list and then uploaded+drawn at the end of this function:
	std::vector< Vertex > vertices;

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

	draw_tiles(vertices);
	draw_players(vertices);
	// draw the borders for debugging purposes
	// draw_borders(hex_to_color_vec(border_color), vertices);

	if (GAME_OVER) {
		std::string msg = "PLAYER " + std::to_string(winner_id) + " WON";
		draw_text(vertices, msg, glm::vec2(GRID_W/0.5f, GRID_H/0.5f), hex_to_color_vec(player_colors[winner_id]));
	} 
	size_t num_players = players.size();
	size_t i = 0;
	for (auto &[id, player] : players) {
		std::string msg = std::to_string((player.area * 100) / (NUM_ROWS * NUM_COLS));
		draw_text(vertices, msg, glm::vec2((i + 1) * NUM_COLS * TILE_SIZE / (num_players + 1), (NUM_ROWS - 2.0f) * TILE_SIZE), hex_to_color_vec(player_colors[id]));
		i++;
	}

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
	glBindTexture(GL_TEXTURE_2D, sprite_tex);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));

	//unbind the solid white texture:
	glBindTexture(GL_TEXTURE_2D, 0);

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);

	// draw the background
	// { 	
	// 	DrawBackground background(court_to_clip);
	// 	background.draw(
	// 		glm::vec2(GRID_W, GRID_H),
	// 		hex_to_color_vec(white_color));
		
	// }

	// draw the bloom light
	{ 	
		for (auto& [id, player] : players) {
			uint32_t trail_color = trail_colors[id];
			DrawBloom bloom(court_to_clip);
			bloom.draw(
				(glm::vec2(player.pos) + glm::vec2(0.5f, 0.5f))*TILE_SIZE,
				600.0f,
				hex_to_color_vec(trail_color & 0xffffff8f));
		}

		for (int x = 0; x < NUM_COLS; x++) {
			for (int y = 0; y < NUM_ROWS; y++) {
				if (tiles[x][y].powerup.type != no_powerup) {
					uint32_t powerup_color = powerup_colors.at(tiles[x][y].powerup.type);
					DrawBloom bloom(court_to_clip);
					bloom.draw(
						(glm::vec2(x, y) + glm::vec2(0.5f, 0.5f))*TILE_SIZE,
						100.0f,
						hex_to_color_vec(powerup_color & 0xffffff8f));
				}
			}
		}
	}


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
		std::vector<uint32_t> visual_board_col;
		for (int row = 0; row < NUM_ROWS; row++) {
			tile_col.emplace_back(base_color, 0);
			visual_board_col.push_back(base_color);
		}
		tiles.push_back(tile_col);
		visual_board.push_back(visual_board_col);
	}
}

void PlayMode::create_player(uint8_t id, Dir dir, glm::uvec2 pos) {
	Player new_player = { id, player_colors[id], pos };

	new_player.prev_pos[0] = pos;
	new_player.prev_pos[1] = pos;

	tiles[(uint8_t)pos.x][(uint8_t)pos.y].color = trail_colors[id];

	players.insert({ id, new_player });
}

void PlayMode::update_player(Player* p, Dir dir, glm::uvec2 pos, float elapsed) {
	uint8_t x = pos.x;
	uint8_t y = pos.y;
	uint8_t id = p->id;

	bool moving = p->pos != pos;
	// position changed, shift it into prev_pos
	if (moving) {
		p->prev_pos[1] = p->prev_pos[0];
		p->prev_pos[0] = p->pos;
		// update walk frame
		float next_frame = p->walk_frame + 2.0f * elapsed / 0.1f;
		while (next_frame > 2.0f) { next_frame -= 2.0f;}
		p->walk_frame = next_frame; 
	} else {
		p->walk_frame = 1.0f;
	}

	// update_sound(p, moving, elapsed);

	// update player's position
	p->pos = pos;
	if (dir != none) { p->dir = dir; }

	// update and trim player's trails
	for (int col = 0; col < NUM_COLS; col++) {
		for (int row = 0; row < NUM_ROWS; row++) {
			if (tiles[col][row].color == trail_colors[id]) {
				tiles[col][row].age++;
				if (p->powerup_type == trail) {
					if (tiles[col][row].age >= TRAIL_MAX_LEN + TRAIL_POWERUP_LEN)
						tiles[col][row].color = base_color;
				}
				else {
					if (tiles[col][row].age >= TRAIL_MAX_LEN)
						tiles[col][row].color = base_color;
				}
			}
		}
	}

	// player gets powerup
	if (tiles[x][y].powerup.type != no_powerup) {
		p->powerup_type = tiles[x][y].powerup.type;
		// std::cout << tiles[x][y].powerup;
		tiles[x][y].powerup.type = no_powerup;
		powerup_cd = 10.0f;
	}

	// player enters their own territory
	if (tiles[x][y].color == player_colors[id]) {
		// update player's territory and clear player's trail
		uint32_t trail_size = 0;
		for (int col = 0; col < NUM_COLS; col++) {
			for (int row = 0; row < NUM_ROWS; row++) {
				if (tiles[col][row].color == trail_colors[id]) {
					tiles[col][row].color = player_colors[id];
					trail_size++;
				}
			}
		}

		uint32_t territory_size = 0; uint32_t delta_size = 0;
		fill_interior(player_colors[id], delta_size, territory_size);
		if (moving && (delta_size + trail_size) > 0) { 
			Sound::play(*success_sample, (p->id == local_id) ? 0.3f : 0.0f, 0.0f); 
		}

		update_areas();

		// check if player has won
		if (territory_size > WIN_THRESHOLD) {
			win_game(id, territory_size);
		}
	}
	// player hits their own trail
	else if (tiles[x][y].color == trail_colors[id]) {
		if (moving) {
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
			assert(path.size() > 0 && "path around loop must exist"); // assert a path exists
			// reconnect loop
			path.push_back(p->prev_pos[0]);

			// clear player's trail
			allowed_tiles.push_back(p->prev_pos[0]); // re-add previous tile
			for (auto allowed_pos : allowed_tiles) {
				if (tiles[(uint8_t)allowed_pos.x][(uint8_t)allowed_pos.y].color == trail_colors[id])
					tiles[(uint8_t)allowed_pos.x][(uint8_t)allowed_pos.y].color = base_color;
			}

			// add loop to territory
			uint32_t trail_size = 0;
			for (glm::uvec2 pos : path) {
				tiles[(uint8_t)pos.x][(uint8_t)pos.y].color = player_colors[id];
				trail_size++;
			}

			uint32_t territory_size = 0; uint32_t delta_size = 0;
			fill_interior(player_colors[id], delta_size, territory_size);
			if (moving && (trail_size + delta_size) > 0) { 
				Sound::play(*success_sample, (p->id == local_id) ? 0.3f : 0.0f, 0.0f); 
			}

			update_areas();

			// check if player has won
			if (territory_size > WIN_THRESHOLD) {
				win_game(id, territory_size);
			}
		}
	}
	// player hits other player's trail or territory
	else if (tiles[x][y].color != base_color) {
		// hit other player's territory:
		if (std::find(player_colors.begin(), player_colors.end(), tiles[x][y].color) != player_colors.end()) {
			// clear own trail
			for (int col = 0; col < NUM_COLS; col++) {
				for (int row = 0; row < NUM_ROWS; row++) {
					if (tiles[col][row].color == trail_colors[id]) {
						tiles[col][row].color = base_color;
					}
				}
			}
		}
		// hit other player's trail
		else if (std::find(trail_colors.begin(), trail_colors.end(), tiles[x][y].color) != trail_colors.end()) {
			// clear other player's trail
			uint32_t other_player_trail_color = tiles[x][y].color;
			for (int col = 0; col < NUM_COLS; col++) {
				for (int row = 0; row < NUM_ROWS; row++) {
					if (tiles[col][row].color == other_player_trail_color) {
						tiles[col][row].color = base_color;
					}
				}
			}
			// overwrite with our trail
			tiles[x][y].color = trail_colors[id];
			tiles[x][y].age = 0;
		}
	}
	else {
		// update player's trail
		tiles[x][y].color = trail_colors[id];
		tiles[x][y].age = 0;
	}
}

// TODO(candy): update this function so that max walk_frame = 3.0f
// void PlayMode::update_sound(Player* p, bool moving, float elapsed) {
// 	if (!moving) {
// 		if (p->walk_sound != nullptr) { 
// 			p->walk_sound->stop(); }
// 		return;
// 	} 
// 	float next_frame = walk_frame + 2.0f * elapsed / 0.1f;
// 	while (next_frame > 2.0f) { next_frame -= 2.0f;}
// 	if ((int)next_frame == (int)walk_frame) { 
// 		walk_frame = next_frame; 
// 		return;
// 	}
// 	walk_frame = next_frame;
// 	float volume = 0.1f;
// 	if (p->id == local_id) { volume = 0.3f; }
// 	if ((int)walk_frame == 0) {p->walk_sound = Sound::play(*walk_sample_0, volume, 0.0f);}
// 	else if ((int)walk_frame == 1) {p->walk_sound = Sound::play(*walk_sample_1, volume, 0.0f);}
	
// }

void PlayMode::draw_rectangle(glm::vec2 const &pos,
                        glm::vec2 const &size,
                        glm::u8vec4 const &color,
                        std::vector<Vertex> &vertices) {
    //draw rectangle as two CCW-oriented triangles:
	vertices.emplace_back(glm::vec3(pos.x, pos.y, 0.0f), color, glm::vec2(1.0f-SPRITE_SIZE/sprite_sheet_size.x, 
																		1.0f-SPRITE_SIZE/sprite_sheet_size.y));
	vertices.emplace_back(glm::vec3(pos.x + size.x, pos.y, 0.0f), color, glm::vec2(1.0f-SPRITE_SIZE/sprite_sheet_size.x, 
																		1.0f));
	vertices.emplace_back(glm::vec3(pos.x + size.x, pos.y + size.y, 0.0f), color, glm::vec2(1.0f, 1.0f));

	vertices.emplace_back(glm::vec3(pos.x, pos.y, 0.0f), color, glm::vec2(1.0f-SPRITE_SIZE/sprite_sheet_size.x, 
																		1.0f-SPRITE_SIZE/sprite_sheet_size.y));
	vertices.emplace_back(glm::vec3(pos.x + size.x, pos.y + size.y, 0.0f), color, glm::vec2(1.0f, 1.0f));
	vertices.emplace_back(glm::vec3(pos.x, pos.y + size.y, 0.0f), color, glm::vec2(1.0f, 1.0f-SPRITE_SIZE/sprite_sheet_size.y));
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
	auto lerp_color = [](uint32_t col1, uint32_t col2, float amount) {
		uint8_t r1 = (col1 >> 24) & 0xff;
		uint8_t g1 = (col1 >> 16) & 0xff;
		uint8_t b1 = (col1 >> 8) & 0xff;
		uint8_t a1 = (col1) & 0xff;

		uint8_t r2 = (col2 >> 24) & 0xff;
		uint8_t g2 = (col2 >> 16) & 0xff;
		uint8_t b2 = (col2 >> 8) & 0xff;
		uint8_t a2 = (col2) & 0xff;

		uint8_t r3 = (uint8_t) ((float) r1 * (1.0f - amount) + (float) r2 * amount);
		uint8_t g3 = (uint8_t) ((float) g1 * (1.0f - amount) + (float) g2 * amount);
		uint8_t b3 = (uint8_t) ((float) b1 * (1.0f - amount) + (float) b2 * amount);
		uint8_t a3 = (uint8_t) ((float) a1 * (1.0f - amount) + (float) a2 * amount);

		return (uint32_t) (r3 << 24 | g3 << 16 | b3 << 8 | a3);
	};

	for (int x = 0; x < NUM_COLS; x++) {
		for (int y = 0; y < NUM_ROWS; y++) {
			std::vector<uint32_t>::const_iterator trail_it = std::find(trail_colors.begin(), trail_colors.end(), tiles[x][y].color);
			bool is_trail_color = (trail_it != trail_colors.end());

			if (tiles[x][y].color == base_color || is_trail_color) {
				visual_board[x][y] = tiles[x][y].color;
			}
			else {
				visual_board[x][y] = lerp_color(visual_board[x][y], tiles[x][y].color, 0.1f);
			}
			glm::u8vec4 color = hex_to_color_vec(visual_board[x][y]);

			if (is_trail_color) {
				// do not draw the first trail tile which overlaps with the player
				uint8_t player_id = trail_it - trail_colors.begin();
				Player player = players.at(player_id);
				if (glm::uvec2(x, y) == player.pos) {
					color = hex_to_color_vec(base_color);
				}
			} 
			draw_rectangle(glm::vec2(x * TILE_SIZE, y * TILE_SIZE),
					glm::vec2(TILE_SIZE, TILE_SIZE),
					color,
					vertices);
			
			// draw powerup
			if (tiles[x][y].powerup.type != no_powerup) {
				// color = hex_to_color_vec(black_color);
				draw_texture(vertices, glm::vec2(x * TILE_SIZE, y * TILE_SIZE), 
					glm::vec2(TILE_SIZE, TILE_SIZE),
					glm::vec2((int)tiles[x][y].powerup.frame, 
							tiles[x][y].powerup.type == speed ? 5.0f : 4.0f),
					glm::vec2(1.0f, 1.0f),
					glm::u8vec4(255, 255, 255, 255));
				// std::cout << x << y << '\n';
			} 
		}
	}

	// draw borders
	draw_rectangle(glm::vec2(0, 0), glm::vec2(NUM_COLS, vertical_border) * TILE_SIZE, glm::u8vec4(0, 0, 0, 255), vertices);
	draw_rectangle(glm::vec2(0, NUM_ROWS - vertical_border) * TILE_SIZE, glm::vec2(NUM_COLS, vertical_border) * TILE_SIZE, glm::u8vec4(0, 0, 0, 255), vertices);
	draw_rectangle(glm::vec2(0, 0), glm::vec2(horizontal_border, NUM_ROWS) * TILE_SIZE, glm::u8vec4(0, 0, 0, 255), vertices);
	draw_rectangle(glm::vec2(NUM_COLS - horizontal_border, 0) * TILE_SIZE, glm::vec2(horizontal_border, NUM_ROWS) * TILE_SIZE, glm::u8vec4(0, 0, 0, 255), vertices);
}

void PlayMode::draw_players(std::vector<Vertex>& vertices) {
	for (auto& [id, player] : players) {
		(void) id; // appease compiler's unused variable warning
		// draw player
		// std::cout << "id: " + std::to_string(player.id) << " dir: " + std::to_string(player.dir) << std::endl;
		glm::vec2 tex_pos;
		switch(player.dir) {
			case left:
			case ll:
				tex_pos.y = 2.0f;
				break;
			case right:
			case rr:
				tex_pos.y = 1.0f;
				break;
			case up:
			case uu:
				tex_pos.y = 0.0f;
				break;
			case down:
			case dd:
				tex_pos.y = 3.0f;
				break;
			case none: // initial state
				tex_pos.y = 3.0f;
				break;
		}
		tex_pos.x = player.id*3.0f + (int)player.walk_frame;

		draw_texture(vertices, glm::vec2(player.pos.x * TILE_SIZE, player.pos.y * TILE_SIZE), 
					glm::vec2(TILE_SIZE, TILE_SIZE),
					tex_pos,
					glm::vec2(1.0f, 1.0f),
					glm::u8vec4(255, 255, 255, 255));
		// draw_rectangle(glm::vec2(player.pos.x * TILE_SIZE, player.pos.y * TILE_SIZE),
		// 	glm::vec2(TILE_SIZE, TILE_SIZE),
		// 	hex_to_color_vec(player.color),
		// 	vertices);
		// draw_rectangle(glm::vec2(player.pos.x * TILE_SIZE + TILE_SIZE / 4, player.pos.y * TILE_SIZE + TILE_SIZE / 4),
		// 	glm::vec2(TILE_SIZE / 2, TILE_SIZE / 2),
		// 	hex_to_color_vec(white_color),
		// 	vertices);
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

void PlayMode::draw_texture(std::vector< Vertex >& vertices, glm::vec2 pos, glm::vec2 size, glm::vec2 tilepos, glm::vec2 tilesize, glm::u8vec4 color) {
	tilepos = glm::vec2(tilepos.x * SPRITE_SIZE / sprite_sheet_size.x, tilepos.y * SPRITE_SIZE / sprite_sheet_size.y);
	tilesize = glm::vec2(tilesize.x * SPRITE_SIZE / sprite_sheet_size.x, tilesize.y * SPRITE_SIZE / sprite_sheet_size.y);
	// std::cout << "tilepos: " + glm::to_string(tilepos) + ", tilesize: " + glm::to_string(tilesize) << std::endl;
	vertices.emplace_back(glm::vec3(pos.x, pos.y, 0.0f), color, glm::vec2(tilepos.x, tilepos.y));
	vertices.emplace_back(glm::vec3(pos.x + size.x, pos.y, 0.0f), color, glm::vec2(tilepos.x + tilesize.x, tilepos.y));
	vertices.emplace_back(glm::vec3(pos.x + size.x, pos.y + size.y, 0.0f), color, glm::vec2(tilepos.x + tilesize.x, tilepos.y + tilesize.y));

	vertices.emplace_back(glm::vec3(pos.x, pos.y, 0.0f), color, glm::vec2(tilepos.x, tilepos.y));
	vertices.emplace_back(glm::vec3(pos.x + size.x, pos.y + size.y, 0.0f), color, glm::vec2(tilepos.x + tilesize.x, tilepos.y + tilesize.y));
	vertices.emplace_back(glm::vec3(pos.x, pos.y + size.y, 0.0f), color, glm::vec2(tilepos.x, tilepos.y + tilesize.y));
}

void PlayMode::draw_text(std::vector< Vertex >& vertices, std::string msg, glm::vec2 anchor, glm::u8vec4 color) {
	auto draw_string = [&](std::string str, glm::vec2 at, glm::u8vec4 color) {
		std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.%/ ";

		for (size_t i = 0; i < str.size(); i++) {
			for (size_t j = 0; j < alphabet.size(); j++) {
				if (str[i] == alphabet[j]) {
					float s = 2.5f;
 					draw_texture(vertices,
						         at + (float)i * s * glm::vec2(12.0f, 0.0f),
						         s * glm::vec2(11.0f, 13.0f),
						         glm::vec2((float)j * 11.0f / SPRITE_SIZE, 15.0f),
						         glm::vec2(11.0f / SPRITE_SIZE, 13.0f / SPRITE_SIZE),
						         color);
				}
			}
		}
	};

	if (gameState == QUEUEING) {
		std::string msg = "QUEUEING... ";
		float width = msg.size() * 12.0f * 2.5f;
		draw_string(msg, glm::vec2(0.5f * NUM_COLS * TILE_SIZE - 0.5f * width, 0.5 * NUM_ROWS * TILE_SIZE + 0.5f * 12.0f * 2.5f + 1), glm::u8vec4(255, 255, 255, 255));

		msg = std::to_string(lobby_size) + "/2";
		width = msg.size() * 12.0f * 2.5f;
		draw_string(msg, glm::vec2(0.5f * NUM_COLS * TILE_SIZE - 0.5f * width, 0.5 * NUM_ROWS * TILE_SIZE - 0.5f * 12.0f * 2.5f), glm::u8vec4(255, 255, 255, 255));
	}
	else if (gameState == IN_GAME) {
		if (start_countdown > 0) {
			std::string msg = std::to_string(start_countdown / 10 + 1);
			float width = msg.size() * 12.0f * 2.5f;
			draw_string(msg, glm::vec2(0.5f * NUM_COLS * TILE_SIZE - 0.5f * width, 0.5 * NUM_ROWS * TILE_SIZE + 0.5f * 13.0f), glm::u8vec4(255, 255, 255, 255));
		}
		if (GAME_OVER) {
			std::string msg = "PLAYER " + std::to_string(winner_id) + " WON";
			float width = msg.size() * 12.0f * 2.5f;
			draw_string(msg, glm::vec2(0.5f * NUM_COLS * TILE_SIZE - 0.5f * width, 0.5 * NUM_ROWS * TILE_SIZE + 0.5f * 13.0f), hex_to_color_vec(player_colors[winner_id]));
		} else {
			size_t num_players = players.size();
			size_t i = 0;
			for (auto &[id, player] : players) {
				std::string msg = std::to_string((player.area * 100) / (NUM_ROWS * NUM_COLS)) + "%";
				float width = msg.size() * 12.0f * 2.5f;
				draw_string(msg, glm::vec2((i + 1) * NUM_COLS * TILE_SIZE / (num_players + 1) - 0.5f * width, (NUM_ROWS - 2.0f) * TILE_SIZE), hex_to_color_vec(player_colors[id]));
				i++;
			}
			
		}
	}
}

// clear powerup in tiles
// clear player's powerup
// create new random powerup in tiles
void PlayMode::new_powerup() {
	std::vector<glm::uvec2> empty_pos;
	for (int x = horizontal_border; x < NUM_COLS - 1 - horizontal_border; x++) {
		for (int y = vertical_border; y < NUM_ROWS - 1 - vertical_border; y++) {
			tiles[x][y].powerup.type = no_powerup;
			if (tiles[x][y].color == base_color) {
				empty_pos.push_back(glm::uvec2(x, y));
			}
		}
	}

	for (auto& [id, player] : players) {
		(void) id;
		player.powerup_type = no_powerup;
	}

	int rnd_idx = rand() % empty_pos.size();
	tiles[empty_pos[rnd_idx].x][empty_pos[rnd_idx].y].powerup.type = (PowerupType)(rand() % n_powerups);
	tiles[empty_pos[rnd_idx].x][empty_pos[rnd_idx].y].powerup.frame = 0.0f;
}

void PlayMode::update_powerup(float elapsed) {
	for (int x = horizontal_border; x < NUM_COLS - 1 - horizontal_border; x++) {
		for (int y = vertical_border; y < NUM_ROWS - 1 - vertical_border; y++) {
			if (tiles[x][y].powerup.type == no_powerup) { continue; }
			float num_frames = tiles[x][y].powerup.type == speed ? 4.0f : 5.0f; 
			float next_frame = tiles[x][y].powerup.frame + num_frames * elapsed / 0.5f;
			while (next_frame > num_frames) { next_frame -= num_frames;}
			tiles[x][y].powerup.frame = next_frame; 
		}
	}
}

void PlayMode::win_game(uint8_t id, uint32_t area) {
	GAME_OVER = true;
	winner_id = id;
	winner_score = area;
	std::cout << "game over" << ' ' << (int)winner_id << ' ' << winner_score << '\n';
}

// shortest path using Dijkstra's algorithm (returns empty vector if no path exists)
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
		if (prev.find(pos) == prev.end()) { // no path exists
			shortest_path.clear();
			return shortest_path;
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
void PlayMode::fill_interior(uint32_t color, uint32_t &delta_size, uint32_t &territory_size) {
	const uint32_t EMPTY = 0;
	const uint32_t BORDER = 1;
	const uint32_t FILL = 2;

	territory_size = 0;
	delta_size = 0;

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
				delta_size++;
				// std::cout << "(" << x << ", " << y << ")" << std::endl;
			}
		}
	}
	return;
}

void PlayMode::update_areas() {
	for (auto &[id, player] : players) {
		(void) id;
		player.area = 0;
	}
	for (int x = 0; x < NUM_COLS; x++) {
		for (int y = 0; y < NUM_ROWS; y++) {
			if (tiles[x][y].color != white_color) {
				for (uint32_t i = 0; i < player_colors.size(); i++) {
					if (tiles[x][y].color == player_colors[i]) {
						players.at(i).area++;
					}
				}
			}
		}
	}
}