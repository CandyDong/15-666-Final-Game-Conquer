
#include "Connection.hpp"

#include "hex_dump.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <unordered_map>
#include <deque>
#include <algorithm>

const uint8_t NUM_ROWS = 50;
const uint8_t NUM_COLS = 80;
const uint8_t MAX_NUM_PLAYERS = 4;

struct Uvec2 {
	Uvec2(const uint32_t &_x, const uint32_t &_y): x(_x), y(_x) { }
	uint32_t x, y;
	inline Uvec2& operator = (const Uvec2 &a) {
		x = a.x;
		y = b.y;
		return *this;
	}
	inline bool operator == (const Uvec2 &a) return x == a.x && y == a.y;
	inline bool operator != (const Uvec2 &a) return !(*this == a);
};

//server state:
static std::deque<uint32_t> unused_player_ids;
static std::vector<Uvec2> init_positions;

uint8_t horizontal_border = (NUM_COLS - 20) / 2; // size of L/R walls
uint8_t vertical_border = (NUM_ROWS - 20) / 2; // size of T/B walls
const uint8_t BORDER_DECREMENT = 3;
const uint32_t LEVEL_GROW_INTERVAL = 100; // in ticks

//per-client state:
struct PlayerInfo {
	PlayerInfo() { 
		id = unused_player_ids.front();
		unused_player_ids.pop_front();
		name = "Player " + std::to_string(id);
		do {
			x = (rand() % (NUM_COLS - horizontal_border * 2)) + horizontal_border;
			y = (rand() % (NUM_ROWS - vertical_border * 2)) + vertical_border;
		} while (std::find(init_positions.begin(),
							init_positions.end(),
							Uvec2(x, y))
				!= init_positions.end());
		init_positions.emplace_back(x, y);
		
		std::cout << name << " connected: (" + std::to_string(x) + ", " + std::to_string(y) + ");" << std::endl;
	}
	std::string name;
	uint8_t id;
	uint8_t dir = 8;
	uint8_t x, y;
};

static std::unordered_map< Connection *, PlayerInfo > players;
//static uint8_t winner_id;
//static size_t winner_score = 0;
//static bool GAME_OVER = false;

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
		static uint32_t tick = 0;
		//process incoming data from clients until a tick has elapsed:
		while (true) {
			auto now = std::chrono::steady_clock::now();
			double remain = std::chrono::duration< double >(next_tick - now).count();
			if (remain < 0.0) {
				next_tick += std::chrono::duration< double >(ServerTick);
				tick++;
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
						c->send('g');
						c->send(uint8_t(horizontal_border));
						c->send(uint8_t(vertical_border));
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
						if (type != 'b') {
							std::cout << " message of non-'b' type received from client!" << std::endl;
							//shut down client connection:
							c->close();
							return;
						}
						player.dir = c->recv_buffer[1];

						c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 2);
					}
				}
				}, remain);
		}

		if (tick % LEVEL_GROW_INTERVAL == 0) {
			horizontal_border = std::max(0, horizontal_border - BORDER_DECREMENT);
			vertical_border = std::max(0, vertical_border - BORDER_DECREMENT);

			for (auto& [c, player] : players) {
				(void)player; //work around "unused variable" warning on whatever g++ github actions uses
				c->send('g');
				c->send(uint8_t(horizontal_border));
				c->send(uint8_t(vertical_border));
			}
		}

		//update current game state
		//TODO: replace with *your* game state update
		// update player position
		for (auto& [c, player] : players) {
			(void)c;
			if (player.dir == 8) continue; // none
			if (player.dir % 4 == 0 && player.x > horizontal_border) { // left
				player.x--;
			}
			else if (player.dir % 4 == 1 && player.x < NUM_COLS - 1 - horizontal_border) { // right
				player.x++;
			}
			else if (player.dir % 4 == 2 && player.y < NUM_ROWS - 1 - vertical_border) { // up
				player.y++;
			}
			else if (player.dir % 4 == 3 && player.y > vertical_border) { // down
				player.y--;
			}
			if (3 < player.dir) { // ll/rr/uu/dd
				if (player.dir % 4 == 0 && player.x > horizontal_border) { // left
					player.x--;
				}
				else if (player.dir % 4 == 1 && player.x < NUM_COLS - 1 - horizontal_border) { // right
					player.x++;
				}
				else if (player.dir % 4 == 2 && player.y < NUM_ROWS - 1 - vertical_border) { // up
					player.y++;
				}
				else if (player.dir % 4 == 3 && player.y > vertical_border) { // down
					player.y--;
				}
			}
		}

		//send updated game state to all clients
		//TODO: update for your game state
		for (auto& [c, player] : players) {
			(void)player; //work around "unused variable" warning on whatever g++ github actions uses
			c->send('a');
			c->send(uint8_t(players.size()));
			// send along all player info
			for (auto& [c_prime, player_prime] : players) {
				(void)c_prime;
				c->send(uint8_t(player_prime.id));
				c->send(uint8_t(player_prime.x));
				c->send(uint8_t(player_prime.y));
			}
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