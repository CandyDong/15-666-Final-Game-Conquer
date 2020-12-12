
#include "Connection.hpp"

#include "hex_dump.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <unordered_map>
#include <deque>
#include <algorithm>

const uint8_t NUM_ROWS = 20;
const uint8_t NUM_COLS = 40;
//const uint8_t MAX_NUM_PLAYERS = 4;
const uint8_t START_GAME_PLAYERS = 2; // TODO support 4 player games
const uint8_t START_HORIZONTAL_BORDER = (NUM_COLS - 10) / 2;
const uint8_t START_VERTICAL_BORDER = (NUM_ROWS - 10) / 2;
const uint8_t POWERUP_INTERVAL = 100; // 100 ticks = 10 seconds
const uint8_t BORDER_DECREMENT = 1;
const uint32_t LEVEL_GROW_INTERVAL = 40; // in ticks

struct Uvec2 {
	Uvec2(const uint32_t &_x, const uint32_t &_y): x(_x), y(_x) { }
	uint32_t x, y;
	inline Uvec2& operator = (const Uvec2 &a) {
		x = a.x;
		y = a.y;
		return *this;
	}
	inline bool operator == (const Uvec2 &a) { return x == a.x && y == a.y; }
	inline bool operator != (const Uvec2 &a) { return !(*this == a); }
};

//per-client state:
struct PlayerInfo {
	PlayerInfo(std::vector<Uvec2> &init_positions, uint8_t _id) { 
		id = _id;
		name = "Player " + std::to_string(id);
		do {
			x = (rand() % (NUM_COLS - START_HORIZONTAL_BORDER * 2)) + START_HORIZONTAL_BORDER;
			y = (rand() % (NUM_ROWS - START_VERTICAL_BORDER * 2)) + START_VERTICAL_BORDER;
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

struct Game {
	bool game_over = false;
	uint8_t start_countdown = 30; // 30 ticks = 3 seconds
	uint32_t tick = 0;
	bool powerup_placed = false;
	uint32_t powerup_timer = POWERUP_INTERVAL;
	uint8_t powerup_x, powerup_y;
	std::unordered_map< Connection *, PlayerInfo > players;
	std::vector<Uvec2> init_positions;
	uint8_t horizontal_border = START_HORIZONTAL_BORDER; // size of L/R walls
	uint8_t vertical_border = START_VERTICAL_BORDER; // size of T/B walls
};

static std::deque<Connection *> matchmaking_queue;
static std::vector<Game> games;

//static uint8_t winner_id;
//static size_t winner_score = 0;
//static bool GAME_OVER = false;

void add_to_matchmaking_queue(Connection* c) {
	matchmaking_queue.emplace_back(c);
	for (Connection* cc : matchmaking_queue) {
		cc->send('q');
		cc->send(uint8_t(matchmaking_queue.size()));
	}
	if (matchmaking_queue.size() >= START_GAME_PLAYERS) { // we have enough players to start a game
		games.push_back(Game());
		for (uint8_t i = 0; i < START_GAME_PLAYERS; i++) {
			Connection* cc = matchmaking_queue.front();
			matchmaking_queue.pop_front();
			games.back().players.emplace(cc, PlayerInfo(games.back().init_positions, i));
			cc->send('i');
			cc->send(i);
			cc->send('g');
			cc->send(uint8_t(games.back().horizontal_border));
			cc->send(uint8_t(games.back().vertical_border));
		}
	}
}

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
				}
				else if (evt == Connection::OnClose) {
					//client disconnected:
					//remove them from the matchmaking queue
					auto f = std::find(matchmaking_queue.begin(), matchmaking_queue.end(), c);
					if (f != matchmaking_queue.end()) {
						matchmaking_queue.erase(f);
						for (Connection* c : matchmaking_queue) {
							c->send('q');
							c->send(uint8_t(matchmaking_queue.size()));
						}
					}

					//remove them from the players list:
					for (auto it = games.rbegin(); it != games.rend(); it++) {
						auto &game = *it;
						auto f = game.players.find(c);
						if (f != game.players.end()) {
							game.players.erase(f);
							if (game.players.size() == 0) {
								games.erase(std::next(it).base());
								std::cout << "empty game, removing" << std::endl;
							}
						}
					}
				}
				else {
					assert(evt == Connection::OnRecv);

					// check if it's a join queue from main menu screen (this only occurs once per connection)
					if (c->recv_buffer.size() >= 1 && c->recv_buffer[0] == 'q') {
						add_to_matchmaking_queue(c);
						c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 1);
					}

					//look up in players list:
					for (auto it = games.rbegin(); it != games.rend(); it++) {
						auto &game = *it;
						auto f = game.players.find(c);
						if (f != game.players.end()) {
							PlayerInfo& player = f->second;

							//handle messages from client:
							while (c->recv_buffer.size() >= 1) {
								char type = c->recv_buffer[0];
								
								if (type == 'b') {
									if (c->recv_buffer.size() < 2) break;
									player.dir = c->recv_buffer[1];
									c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 2);
								}
								else if (type == 'd') { // disconnect from game, go back to lobby
									game.players.erase(f);
									if (game.players.size() == 0) {
										games.erase(std::next(it).base());
										std::cout << "empty game, removing" << std::endl;
									}
									add_to_matchmaking_queue(c);
									c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 1);
								}
								else if (type == 'l') {
									if (c->recv_buffer.size() < 3) break;
									game.powerup_timer = POWERUP_INTERVAL;
									game.powerup_placed = true;
									game.powerup_x = c->recv_buffer[1];
									game.powerup_y = c->recv_buffer[2];
									uint8_t powerup_type = rand() % 2;
									for (auto& it : game.players) {
										auto& c = it.first;
										c->send('p');
										c->send(powerup_type);
										c->send(game.powerup_x);
										c->send(game.powerup_y);
									}
									c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 3);
								}
								else {
									std::cout << "Unrecognized message received from client! recv_buffer = " << std::endl;
									std::cout << hex_dump(c->recv_buffer) << std::endl;
									//shut down client connection:
									c->close();
									return;
								}
							}
						}
					}
				}
				}, remain);
		}

		for (auto& game : games) {
			if (game.start_countdown > 0) {
				game.start_countdown--;
				for (auto& it : game.players) {
					auto &c = it.first;
					c->send('s');
					c->send(uint8_t(game.start_countdown));
				}
			}
			else {
				game.tick++;
				if (game.tick % LEVEL_GROW_INTERVAL == 0) {
					game.horizontal_border = std::max(0, game.horizontal_border - BORDER_DECREMENT);
					game.vertical_border = std::max(0, game.vertical_border - BORDER_DECREMENT);

					for (auto& it : game.players) {
						auto& c = it.first;
						c->send('g');
						c->send(uint8_t(game.horizontal_border));
						c->send(uint8_t(game.vertical_border));
					}
				}
				game.powerup_timer--;
				if (game.powerup_timer == 0) {
					// ask a player to generate a powerup location
					game.players.begin()->first->send('l');
				}
			}
		}

		//update current game states
		// update player position
		for (auto& game : games) {
			if (game.start_countdown == 0) {
				for (auto& it : game.players) {
					auto& player = it.second;
					if (player.dir == 8) continue; // none
					if (player.dir % 4 == 0 && player.x > game.horizontal_border) { // left
						player.x--;
					}
					else if (player.dir % 4 == 1 && player.x < NUM_COLS - 1 - game.horizontal_border) { // right
						player.x++;
					}
					else if (player.dir % 4 == 2 && player.y < NUM_ROWS - 1 - game.vertical_border) { // up
						player.y++;
					}
					else if (player.dir % 4 == 3 && player.y > game.vertical_border) { // down
						player.y--;
					}
					if (3 < player.dir) { // ll/rr/uu/dd
						if (player.dir % 4 == 0 && player.x > game.horizontal_border) { // left
							player.x--;
						}
						else if (player.dir % 4 == 1 && player.x < NUM_COLS - 1 - game.horizontal_border) { // right
							player.x++;
						}
						else if (player.dir % 4 == 2 && player.y < NUM_ROWS - 1 - game.vertical_border) { // up
							player.y++;
						}
						else if (player.dir % 4 == 3 && player.y > game.vertical_border) { // down
							player.y--;
						}
					}

					if (player.x == game.powerup_x && player.y == game.powerup_y) {
						game.powerup_timer = POWERUP_INTERVAL;
						game.powerup_placed = false;
					}
				}
			}
		}

		//send updated game state to all clients in all games
		for (auto& game : games) {
			for (auto& it : game.players) {
				auto& c = it.first;
				c->send('a');
				c->send(uint8_t(game.players.size()));
				// send along all player info
				for (auto& it_prime : game.players) {
					auto& player_prime = it_prime.second;
					c->send(uint8_t(player_prime.id));
					c->send(uint8_t(player_prime.dir));
					c->send(uint8_t(player_prime.x));
					c->send(uint8_t(player_prime.y));
				}
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
