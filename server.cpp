
#include "Connection.hpp"

#include "hex_dump.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <unordered_map>
#include <deque>
#include <glm/glm.hpp>

const uint8_t NUM_ROWS = 50;
const uint8_t NUM_COLS = 80;
const uint8_t MAX_NUM_PLAYERS = 4;
const size_t TOTAL_AREA = NUM_ROWS*NUM_COLS;
const size_t WIN_THRESHOLD = size_t(0.5f*TOTAL_AREA);

//server state:
static std::deque<uint32_t> unused_player_ids;
static std::vector<glm::uvec2> init_positions;

static bool state_updated = false;

//per-client state:
struct PlayerInfo {
	PlayerInfo() { 
		id = unused_player_ids.front();
		unused_player_ids.pop_front();
		name = "Player " + std::to_string(id);
		uint8_t x, y;
		do {
			x = rand() % NUM_COLS;
			y = rand() % NUM_ROWS;
		} while (std::find(init_positions.begin(), 
							init_positions.end(), 
							glm::uvec2(x, y)) 
				!= init_positions.end());
		init_positions.emplace_back(glm::uvec2(x, y));
		pos = glm::uvec2(x, y);
		trail.emplace_back(glm::uvec2(x, y));
		
		std::cout << name << " connected: (" + std::to_string(x) + ", " + std::to_string(y) + ");" << std::endl;
	}
	std::string name;
	uint8_t id;
	uint8_t dir;
	glm::uvec2 pos;

	std::vector< glm::uvec2 > territory;
	std::vector< glm::uvec2 > trail; //stores (x,y), oldest elements first
};

static std::unordered_map< Connection *, PlayerInfo > players;
static uint8_t winner_id;
static size_t winner_score = 0;
static bool GAME_OVER = false;

void send_vector(Connection *c, std::vector< glm::uvec2 > data);
void send_uint32(Connection *c, size_t num);
uint8_t get_nth_byte(uint8_t n, size_t num);
size_t get_packet_size(Connection *c, std::unordered_map< Connection *, PlayerInfo > players);
void send_on_update(std::unordered_map< Connection *, PlayerInfo > players);
void send_on_new_connection(Connection *c, std::unordered_map< Connection *, PlayerInfo > players);
void send_on_game_over(std::unordered_map< Connection *, PlayerInfo > players);
void recv_uint32(std::vector< char > buffer, size_t &start, size_t &result);
void recv_vector(std::vector< char > buffer, size_t &start, std::vector< glm::uvec2 > &data);

void calculate_scores(std::unordered_map< Connection *, PlayerInfo > players);


int main(int argc, char **argv) {
#ifdef _WIN32
	//when compiled on windows, unhandled exceptions don't have their message printed, which can make debugging simple issues difficult.
	try {
#endif

	//------------ argument parsing ------------

	if (argc != 2) {
		std::cerr << "Usage:\n\t./server <port>" << std::endl;
		return 1;
	}

	//------------ initialization ------------

	Server server(argv[1]);
	srand ((uint32_t)time(NULL)); // initialize random seed

	//------------ main loop ------------
	constexpr float ServerTick = 1.0f / 10.0f; //TODO: set a server tick that makes sense for your game

	for (int i = 0; i < MAX_NUM_PLAYERS; i++) {
		unused_player_ids.emplace_back(i);
	};

	while (true) {
		static auto next_tick = std::chrono::steady_clock::now() + std::chrono::duration< double >(ServerTick);
		//process incoming data from clients until a tick has elapsed:
		while (true) {
			auto now = std::chrono::steady_clock::now();
			double remain = std::chrono::duration< double >(next_tick - now).count();
			if (remain < 0.0) {
				next_tick += std::chrono::duration< double >(ServerTick);
				break;
			}
			server.poll([&](Connection *c, Connection::Event evt){
				if (evt == Connection::OnOpen) {
					std::cout << "connected" << '\n';
					//client connected:
					if (GAME_OVER) {
						c->close();
					}
					if (unused_player_ids.empty()) { // server is full
						c->close();
					} else {
						// create some player info for them:
						players.emplace(c, PlayerInfo());
						send_on_new_connection(c, players);
						state_updated = true;
					}
				} else if (evt == Connection::OnClose) {
					//client disconnected:
					//remove them from the players list:
					auto f = players.find(c);
					assert(f != players.end());
					unused_player_ids.emplace_back(f->second.id);
					players.erase(f);
					state_updated = true;

				} else { assert(evt == Connection::OnRecv);
					//got data from client:
					// std::cout << "[" << c->socket << "] recv'd data of size " << c->recv_buffer.size() << ". Current buffer:\n" << hex_dump(c->recv_buffer);
					// std::cout.flush();
					//look up in players list:
					auto f = players.find(c);
					assert(f != players.end());
					PlayerInfo &player = f->second;
					player.territory.clear();
					player.trail.clear();

					//handle messages from client:
					while (c->recv_buffer.size() >= 5) {
						char type = c->recv_buffer[0];
						if (type != 'b') {
							std::cout << " message of non-'b' type received from client!" << std::endl;
							//shut down client connection:
							c->close();
							return;
						}
						size_t byte_index = 1;
						size_t packet_size;
						recv_uint32(c->recv_buffer, byte_index, packet_size);
						// std::cout << "packet size=" + std::to_string(packet_size) << std::endl;
						if (c->recv_buffer.size() < 5+packet_size) break; //if whole message isn't here, can't process
						//whole message *is* here, so set current server message:
						size_t x; size_t y;
						recv_uint32(c->recv_buffer, byte_index, x);
						recv_uint32(c->recv_buffer, byte_index, y);
						player.pos = glm::uvec2(x, y);
						recv_vector(c->recv_buffer, byte_index, player.territory);
						recv_vector(c->recv_buffer, byte_index, player.trail);
						assert(byte_index == packet_size+5);
						//and consume this part of the buffer:
						c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + byte_index);
					}
					state_updated = true;
					calculate_scores(players);
				}
			}, remain);
		}

		//send updated game state to all clients
		if (GAME_OVER) {
			send_on_game_over(players);
			state_updated = false;
			GAME_OVER = false;
		}

		if (state_updated) {
			send_on_update(players);
			state_updated = false;
		}
	}
	return 0;

#ifdef _WIN32
	} catch (std::exception const &e) {
		std::cerr << "Unhandled exception:\n" << e.what() << std::endl;
		return 1;
	} catch (...) {
		std::cerr << "Unhandled exception (unknown type)." << std::endl;
		throw;
	}
#endif
}

void send_on_new_connection(Connection *c, std::unordered_map< Connection *, PlayerInfo > players) {
	// send over the corresponding player id
	c->send('i');
	c->send(players.at(c).id);
	send_uint32(c, players.at(c).pos.x);
	send_uint32(c, players.at(c).pos.y);
}

void send_on_game_over(std::unordered_map< Connection *, PlayerInfo > players) {
	std::cout << "GAME OVER" << std::endl;
	// send over the corresponding player id
	for (auto &[c, player] : players) {
		c->send('g');
		c->send(winner_id);
		send_uint32(c, winner_score);
	}
}

void send_on_update(std::unordered_map< Connection *, PlayerInfo > players) {
	if (players.size() <= 1) {
		return;
	}

	for (auto &[c, player] : players) {
		(void)player; //work around "unused variable" warning on whatever g++ github actions uses
		c->send('u');
		size_t packet_size = get_packet_size(c, players);
		send_uint32(c, packet_size);// total packet size
		// std::cout << "[" + std::to_string(player.id) + 
		// 			"] sending a total packet of size " + std::to_string(packet_size) << std::endl;
		// send along all player info
		for (auto &[c_prime, player_prime] : players) {
			if (c_prime == c) {
				// std::cout << "do not send duplicate info since client knows it's own state" << std::endl;
				continue;
			}
			(void)c_prime;
			c->send(uint8_t(player_prime.id)); // 1 byte
			send_uint32(c, player_prime.pos.x);
			send_uint32(c, player_prime.pos.y);
			// send territory
			send_vector(c, player_prime.territory); // 4 bytes for size + (size*8)-byte of data
			// send trail (the last element in the trail is the player's current location)
			send_vector(c, player_prime.trail); // 4 bytes for size + (size*8)-byte of data
		}
	}
}

size_t get_packet_size(Connection *c, std::unordered_map< Connection *, PlayerInfo > players) {
	size_t total_size = 0;
	for (auto &[c_prime, player_prime] : players) {
		if (c_prime == c) {
			continue;
		}
		(void)c_prime;
		total_size += 1; // id
		total_size += 8; // pos
		total_size += 4;
		total_size += 8*(player_prime.territory.size());
		total_size += 4;
		total_size += 8*(player_prime.trail.size());
	}
	return total_size;
}

uint8_t get_nth_byte(uint8_t n, size_t num) {
	return uint8_t((num >> (8*n)) & 0xff);
}

void send_uint32(Connection *c, size_t num) {
	c->send(get_nth_byte(3, num));
	c->send(get_nth_byte(2, num));
	c->send(get_nth_byte(1, num));
	c->send(get_nth_byte(0, num));
}

void send_vector(Connection *c, std::vector< glm::uvec2 > data) {
	send_uint32(c, data.size());
	for (auto &vec : data) {
		send_uint32(c, vec.x);
		send_uint32(c, vec.y);
	}
}

void recv_uint32(std::vector< char > buffer, size_t &start, size_t &result) {
	result = uint32_t(((buffer[start] & 0xff) << 24) | 
						((buffer[start+1] & 0xff) << 16) | 
						((buffer[start+2] & 0xff) << 8) | 
						(buffer[start+3] & 0xff));
	start += 4;
}

void recv_vector(std::vector< char > buffer, size_t &start, std::vector< glm::uvec2 > &data) {
	size_t data_size;
	recv_uint32(buffer, start, data_size);
	for (int i = 0; i < data_size; i++) {
		size_t x;
		recv_uint32(buffer, start, x);
		size_t y;
		recv_uint32(buffer, start, y);
		data.emplace_back(glm::uvec2(x, y));
	}
}

void calculate_scores(std::unordered_map< Connection *, PlayerInfo > players) {
	for (auto &[c, player] : players) {
		(void)c;
		if (player.territory.size() > winner_score) {
			winner_score = player.territory.size();
			winner_id = player.id;
		}
	}
	if (winner_score > WIN_THRESHOLD) {
		GAME_OVER = true;
	}
}
