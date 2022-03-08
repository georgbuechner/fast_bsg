#include "nlohmann/json_fwd.hpp"
#include "server/game/server_game.h"
#include "share/constants/codes.h"
#define NCURSES_NOMACROS
#include <chrono>
#include <cstdlib>
#include <curses.h>
#include <filesystem>
#include <lyra/lyra.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sstream>
#include <stdlib.h>
#include <spdlog/spdlog.h>
#include "spdlog/common.h"
#include "spdlog/sinks/basic_file_sink.h"

#include "share/audio/audio.h"
#include "server/websocket/websocket_server.h"
#include "client/websocket/client.h"
#include "client/game/client_game.h"
#include "share/tools/utils/utils.h"

#define LOGGER "logger"
#define ITERMAX 10000

/**
 * Sets up logger and potentially clears old logs.
 * @param[in] clear_log (if set, empties log-folder at base-path)
 * @param[in] base_path
 * @param[in] log_level.
 */
void SetupLogger(bool clear_log, std::string base_path, std::string log_level);

int main(int argc, const char** argv) {
  // Command line arguments 
  bool show_help = false;
  bool clear_log = false;
  std::string log_level = "warn";
  std::string base_path = getenv("HOME");
  base_path += "/.dissonance/";
  int server_port = 4444;
  bool multiplayer = false;
  std::string server_address = "ws://localhost:4444";
  bool standalone = false;
  bool only_ai = false;
  std::string path_sound_map = "dissonance//data/examples/Hear_My_Call-coffeeshoppers.mp3";
  std::string path_sound_ai_1 = "dissonance/data/examples/airtone_-_blackSnow_1.mp3";
  std::string path_sound_ai_2 = "dissonance/data/examples/Karstenholymoly_-_The_night_is_calling.mp3";

  // Setup command-line-arguments-parser
  auto cli = lyra::cli() 
    // Standard settings (clear-log, log-level and base-path)
    | lyra::opt(clear_log) ["-c"]["--clear-log"]("If set, removes all log-files before starting the game.")
    | lyra::opt(log_level, "options: [warn, info, debug], default: \"warn\"") ["-l"]["--log_level"]("set log-level")
    | lyra::opt(base_path, "path to dissonance files") 
        ["-p"]["--base-path"]("Set path to dissonance files (logs, settings, data)")

    // multi-player/ standalone.
    | lyra::opt(multiplayer) ["-m"]["--multiplayer"]("If set, starts a multi-player game.")
    | lyra::opt(standalone) ["-s"]["--standalone"]("If set, starts only server.")
    | lyra::opt(server_address, "format [ws://<url>:<port> | wss://<url>:<port>], default: wss://kava-i.de:4444") 
        ["-z"]["--connect"]("specify address which to connect to.")

    | lyra::opt(only_ai) ["--only-ai"]("If set, starts game between to ais.")
    | lyra::opt(path_sound_map, "for ai games: map sound input") ["--map_sound"]("")
    | lyra::opt(path_sound_ai_1, "for ai games: ai-1 sound input") ["--ai1_sound"]("")
    | lyra::opt(path_sound_ai_2, "for ai games: ai-2 sound input") ["--ai2_sound"]("");

  cli.add_argument(lyra::help(show_help));
  auto result = cli.parse({ argc, argv });

  // Print help and exit. 
  if (show_help) {
    std::cout << cli;
    return 0;
  }

  // Setup logger.
  SetupLogger(clear_log, base_path, log_level);
  
  // Initialize random numbers and audio.
  srand (time(NULL));
  Audio::Initialize();

  // Enter-username (omitted for standalone server or only-ai)
  std::string username = "";
  if (!standalone && !only_ai) {
    std::cout << "Enter your username: ";
    std::getline(std::cin, username);
  }

  /* 
   * Start games: 
   * 1. only-ai-game (no websockets)
   * 2. websocket-server (for single-player and standalone)
   * 3. client (for multi-player)
   *
   * 1. depens on `--only_ai`.
   * 2. and 3. depend on `--standalone` and `--multiplayer`. 
   * If a) both are set, start websocket-server (localhost) and client.
   * If b) only `--standalone` is set, start only server at given adress.
   * If c) only `--multiplayer` is set, start only client.
  */

  // only ai-game
  if (only_ai) {
    ServerGame* game = new ServerGame(50, 50, AI_GAME, 2, base_path, nullptr);
    game->InitAiGame(base_path, path_sound_map, path_sound_ai_1, path_sound_ai_2);
    utils::WaitABit(100);
    return 0;
  }
  
  // websocket server.
  WebsocketServer* srv = new WebsocketServer(standalone);
  std::thread thread_server([srv, server_port, multiplayer, standalone]() { 
    if (!multiplayer) {
      if (standalone)
        std::cout << "Server started on port: " << server_port << std::endl;
      srv->Start(server_port);
    }
  });
  std::thread thread_kill_games([srv, multiplayer]() {
    if (!multiplayer)
      srv->CloseGames();
  });

  // client and client-game.
  ClientGame::init();
  ClientGame* client_game = (standalone) ? nullptr : new ClientGame(base_path, username, multiplayer);
  Client* client = (standalone) ? nullptr : new Client(client_game, username);
  if (client_game)
    client_game->set_client(client);
  std::thread thread_client([client, server_address]() { if (client) client->Start(server_address); });
  std::thread thread_client_input([client_game, client]() { 
    if (client) {
      client_game->GetAction(); 
      sleep(1);
      //client->Stop();
    }
  });

  thread_server.join();
  thread_kill_games.join();
  thread_client.join();
  thread_client_input.join();
}

void SetupLogger(bool clear_log, std::string base_path, std::string log_level) {
  // clear log
  if (clear_log)
    std::filesystem::remove_all(base_path + "logs/");

  // Logger 
  std::string logger_file = "logs/" + utils::GetFormatedDatetime() + "_logfile.txt";
  auto logger = spdlog::basic_logger_mt("logger", base_path + logger_file);
  spdlog::flush_on(spdlog::level::warn);
  if (log_level == "warn")
    spdlog::set_level(spdlog::level::warn);
  else if (log_level == "info") {
    spdlog::set_level(spdlog::level::info);
    spdlog::flush_on(spdlog::level::info);
  }
  else if (log_level == "debug") {
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::debug);
    spdlog::flush_every(std::chrono::seconds(1));
  }
}
