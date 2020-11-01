
#include "Connection.hpp"

#include "hex_dump.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <unordered_map>
#include <deque>

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


	//------------ main loop ------------
	constexpr float ServerTick = 1.0f / 10.0f; //TODO: set a server tick that makes sense for your game

	//server state:
	static std::deque<uint32_t> unused_player_ids = {0, 1, 2, 3, 4};
	//per-client state:
	struct PlayerInfo {
		PlayerInfo() { 
			id = unused_player_ids.front();
			unused_player_ids.pop_front();
			name = "Player " + std::to_string(id);
		}
		std::string name;
		uint32_t id;

		uint32_t left_presses = 0;
		uint32_t right_presses = 0;
		uint32_t up_presses = 0;
		uint32_t down_presses = 0;

		int32_t total = 0;

	};
	std::unordered_map< Connection *, PlayerInfo > players;

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
					//client connected:
					if (unused_player_ids.empty()) { // server is full
						c->close();
					} else {
						//create some player info for them:
						players.emplace(c, PlayerInfo());
					}
				} else if (evt == Connection::OnClose) {
					//client disconnected:

					//remove them from the players list:
					auto f = players.find(c);
					assert(f != players.end());
					unused_player_ids.emplace_back(f->second.id);
					players.erase(f);

				} else { assert(evt == Connection::OnRecv);
					//got data from client:
					std::cout << "got bytes:\n" << hex_dump(c->recv_buffer); std::cout.flush();

					//look up in players list:
					auto f = players.find(c);
					assert(f != players.end());
					PlayerInfo &player = f->second;

					//handle messages from client:
					//TODO: update for the sorts of messages your clients send
					while (c->recv_buffer.size() >= 5) {
						//expecting five-byte messages 'b' (left count) (right count) (down count) (up count)
						char type = c->recv_buffer[0];
						if (type != 'b') {
							std::cout << " message of non-'b' type received from client!" << std::endl;
							//shut down client connection:
							c->close();
							return;
						}
						uint8_t left_count = c->recv_buffer[1];
						uint8_t right_count = c->recv_buffer[2];
						uint8_t down_count = c->recv_buffer[3];
						uint8_t up_count = c->recv_buffer[4];

						player.left_presses += left_count;
						player.right_presses += right_count;
						player.down_presses += down_count;
						player.up_presses += up_count;

						c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 5);
					}
				}
			}, remain);
		}

		//update current game state
		//TODO: replace with *your* game state update

		//send updated game state to all clients
		//TODO: update for your game state
		for (auto &[c, player] : players) {
			(void)player; //work around "unused variable" warning on whatever g++ github actions uses
			//send an update starting with 'm', a 24-bit size, and a blob of text:
			c->send('a');
			c->send(uint8_t(players.size()));
			// send along all player info
			for (auto &[c_prime, player_prime] : players) {
				(void)c_prime;
				c->send(uint8_t(player_prime.id));
				c->send(uint8_t(player_prime.name.size())); // TODO(candy): make sure length of name is no longer than one byte
				// std::cout << "name=" << player_prime.name << ", size=" << player_prime.name.size() << std::endl;
				c->send_buffer.insert(c->send_buffer.end(), player_prime.name.begin(), player_prime.name.end());
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
