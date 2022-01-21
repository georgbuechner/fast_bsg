#include "server/game/server_game.h"
#include "nlohmann/json_fwd.hpp"
#include "server/game/player/audio_ki.h"
#include "server/game/player/player.h"
#include "share/audio/audio.h"
#include "share/constants/codes.h"
#include "share/defines.h"
#include "share/objects/dtos.h"
#include "share/objects/transfer.h"
#include "share/objects/units.h"
#include "server/websocket/websocket_server.h"
#include "share/tools/eventmanager.h"
#include "share/tools/random/random.h"
#include "share/tools/utils/utils.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

bool IsAi(std::string username) {
  return username.find("AI") != std::string::npos;
}

ServerGame::ServerGame(int lines, int cols, int mode, int num_players, std::string base_path, 
    WebsocketServer* srv, float speed) : num_players_(num_players), audio_(base_path), ws_server_(srv), 
    status_(WAITING), mode_(mode), lines_(lines), cols_(cols), ai_speed_(speed) {
  // Initialize eventmanager.
  eventmanager_.AddHandler("initialize_game", &ServerGame::m_InitializeGame);
  eventmanager_.AddHandler("add_iron", &ServerGame::m_AddIron);
  eventmanager_.AddHandler("remove_iron", &ServerGame::m_RemoveIron);
  eventmanager_.AddHandler("add_technology", &ServerGame::m_AddTechnology);
  eventmanager_.AddHandler("resign", &ServerGame::m_Resign);
  eventmanager_.AddHandler("check_build_neuron", &ServerGame::m_CheckBuildNeuron);
  eventmanager_.AddHandler("check_build_potential", &ServerGame::m_CheckBuildPotential);
  eventmanager_.AddHandler("build_neuron", &ServerGame::m_BuildNeurons);
  eventmanager_.AddHandler("get_positions", &ServerGame::m_GetPositions);
  eventmanager_.AddHandler("toggle_swarm_attack", &ServerGame::m_ToggleSwarmAttack);
  eventmanager_.AddHandler("set_way_point", &ServerGame::m_SetWayPoint);
  eventmanager_.AddHandler("set_ipsp_target", &ServerGame::m_SetIpspTarget);
  eventmanager_.AddHandler("set_epsp_target", &ServerGame::m_SetEpspTarget);
}

int ServerGame::status() {
  return status_;
}

int ServerGame::mode() {
  return mode_;
}

void ServerGame::set_status(int status) {
  std::unique_lock ul(mutex_status_);
  status_ = status;
}

void ServerGame::PrintStatistics() {
  for (const auto& it : players_) {
    std::cout << it.first << std::endl;
    it.second->statistics().print();
  }
}

void ServerGame::AddPlayer(std::string username, int lines, int cols) {
  std::unique_lock ul(mutex_players_);
  spdlog::get(LOGGER)->info("ServerGame::AddPlayer: {}", username);
  // Check is free slots in lobby.
  if (players_.size() < num_players_) {
    spdlog::get(LOGGER)->debug("ServerGame::AddPlayer: adding user.");
    players_[username] = nullptr;
    // Adjust field size and width
    lines_ = (lines < lines_) ? lines : lines_;
    cols_ = (cols < cols_) ? cols : cols_;
  }
  // Only start game if status is still waiting, to avoid starting game twice.
  if (players_.size() >= num_players_ && status_ == WAITING_FOR_PLAYERS) {
    spdlog::get(LOGGER)->debug("ServerGame::AddPlayer: starting game.");
    ul.unlock();
    StartGame({});
  }
}

nlohmann::json ServerGame::HandleInput(std::string command, nlohmann::json msg) {
  if (eventmanager_.handlers().count(command))
    (this->*eventmanager_.handlers().at(command))(msg);
  else 
    msg = nlohmann::json();
  spdlog::get(LOGGER)->debug("ClientGame::HandleAction: response {}", msg.dump());
  return msg;
}

// command methods

void ServerGame::m_AddIron(nlohmann::json& msg) {
  std::string username = msg["username"];
  Player* player = (players_.count(username) > 0) ? players_.at(username) : NULL;
  int resource = msg["data"]["resource"];
  if (player && player->DistributeIron(resource)) {
    msg = {{"command", "set_msg"}, {"data", {{"msg", "Distribute iron: done!"}} }};
    spdlog::get(LOGGER)->debug("ServerGame::m_AddIron: checking whether to send player new resource-neuron: {} cur: {}", 
        resource, player->resources().at(resource).distributed_iron());
    // If resource is newly created, send client resource-neuron as new unit.
    if (player->resources().at(resource).distributed_iron() == 2) {
      spdlog::get(LOGGER)->debug("ServerGame::m_AddIron: sending player new resource-neuron");
      position_t pos = player->resources().at(resource).pos();
      spdlog::get(LOGGER)->debug("ServerGame::m_AddIron: 1");
      nlohmann::json req = {{"command", "set_unit"}, {"data", {{"unit", RESOURCENEURON}, {"pos", pos}, 
          {"color", COLOR_RESOURCES}} }};
      spdlog::get(LOGGER)->debug("ServerGame::m_AddIron: 2");
      ws_server_->SendMessage(username, req.dump());
      spdlog::get(LOGGER)->debug("ServerGame::m_AddIron: 3");
    }
  }
  else 
    msg = {{"command", "set_msg"}, {"data", {{"msg", "Distribute iron: not enough iron!"}} }};
}

void ServerGame::m_RemoveIron(nlohmann::json& msg) {
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  int resource = msg["data"]["resource"];
  if (player && player->RemoveIron(msg["data"]["resource"])) {
    msg = {{"command", "set_msg"}, {"data", {{"msg", "Remove iron: done!"}} }};
    // If resource is newly created, send client resource-neuron as new unit.
    if (player->resources().at(resource).bound() == 1) {
      spdlog::get(LOGGER)->debug("ServerGame::m_AddIron: sending player removed resource-neuron");
      position_t pos = player->resources().at(resource).pos();
      nlohmann::json req = {{"command", "set_unit"}, {"data", {{"unit", RESOURCENEURON}, {"pos", pos}, 
          {"color", COLOR_DEFAULT}} }};
      ws_server_->SendMessage(msg["username"], req.dump());
    }
  }
  else 
    msg = {{"command", "set_msg"}, {"data", {{"msg", "Remove iron: not enough iron!"}} }};
}

void ServerGame::m_AddTechnology(nlohmann::json& msg) {
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  if (player && player->AddTechnology(msg["data"]["technology"]))
    msg = {{"command", "set_msg"}, {"data", {{"msg", "Add technology: done!"}} }};
  else 
    msg = {{"command", "set_msg"}, {"data", {{"msg", "Add technology: probably not enough resources!"}} }};
}

void ServerGame::m_Resign(nlohmann::json& msg) {
  std::unique_lock ul(mutex_status_);
  status_ = CLOSING;
  ul.unlock();
  // If multi player, inform other player.
  nlohmann::json resp = {{"command", "game_end"}, {"data", {{"msg", "YOU WON - opponent resigned"}} }};
  SendMessageToAllPlayers(resp.dump(), msg["username"]);
  msg = nlohmann::json();
}

void ServerGame::m_CheckBuildNeuron(nlohmann::json& msg) {
  int unit = msg["data"]["unit"];
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  if (player) {
    std::string missing = GetMissingResourceStr(player->GetMissingResources(unit));
    std::vector<position_t> positions = player->GetAllPositionsOfNeurons(NUCLEUS);
    // Can be build (enough resources) and start-position for selecting pos is known (only one nucleus)
    if (missing == "" && positions.size() == 1)
      msg = {{"command", "build_neuron"}, {"data", {{"unit", unit}, {"start_pos", positions.front()}, 
        {"range", player->cur_range()}} }}; 
    // Can be build (enough resources) and start-position for selecting pos is unknown (multiple nucleus)
    else if (missing == "") 
      msg = {{"command", "build_neuron"}, {"data", {{"unit", unit}, {"positions", positions}, 
        {"range", player->cur_range()}} }}; 
    else 
      msg = {{"command", "set_msg"}, {"data", {{"msg", "Not enough resource! missing: " + missing}}}};
  }
}

void ServerGame::m_CheckBuildPotential(nlohmann::json& msg) {
  int unit = msg["data"]["unit"];
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  if (player) {
    auto synapses = player->GetAllPositionsOfNeurons(SYNAPSE);
    std::string missing = GetMissingResourceStr(player->GetMissingResources(unit));
    // Missing resources => error message
    if (missing != "")
      msg = {{"command", "set_msg"}, {"data", {{"msg", "Not enough resource! missing: " + missing}}}};
    // No synapses => error message
    else if (synapses.size() == 0)
      msg = {{"command", "set_msg"}, {"data", {{"msg", "No synapse!"}} }};
    // Only one synapse or player specified position => add potential
    else if (synapses.size() == 1 || msg["data"].contains("start_pos")) {
      position_t pos = (synapses.size() == 1) ? synapses.front() : utils::PositionFromVector(msg["data"]["start_pos"]);
      BuildPotentials(unit, pos, msg["data"]["num"], msg["username"], player);
      msg = nlohmann::json();
    }
    // More than one synapse and position not given => tell user to select position.
    else  
      msg = {{"command", "build_potential"}, {"data", {{"unit", unit}, {"positions", 
        player->GetAllPositionsOfNeurons(SYNAPSE)}, {"num", msg["data"]["num"]}} }}; 
  }
}

void ServerGame::m_BuildNeurons(nlohmann::json& msg) {
  int unit = msg["data"]["unit"];
  position_t pos = {msg["data"]["pos"][0], msg["data"]["pos"][1]};
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  if (player) {
    bool success = false;
    // In case of synapse get random position for epsp- and ipsp-target.
    if (unit == SYNAPSE)
      success = player->AddNeuron(pos, unit, player->enemies().front()->GetOneNucleus(), 
        player->enemies().front()->GetRandomNeuron());
    // Otherwise simply add.
    else 
      success = player->AddNeuron(pos, unit);
    // Add position to field, tell all players to add position and send success mesage.
    if (success) {
      field_->AddNewUnitToPos(pos, unit);
      msg = {{"command", "set_unit"}, {"data", {{"unit", unit}, {"pos", pos}, 
        {"color", player->color()}} }};
    }
    else
      msg = {{"command", "set_msg"}, {"data", {{"msg", "Failed!"}} }};
  }
}

void ServerGame::m_GetPositions(nlohmann::json& msg) {
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  spdlog::get(LOGGER)->debug("ServerGame::m_GetPositions: dezerialising dto.");
  GetPosition req = GetPosition(msg);
  spdlog::get(LOGGER)->debug("ServerGame::m_GetPositions: dezerialising dto done.");
  if (player) {
    std::vector<std::vector<position_t>> all_positions;
    for (const auto& it : req.position_requests()) {
      std::vector<position_t> positions;
      // player-units
      if (it.first == Positions::PLAYER)
        positions = player->GetAllPositionsOfNeurons(it.second.unit());
      // enemy-units
      else if (it.first == Positions::ENEMY) {
        for (const auto& enemy : player->enemies()) 
          for (const auto& it : enemy->GetAllPositionsOfNeurons(it.second.unit()))
            positions.push_back(it);
      }
      // center-positions
      else if (it.first == Positions::CENTER)
        positions = field_->GetAllCenterPositionsOfSections();
      // ipsp-/ epsp-targets
      else if (it.first == Positions::TARGETS) {
        position_t ipsp_target_pos = player->GetSynapesTarget(it.second.pos(), it.second.unit());
        if (ipsp_target_pos.first != -1)
          positions.push_back(ipsp_target_pos);
      }
      else if (it.first == Positions::CURRENT_WAY) {
        // Get way to ipsp-target
        for (const auto& it : field_->GetWayForSoldier(it.second.pos(), 
              player->GetSynapesWayPoints(it.second.pos(), IPSP)))
          positions.push_back(it);
        // Get way to epsp-target
        for (const auto& it : field_->GetWayForSoldier(it.second.pos(), 
              player->GetSynapesWayPoints(it.second.pos(), EPSP)))
          positions.push_back(it);
      }
      else if (it.first == Positions::CURRENT_WAY_POINTS) {
        positions = player->GetSynapesWayPoints(it.second.pos());
      }
      all_positions.push_back(positions);
    }
    msg = {{"command", msg["data"]["return_cmd"]}, {"data", {{"positions", all_positions}} }};
  }
}

void ServerGame::m_ToggleSwarmAttack(nlohmann::json& msg) {
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  if (player) {
    std::string on_off = (player->SwitchSwarmAttack(utils::PositionFromVector(msg["data"]["pos"]))) ? "on" : "off";
    msg = {{"command", "set_msg"}, {"data", {{"msg", "Toggle swarm-attack successfull. Swarm attack " + on_off}} }};
  }
}

void ServerGame::m_SetWayPoint(nlohmann::json& msg) {
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  if (player) {
    int num = msg["data"]["num"];
    int tech = player->technologies().at(WAY).first;
    position_t synapse_pos = utils::PositionFromVector(msg["data"]["synapse_pos"]);
    std::string x_of = std::to_string(num) + "/" + std::to_string(tech);
    if (num == 1) 
      player->ResetWayForSynapse(synapse_pos, msg["data"]["pos"]);
    else 
      player->AddWayPosForSynapse(synapse_pos, msg["data"]["pos"]);
    msg = {{"command", "set_wps"}, {"data", {{"msg", "New way-point added: " + x_of}, {"synapse_pos", synapse_pos}} }};
    msg["data"]["num"] = (num < tech) ? num+1 : -1;  // indicate setting next way-point or that last way-point was set.
  }
}

void ServerGame::m_SetIpspTarget(nlohmann::json& msg) {
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  if (player) {
    player->ChangeIpspTargetForSynapse(msg["data"]["synapse_pos"], msg["data"]["pos"]);
    msg = {{"command", "set_msg"}, {"data", {{"msg", "Ipsp target for this synapse set"}} }};
  }
}

void ServerGame::m_SetEpspTarget(nlohmann::json& msg) {
  Player* player = (players_.count(msg["username"]) > 0) ? players_.at(msg["username"]) : NULL;
  if (player) {
    player->ChangeEpspTargetForSynapse(msg["data"]["synapse_pos"], msg["data"]["pos"]);
    msg = {{"command", "set_msg"}, {"data", {{"msg", "Epsp target for this synapse set"}} }};
  }
}


void ServerGame::BuildPotentials(int unit, position_t pos, int num_potenials_to_build, 
    std::string username, Player* player) {
  bool success = false;
  for (int i=0; i < num_potenials_to_build; i++) {
    success = player->AddPotential(pos, unit);
    if (!success)
      break;
    // Wait a bit.
    utils::WaitABit(110);
  }
  nlohmann::json msg = {{"command", "set_msg"}, {"data", {{"msg", "Success!"}} }};
  if (!success)
    msg["data"]["msg"] = "Failed!";
  ws_server_->SendMessage(username, msg.dump());
}

void ServerGame::m_InitializeGame(nlohmann::json& msg) {
  std::string username = msg["username"];
  spdlog::get(LOGGER)->info("ServerGame::InitializeGame: initializing with user: {}", username);
  nlohmann::json data = msg["data"];

  // Get and analyze main audio-file (used for map and in SP for AI).
  std::string source_path = data["source_path"];
  spdlog::get(LOGGER)->info("ServerGame::InitializeGame: Selected path: {}", source_path);
  audio_.set_source_path(source_path);
  audio_.Analyze();
  spdlog::get(LOGGER)->info("ServerGame::InitializeGame: audio has {} beats", audio_.analysed_data().data_per_beat_.size());

  // Get and analyze audio-files for AIs (OBSERVER-mode).
  std::vector<Audio*> audios; 
  if (msg["data"].contains("ais")) {
    for (const auto& it : msg["data"]["ais"]) {
      Audio* new_audio = new Audio(msg["data"]["base_path"].get<std::string>());
      new_audio->set_source_path(it);
      new_audio->Analyze();
      audios.push_back(new_audio);
    }
  }

  // Add host to players.
  if (mode_ < OBSERVER)
    players_[username] = nullptr;
  else if (mode_ == OBSERVER)
    observers_.push_back(username);

  // If SINGLE_PLAYER, add AI to players.
  if (mode_ == SINGLE_PLAYER) {
    players_["AI (" + audio_.filename(true) + ")"] = nullptr;
    StartGame(audios);
  }
  else if (mode_ >= OBSERVER) {
    players_["AI (" + audios[0]->filename(true) + ")"] = nullptr;
    players_["AI (" + audios[1]->filename(true) + ")"] = nullptr;
    StartGame(audios);
  }
  // Otherwise send info "waiting for players" to host.
  else {
    status_ = WAITING_FOR_PLAYERS;
    msg["command"] = "print_msg";
    msg["data"] = {{"msg", "Wainting for players..."}};
  }
}

void ServerGame::StartGame(std::vector<Audio*> audios) {
  // Initialize field.
  RandomGenerator* ran_gen = new RandomGenerator(audio_.analysed_data(), &RandomGenerator::ran_note);
  auto nucleus_positions = SetUpField(ran_gen);
  if (nucleus_positions.size() < num_players_)
    return;

  // Setup players.
  std::string nucleus_positions_str = "nucleus': ";
  for (const auto& it : nucleus_positions)
    nucleus_positions_str += utils::PositionToString(it) + ", ";
  spdlog::get(LOGGER)->info("ServerGame::InitializeGame: Creating {} players at {}", num_players_, nucleus_positions_str);
  unsigned int counter = 0;
  unsigned int ai_counter = 0;
  for (const auto& it : players_) {
    int color = (counter % 4) + 10;
    if (IsAi(it.first)) {
      if (audios.size() == 0)
        players_[it.first] = new AudioKi(nucleus_positions[counter], field_, &audio_, ran_gen, color);
      else 
        players_[it.first] = new AudioKi(nucleus_positions[counter], field_, audios[ai_counter++], ran_gen, color);
    }
    else {
      players_[it.first] = new Player(nucleus_positions[counter], field_, ran_gen, color);
      human_players_[it.first] = players_[it.first];
    }
    counter++;
  }
  // Pass all players a vector of all enemies.
  spdlog::get(LOGGER)->info("ServerGame::InitializeGame: Setting enemies for each player");
  for (const auto& it : players_) {
    std::vector<Player*> enemies;
    for (const auto& jt : players_)
      if (jt.first != it.first) 
        enemies.push_back(jt.second);
    it.second->set_enemies(enemies);
  }
  
  // Inform players, to start game with initial field included
  CreateAndSendTransferToAllPlayers(0, false);

  // Start two main threads.
  status_ = SETTING_UP;
  for (const auto& it : players_) {
    if (IsAi(it.first)) {
      std::thread ai([this, it]() { Thread_Ai(it.first); });
      ai.detach();
    }
  }
  // Start update-shedule.
  std::thread update([this]() { Thread_RenderField(); });
  update.detach();
  return ;
}

std::vector<position_t> ServerGame::SetUpField(RandomGenerator* ran_gen) {
  RandomGenerator* map_1 = new RandomGenerator(audio_.analysed_data(), &RandomGenerator::ran_boolean_minor_interval);
  RandomGenerator* map_2 = new RandomGenerator(audio_.analysed_data(), &RandomGenerator::ran_level_peaks);
  // Create field.
  field_ = nullptr;
  spdlog::get(LOGGER)->info("ServerGame::InitializeGame: creating map. field initialized? {}", field_ != nullptr);
  std::vector<position_t> nucleus_positions;
  int denseness = 0;
  while (!field_ && denseness < 3) {
    field_ = new Field(lines_, cols_, ran_gen);
    field_->AddHills(map_1, map_2, denseness++);
    field_->BuildGraph();
    nucleus_positions = field_->AddNucleus(num_players_);
    if (nucleus_positions.size() == 0) {
      delete field_;
      field_ = nullptr;
    }
  }
  delete map_1;
  delete map_2;
  // Check if map is playable (all nucleus-positions could be found)
  if (!field_) {
    nlohmann::json msg = {{"command", "print_msg"}, {"data", {{"msg", "Game cannot be played with this song, "
      "as map is unplayable. It might work with a higher resolution. (dissonance -r)"}} }};
    SendMessageToAllPlayers(msg.dump());
  }
  spdlog::get(LOGGER)->info("ServerGame::InitializeGame: successfully created map.");
  return nucleus_positions;
}

void ServerGame::Thread_RenderField() {
  std::cout << "Game::Thread_RenderField: started. status? " << status_ << std::endl;
  spdlog::get(LOGGER)->info("");
  auto audio_start_time = std::chrono::steady_clock::now();
  // spdlog::get(LOGGER)->info("Game::Thread_RenderField: started 1");
  std::list<AudioDataTimePoint> data_per_beat = audio_.analysed_data().data_per_beat_;
  auto last_update = std::chrono::steady_clock::now();
  double render_frequency = 40;
 
  auto data_at_beat = data_per_beat.front();
  while (status_ < CLOSING) {
    auto cur_time = std::chrono::steady_clock::now();
    // Analyze audio data.
    if (utils::GetElapsed(audio_start_time, cur_time)>= data_at_beat.time_) {
      spdlog::get(LOGGER)->debug("Game::RenderField: next data_at_beat");
      // Update render-frequency.
      render_frequency = 60000.0/(data_at_beat.bpm_*16);
      data_per_beat.pop_front();
      // All players lost, because time is up:
      if (data_per_beat.size() == 0) {
        nlohmann::json resp = {{"command", "game_end"}, {"data", {{"msg", "YOU LOST - times up"}} }};
        SendMessageToAllPlayers(resp.dump());
      }
      else 
        data_at_beat = data_per_beat.front();
      // Increase resources for all non-ai players.
      for (const auto& it : human_players_)
        it.second->IncreaseResources(audio_.MoreOffNotes(data_at_beat));
    }

    // Move potential
    if (utils::GetElapsed(last_update, cur_time) > render_frequency) {
      // Move potentials of all players.
      std::unique_lock ul(mutex_players_);
      for (const auto& it : human_players_)
        it.second->MovePotential(1);
      ul.unlock();
      // After potentials where move check if a new player has lost and whether a player has scouted new enemy neuerons
      HandlePlayersLost();
      SendScoutedNeurons();
      // Handle actiaved-neurons of all players.
      ul.lock();
      for (const auto& it : human_players_)
        it.second->HandleDef(1);
      ul.unlock();
      // Create player agnostic transfer-data
      CreateAndSendTransferToAllPlayers(1-(static_cast<float>(data_per_beat.size())
            /audio_.analysed_data().data_per_beat_.size()));
      // Refresh page
      last_update = cur_time;
      spdlog::get(LOGGER)->debug("Game::RenderField: checking... render_frequency {}", render_frequency);
    }
  } 
  sleep(1);
  std::unique_lock ul(mutex_status_);
  delete field_;
  for (const auto& it : players_)
    delete players_[it.first];
  status_ = CLOSED;
  spdlog::get(LOGGER)->info("Game::Thread_RenderField: ended");
}

void ServerGame::Thread_Ai(std::string username) {
  std::cout << "Game::Thread_Ai: started: " << username << std::endl;
  // spdlog::get(LOGGER)->info("Game::Thread_Ai: started: {}", username);
  auto audio_start_time = std::chrono::steady_clock::now();
  std::list<AudioDataTimePoint> data_per_beat = players_.at(username)->data_per_beat();
  // spdlog::get(LOGGER)->info("Game::Thread_Ai: audio-data for player {} has {} beats", username, data_per_beat.size());
  Player* ai = players_.at(username);

  // Markers for unit-updates
  auto last_update = std::chrono::steady_clock::now();
  double render_frequency = 40;

  // Handle building neurons and potentials.
  auto data_at_beat = data_per_beat.front();
  while(ai && !ai->HasLost() && status_ < CLOSING) {
    auto cur_time = std::chrono::steady_clock::now();
    // Analyze audio data.
    if (utils::GetElapsed(audio_start_time, cur_time) >= data_at_beat.time_*ai_speed_) {
      // Do action.
      ai->DoAction(data_at_beat);
      ai->set_last_time_point(data_at_beat);
      // Increase reasources twice every beat.
      ai->IncreaseResources(audio_.MoreOffNotes(data_at_beat));
      ai->IncreaseResources(audio_.MoreOffNotes(data_at_beat));
      // Update render-frequency.
      render_frequency = 60000.0/(data_at_beat.bpm_*16)*ai_speed_;
      data_per_beat.pop_front();
      // If all beats have been used, restart at beginning.
      if (data_per_beat.size() == 0) {
        spdlog::get(LOGGER)->info("AI audio-data done. Resetting... {}", players_.at(username)->data_per_beat().size());
        data_per_beat = players_.at(username)->data_per_beat();
        audio_start_time = std::chrono::steady_clock::now();
      }
      else 
        data_at_beat = data_per_beat.front();
    }
    
    // Move potential
    if (utils::GetElapsed(last_update, cur_time) > render_frequency) {
      std::unique_lock ul(mutex_players_);
      // Move potentials of all players.
      ai->MovePotential(ai_speed_);
      // Handle actiaved-neurons of all players.
      ai->HandleDef(ai_speed_);
      ul.unlock();
      HandlePlayersLost();
    }
  }
  spdlog::get(LOGGER)->info("Game::Thread_Ai: ended");
}

std::map<position_t, std::pair<std::string, int>> ServerGame::GetAndUpdatePotentials() {
  std::map<position_t, std::pair<std::string, int>> potential_per_pos;

  // 1: Swallow epsp potential if on same field as enemies ipsp.
  std::map<position_t, int> positions;
  for (const auto& player : players_) {
    for (const auto& it : player.second->GetIpspAtPosition())
      IpspSwallow(it.first, player.second, player.second->enemies());
  }

  // 2: Create map of potentials in stacked format.
  for (const auto& it : players_) {
    // Add epsp first
    for (const auto& jt : it.second->GetEpspAtPosition()) {
      std::string symbol = utils::CharToString('a', jt.second-1);
      if (symbol > "z") {
        spdlog::get(LOGGER)->error("Symbol to great: {}", symbol);
        symbol = "z";
      }
      // Always add, if field is not jet occupied.
      if (potential_per_pos.count(jt.first) == 0)
        potential_per_pos[jt.first] = {symbol, it.second->color()};
      // Only overwride entry of other player, if this players units are more
      else if (potential_per_pos[jt.first].first < symbol)
        potential_per_pos[jt.first] = {symbol, it.second->color()};
    }
    // Ipsp always dominates epsp
    for (const auto& jt : it.second->GetIpspAtPosition()) {
      std::string symbol = utils::CharToString('1', jt.second-1);
      if (symbol > "9") {
        spdlog::get(LOGGER)->error("Symbol to great: {}", symbol);
        symbol = "9";
      }
      if (potential_per_pos.count(jt.first) == 0)
        potential_per_pos[jt.first] = {symbol, it.second->color()};
      // Always overwride epsps (digits)
      else if (std::isdigit(potential_per_pos[jt.first].first.front()))
        potential_per_pos[jt.first] = {symbol, it.second->color()};
      // Only overwride entry of other player, if this players units are more
      else if (potential_per_pos[jt.first].first < symbol)
        potential_per_pos[jt.first] = {symbol, it.second->color()};
    }
  }

  // Build full map:
  return potential_per_pos;
}

void ServerGame::CreateAndSendTransferToAllPlayers(float audio_played, bool update) {
  spdlog::get(LOGGER)->debug("ServerGame::CreateAndSendTransferToAllPlaters: sending? {}", ws_server_ != nullptr);

  auto updated_potentials = GetAndUpdatePotentials();
  if (ws_server_ == nullptr) {
    spdlog::get(LOGGER)->debug("ServerGame::CreateAndSendTransferToAllPlaters: ommitted");
    return;
  }

  // Create player agnostic transfer-data
  std::map<std::string, std::pair<std::string, int>> players_status;
  std::vector<Player*> vec_players;
  std::map<position_t, int> new_dead_neurons;
  for (const auto& it : players_) {
    players_status[it.first] = {it.second->GetNucleusLive(), it.second->color()};
    vec_players.push_back(it.second);
    for (const auto& it : it.second->new_dead_neurons())
      new_dead_neurons[it.first]= it.second;
  }
  Transfer transfer;
  transfer.set_players(players_status);
  transfer.set_new_dead_neurons(new_dead_neurons);
  transfer.set_audio_played(audio_played);
  
  // Set data for game update (only potentials)
  if (update)
    transfer.set_potentials(updated_potentials);
  // Set data for inital setup (full field and all graph-positions)
  else {
    transfer.set_field(field_->Export(vec_players));
    transfer.set_graph_positions(field_->GraphPositions());
  }

  // Add player-specific transfer-data (resources/ technologies) and send data to player.
  nlohmann::json resp = {{"command", (update) ? "update_game" : "init_game"}, {"data", nlohmann::json()}};
  for (const auto& it : human_players_) {
    transfer.set_resources(it.second->t_resources());
    transfer.set_technologies(it.second->t_technologies());
    transfer.set_build_options(it.second->GetBuildingOptions());
    transfer.set_synapse_options(it.second->GetSynapseOptions());
    resp["data"] = transfer.json();
    ws_server_->SendMessage(it.first, resp.dump());
  }
  // Send data to all observers.
  resp["data"] = transfer.json();
  for (const auto& it : observers_)
    ws_server_->SendMessage(it, resp.dump());
  // Send all new neurons to obersers.
  SendNeuronsToObservers();
}

void ServerGame::HandlePlayersLost() {
  std::unique_lock ul(mutex_players_);
  // Check if new players have lost.
  for (const auto& it : players_) {
    if (it.second->HasLost() && dead_players_.count(it.first) == 0) {
      dead_players_.insert(it.first);
      // Send message if not AI.
      if (!IsAi(it.first) && ws_server_) {
        nlohmann::json resp = {{"command", "game_end"}, {"data", {{"msg", "YOU LOST"}} }};
        ws_server_->SendMessage(it.first, resp.dump());
      }
    }
  }
  // If all but one players have one:
  if (dead_players_.size() == players_.size()-1) {
    nlohmann::json resp = {{"command", "game_end"}, {"data", {{"msg", ""}} }};
    for (const auto& it : players_) {
      if (dead_players_.count(it.first) == 0) {
        resp["data"]["msg"] = it.first + " WON";
        std::cout << it.first << " won." << std::endl;
        // If not AI, send message.
        if (!IsAi(it.first) && ws_server_)
          ws_server_->SendMessage(it.first, resp.dump());
      }
    }
    // Also inform all obersers.
    for (const auto& it : observers_) 
      ws_server_->SendMessage(it, resp.dump());
    // Finally end game.
    std::unique_lock ul(mutex_status_);
    status_ = CLOSING;
  }
}

void ServerGame::SendScoutedNeurons() {
  std::shared_lock sl(mutex_players_);
  for (const auto& it : human_players_) {
    for (const auto& potential : it.second->GetPotentialPositions()) {
      for (const auto& enemy : it.second->enemies()) {
        for (const auto& nucleus : enemy->GetAllPositionsOfNeurons(NUCLEUS)) {
          if (utils::Dist(potential, nucleus) < enemy->cur_range()) {
            nlohmann::json resp = {{"command", "set_units"}, {"data", {{"neurons", 
              enemy->GetAllNeuronsInRange(nucleus)}, {"color", enemy->color()}} }};
            ws_server_->SendMessage(it.first, resp.dump());
          }
        }
      }
    }
  }
}

void ServerGame::SendNeuronsToObservers() {
  if (observers_.size() == 0)
    return;
  // Iterate through (playing) players and send all new neurons to obersers.
  for (const auto& it : players_) {
    for (const auto& enemy : it.second->enemies()) {
      nlohmann::json resp = {{"command", "set_units"}, {"data", {{"neurons", 
        enemy->new_neurons()}, {"color", enemy->color()}} }};
      for (const auto& it : observers_)
        ws_server_->SendMessage(it, resp.dump());
    }
  }
}

void ServerGame::SendMessageToAllPlayers(std::string msg, std::string ignore_username) {
  spdlog::get(LOGGER)->debug("ServerGame::SendMessageToAllPlayers: num human players: {}", human_players_.size());
  for (const auto& it : human_players_) {
    if (ignore_username == "" || it.first != ignore_username)
      ws_server_->SendMessage(it.first, msg);
  }
}

void ServerGame::IpspSwallow(position_t ipsp_pos, Player* player, std::vector<Player*> enemies) {
  std::string ipsp_id = player->GetPotentialIdIfPotential(ipsp_pos, IPSP);
  for (const auto& enemy : enemies) {
    std::string id = enemy->GetPotentialIdIfPotential(ipsp_pos);
    if (id.find("epsp") != std::string::npos) {
      player->NeutralizePotential(ipsp_id, -1); // increase potential by one
      enemy->NeutralizePotential(id, 1); // decrease potential by one
      player->statistics().AddEpspSwallowed();
      spdlog::get(LOGGER)->info("IPSP at {} swallowed esps", utils::PositionToString(ipsp_pos));
    }
  }
}

std::string ServerGame::GetMissingResourceStr(Costs missing_costs) {
  std::string res = "";
  if (missing_costs.size() == 0)
    return res;
  for (const auto& it : missing_costs)
    res += "Missing " + std::to_string(it.second) + " " + resources_name_mapping.at(it.first) + "! ";
  return res;
}
