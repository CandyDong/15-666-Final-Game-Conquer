
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
// const size_t TOTAL_AREA = NUM_ROWS*NUM_COLS;
// const size_t WIN_THRESHOLD = size_t(0.5f*TOTAL_AREA);

//server state:
static std::deque<uint32_t> unused_player_ids;
static std::vector<glm::uvec2> init_positions;

//territory updates
static bool TERRITORY_UPDATE = false;
static std::unordered_map<uint8_t, std::vector<glm::uvec2>> territories_to_update;
static uint32_t territories_update_size;

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
		
		std::cout << name << " connected: (" + std::to_string(x) + ", " + std::to_string(y) + ");" << std::endl;
	}
	std::string name;
	uint8_t id;
	uint8_t dir;
	glm::uvec2 pos;
};

static std::unordered_map< Connection *, PlayerInfo > players;
//static uint8_t winner_id;
//static size_t winner_score = 0;
//static bool GAME_OVER = false;

uint8_t get_nth_byte(uint8_t n, uint32_t num);
void send_uint32(Connection *c, uint32_t num);
void recv_uint32(std::vector< char > buffer, size_t &start, uint32_t &result);
void send_vector(Connection *c, std::vector< glm::uvec2 > data);
void recv_vector(std::vector<char> buffer, size_t &start, std::vector< glm::uvec2 > &data);
void send_map(Connection *c, std::unordered_map<uint8_t, std::vector<glm::uvec2>> map);
void recv_map(std::vector<char> buffer, size_t &start, std::unordered_map<uint8_t, std::vector<glm::uvec2>> &map);

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
			server.poll([&](Connection* c, Connection::Event evt) {
				if (evt == Connection::OnOpen) {
					std::cout << "connected" << '\n';
					//client connected:
					if (unused_player_ids.empty()) { // server is full
						c->close();
					}
					else {
						// create some player info for them:
						players.emplace(c, PlayerInfo());
						// send player id
						c->send('i');
						c->send(players.at(c).id);
					}
				}
				else if (evt == Connection::OnClose) {
					//client disconnected:

					//remove them from the players list:
					auto f = players.find(c);
					assert(f != players.end());
					unused_player_ids.emplace_back(f->second.id);
					players.erase(f);

				}
				else {
					assert(evt == Connection::OnRecv);
					std::cout << "receiving" << '\n';
					//got data from client:
					std::cout << "got bytes:\n" << hex_dump(c->recv_buffer); std::cout.flush();

					//look up in players list:
					auto f = players.find(c);
					assert(f != players.end());
					PlayerInfo& player = f->second;

					//handle messages from client:
					//TODO: update for the sorts of messages your clients send
					while (c->recv_buffer.size() >= 2) {
						//expecting two-byte messages 'b' (dir)
						char type = c->recv_buffer[0];
						if (type == 'b') {
							player.dir = c->recv_buffer[1];
							c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 2);
						} else if (type == 't') {
							TERRITORY_UPDATE = true;
							size_t start = 1;
							recv_uint32(c->recv_buffer, start, territories_update_size);
							if (c->recv_buffer.size() < territories_update_size) break; //if whole message isn't here, can't process
							recv_map(c->recv_buffer, start, territories_to_update);
							c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + start);
						} else {
							std::cout << " message of non-'b' type received from client!" << std::endl;
							//shut down client connection:
							c->close();
							return;
						}
					}
				}
				}, remain);
		}

		//update current game state
		//TODO: replace with *your* game state update
		// update player position
		for (auto& [c, player] : players) {
			(void)c;
			if (player.dir == 0 && player.pos.x > 0) {
				player.pos.x--;
			}
			else if (player.dir == 1 && player.pos.x < NUM_COLS - 1) {
				player.pos.x++;
			}
			else if (player.dir == 2 && player.pos.y < NUM_ROWS - 1) {
				player.pos.y++;
			}
			else if (player.dir == 3 && player.pos.y > 0) {
				player.pos.y--;
			}
		}

		//send updated game state to all clients
		//TODO: update for your game state
		for (auto& [c, player] : players) {
			// (void)player;
			c->send('a');
			c->send(uint8_t(players.size()));
			// send along all player info
			for (auto& [c_prime, player_prime] : players) {
				(void)c_prime;
				c->send(uint8_t(player_prime.id));
				c->send(uint8_t(player_prime.pos.x));
				c->send(uint8_t(player_prime.pos.y));
			}
			if (TERRITORY_UPDATE) {
				c->send('t');
				send_uint32(c, territories_update_size);
				send_map(c, territories_to_update);
			}
		}
		TERRITORY_UPDATE = false;
		territories_to_update.clear();
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

// --------------- network utils ------------------
uint8_t get_nth_byte(uint8_t n, uint32_t num) {
	return uint8_t((num >> (8*n)) & 0xff);
}
void send_uint32(Connection *c, uint32_t num) {
	c->send(get_nth_byte(3, num));
	c->send(get_nth_byte(2, num));
	c->send(get_nth_byte(1, num));
	c->send(get_nth_byte(0, num));
}
void recv_uint32(std::vector< char > buffer, size_t &start, uint32_t &result) {
	result = uint32_t(((buffer[start] & 0xff) << 24) | 
						((buffer[start+1] & 0xff) << 16) | 
						((buffer[start+2] & 0xff) << 8) | 
						(buffer[start+3] & 0xff));
	start += 4;
}
void send_vector(Connection *c, std::vector< glm::uvec2 > data) {
	send_uint32(c, data.size());
	for (auto &vec : data) {
		send_uint32(c, vec.x);
		send_uint32(c, vec.y);
	}
}
void recv_vector(std::vector< char > buffer, size_t &start, std::vector< glm::uvec2 > &data) {
	uint32_t data_size;
	recv_uint32(buffer, start, data_size);
	for (uint32_t i = 0; i < data_size; i++) {
		uint32_t x; recv_uint32(buffer, start, x);
		uint32_t y; recv_uint32(buffer, start, y);
		data.emplace_back(glm::uvec2(x, y));
	}
}
void send_map(Connection *c, std::unordered_map<uint8_t, std::vector<glm::uvec2>> map) {
	send_uint32(c, map.size());
	for (std::pair<uint8_t, std::vector<glm::uvec2>> p : map) {
		c->send(p.first); 
		send_vector(c, p.second);
	}
}
void recv_map(std::vector< char > buffer, size_t &start, std::unordered_map<uint8_t, std::vector<glm::uvec2>> &map) {
	uint32_t map_size;
	recv_uint32(buffer, start, map_size);
	for (uint32_t i = 0; i < map_size; i++) {
		uint8_t id = buffer[start++];
		std::vector<glm::uvec2> data; recv_vector(buffer, start, data);
		map.insert({id, data});
	}
}
