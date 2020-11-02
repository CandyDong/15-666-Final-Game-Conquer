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
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.downs += 1;
			right.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.downs += 1;
			up.pressed = true;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.downs += 1;
			down.pressed = true;
			return true;
		}
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			left.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_d) {
			right.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_w) {
			up.pressed = false;
			return true;
		} else if (evt.key.keysym.sym == SDLK_s) {
			down.pressed = false;
			return true;
		}
	}

	return false;
}

void PlayMode::update(float elapsed) {

	//queue data for sending to server:
	//TODO: send something that makes sense for your game
	if (left.downs || right.downs || down.downs || up.downs) {
		//send a five-byte message of type 'b':
		client.connections.back().send('b');
		client.connections.back().send(left.downs);
		client.connections.back().send(right.downs);
		client.connections.back().send(down.downs);
		client.connections.back().send(up.downs);
	}

	//reset button press counters:
	left.downs = 0;
	right.downs = 0;
	up.downs = 0;
	down.downs = 0;

	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush();
			//expecting message(s): 'S' + 1-byte [number of players] + [number-of-player] chunks of player info:
			while (c->recv_buffer.size() >= 2) {
				std::cout << "[" << c->socket << "] recv'd data of size " 
						<< c->recv_buffer.size() << ". Current buffer:\n" 
						<< hex_dump(c->recv_buffer); std::cout.flush();
				char type = c->recv_buffer[0];
				std::cout << "type=" << type << std::endl;
				if (type != 'a') {
					throw std::runtime_error("Server sent unknown message type '" + std::to_string(type) + "'");
				}
				uint32_t num_players = uint8_t(c->recv_buffer[1]);
				std::cout << "num_players=" << num_players << std::endl;
				if (c->recv_buffer.size() < 2 + num_players * 4) break; //if whole message isn't here, can't process
				//whole message *is* here, so set current server message:

				uint8_t byte_index = 2;
				for (uint32_t k = 0; k < num_players; k++) {
					uint8_t id = c->recv_buffer[byte_index++];
					uint8_t name_len = c->recv_buffer[byte_index++];
					std::string name = std::string(c->recv_buffer.begin() + byte_index,
						c->recv_buffer.begin() + byte_index + name_len);
					byte_index += name_len;
					uint8_t row = c->recv_buffer[byte_index++];
					uint8_t col = c->recv_buffer[byte_index++];
					glm::vec2 pos = glm::vec2(row, col);

					auto player = players.find(id);
					if (player == players.end()) { // new player
						Player new_player = { name, hex_to_color_vec(player_colors[id]), pos };

						// the player's starting position is their initial territory
						new_player.territory.push_back(pos);
						tiles[row][col] = player_colors[id];

						// add new player to the players map
						players.insert({ id, new_player });
					}
					else {
						// update player's position
						player->second.pos = pos;

						if (tiles[row][col] == player_colors[id]) { // player enters their own territory
							// update player's territory and clear player's trail
							player->second.territory.insert(player->second.territory.end(), player->second.trail.begin(), player->second.trail.end());
							for (auto& trail_pos : player->second.trail) {
								tiles[(uint8_t)trail_pos.x][(uint8_t)trail_pos.y] = player_colors[id];
							}
							player->second.trail.clear();
						}
						else if (tiles[row][col] == trail_colors[id]) { // player hits their own trail
							// nothing happens
						}
						else if (tiles[row][col] != white_color) { // player hits other player's trail or territory
							// clear player's trail
							for (auto& trail_pos : player->second.trail) {
								tiles[(uint8_t)trail_pos.x][(uint8_t)trail_pos.y] = white_color;
							}
							player->second.trail.clear();
						}
						else {
							// update player's trail
							player->second.trail.push_back(pos);
							tiles[row][col] = trail_colors[id];
						}

					}
					std::cout << name << " : (" + std::to_string(row) + ", " + std::to_string(col) + ");" << std::endl;
				}
				//and consume this part of the buffer:
				c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + byte_index);
			}
		}
	}, 0.0);
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	//vertices will be accumulated into this list and then uploaded+drawn at the end of this function:
	std::vector< Vertex > vertices;

	// draw the tilemap
	draw_tiles(vertices);
	// draw the players
	draw_players(vertices);

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
glm::u8vec4 PlayMode::hex_to_color_vec(int color_hex) {
	return glm::u8vec4((color_hex >> 24) & 0xff, 
						(color_hex >> 16) & 0xff, 
						(color_hex >> 8) & 0xff, 
						(color_hex) & 0xff);
}

void PlayMode::init_tiles() {
	for (int row = 0; row < NUM_ROWS; row++) {
		std::vector<uint32_t> tile_row;
		for (int col = 0; col < NUM_COLS; col++) {
			tile_row.push_back(white_color);
		}
		tiles.push_back(tile_row);
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
	for (int row = 0; row < NUM_ROWS; row++) {
		for (int col = 0; col < NUM_COLS; col++) {
			draw_rectangle(glm::vec2(col * TILE_SIZE, row * TILE_SIZE),
						   glm::vec2(TILE_SIZE, TILE_SIZE),
				           hex_to_color_vec(tiles[row][col]), vertices);
		}
	}
	// draw the borders for debugging purposes
	// draw_borders(hex_to_color_vec(border_color), vertices);
}

void PlayMode::draw_players(std::vector<Vertex> &vertices) {
	for (auto& [id, player] : players) {
		// draw player
		draw_rectangle(glm::vec2(player.pos.y* TILE_SIZE, player.pos.x* TILE_SIZE),
			glm::vec2(TILE_SIZE, TILE_SIZE),
			player.color,
			vertices);
		draw_rectangle(glm::vec2(player.pos.y * TILE_SIZE + TILE_SIZE / 4, player.pos.x * TILE_SIZE + TILE_SIZE / 4),
			glm::vec2(TILE_SIZE/2, TILE_SIZE/2),
			hex_to_color_vec(white_color),
			vertices);
	}
}
