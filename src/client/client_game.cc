#include "client/client_game.h"
#include "client/client.h"
#include "client/context.h"
#include "share/eventmanager.h"
#include "constants/codes.h"
#include "constants/texts.h"
#include "curses.h"
#include "nlohmann/json_fwd.hpp"
#include "objects/transfer.h"
#include "print/drawrer.h"
#include "spdlog/spdlog.h"
#include "utils/utils.h"
#include <unistd.h>
#include <vector>

#define CONTEXT_FIELD 0
#define CONTEXT_RESOURCES 1
#define CONTEXT_TECHNOLOGIES 2
#define CONTEXT_RESOURCES_MSG "Distribute (+)/ remove (-) iron to handler resource-gain"
#define CONTEXT_TECHNOLOGIES_MSG "Research technology by pressing [enter]"

ClientGame::ClientGame(bool relative_size, std::string base_path, std::string username, bool mp) 
    : username_(username), muliplayer_availible_(mp), base_path_(base_path), render_pause_(false), drawrer_() {
  status_ = WAITING;
  // Initialize curses
  setlocale(LC_ALL, "");
  initscr();
  cbreak();
  noecho();
  curs_set(0);
  keypad(stdscr, true);
  clear();
  
  // Initialize colors.
  use_default_colors();
  start_color();
  init_pair(COLOR_AVAILIBLE, COLOR_BLUE, -1);
  init_pair(COLOR_ERROR, COLOR_RED, -1);
  init_pair(COLOR_DEFAULT, -1, -1);
  init_pair(COLOR_MSG, COLOR_CYAN, -1);
  init_pair(COLOR_SUCCESS, COLOR_GREEN, -1);
  init_pair(COLOR_MARKED, COLOR_MAGENTA, -1);

  // Setup map-size
  drawrer_.SetUpBorders(LINES, COLS);

  // Set-up audio-paths.
  std::vector<std::string> paths = utils::LoadJsonFromDisc(base_path_ + "/settings/music_paths.json");
  for (const auto& it : paths) {
    if (it.find("$(HOME)") != std::string::npos)
      audio_paths_.push_back(getenv("HOME") + it.substr(it.find("/")));
    else if (it.find("$(DISSONANCE)") != std::string::npos)
      audio_paths_.push_back(base_path_ + it.substr(it.find("/")));
    else
      audio_paths_.push_back(it);
  }

  // Initialize contexts
  current_context_ = CONTEXT_RESOURCES;
  // Basic handlers shared by standard-contexts
  std::map<char, void(ClientGame::*)(int)> std_handlers = { {'j', &ClientGame::h_MoveSelectionUp}, 
    {'k', &ClientGame::h_MoveSelectionDown}, {'t', &ClientGame::h_ChangeViewPoint}, {'q', &ClientGame::h_Quit} };
  // Resource context:
  contexts_[CONTEXT_RESOURCES] = Context(CONTEXT_RESOURCES_MSG, std_handlers, {{'+', &ClientGame::h_AddIron}, 
      {'-', &ClientGame::h_RemoveIron}});
  // Technology context:
  contexts_[CONTEXT_TECHNOLOGIES] = Context(CONTEXT_TECHNOLOGIES_MSG, std_handlers, {{'\n', &ClientGame::h_AddTech}});

  // Initialize eventmanager.
  eventmanager_.AddHandler("select_mode", &ClientGame::m_SelectMode);
  eventmanager_.AddHandler("select_audio", &ClientGame::m_SelectAudio);
  eventmanager_.AddHandler("print_msg", &ClientGame::m_PrintMsg);
  eventmanager_.AddHandler("print_field", &ClientGame::m_PrintField);
  eventmanager_.AddHandler("set_msg", &ClientGame::m_SetMsg);
  eventmanager_.AddHandler("game_start", &ClientGame::m_GameStart);
  eventmanager_.AddHandler("game_end", &ClientGame::m_GameEnd);
}

nlohmann::json ClientGame::HandleAction(nlohmann::json msg) {
  std::string command = msg["command"];
  spdlog::get(LOGGER)->debug("ClientGame::HandleAction: {}, {}", command, msg["data"].dump());

  if (eventmanager_.handlers().count(command))
    (this->*eventmanager_.handlers().at(command))(msg);
  else 
    msg = nlohmann::json();
  
  spdlog::get(LOGGER)->debug("ClientGame::HandleAction: response {}", msg.dump());
  return msg;
}


void ClientGame::GetAction() {
  spdlog::get(LOGGER)->info("ClientGame::GetAction.");

  while(true) {
    // Get Input
    if (status_ == WAITING) continue; // Skip as long as not active.
    if (status_ == CLOSING) break; // Leave thread.
    char choice = getch();
    spdlog::get(LOGGER)->info("ClientGame::GetAction action_ {}, in: {}", status_, choice);
    if (status_ == WAITING) continue; // Skip as long as not active. 
    if (status_ == CLOSING) break; // Leave thread.

    // Throw event
    if (contexts_.at(current_context_).eventmanager().handlers().count(choice) > 0) {
      spdlog::get(LOGGER)->debug("ClientGame::GetAction: calling handler.");
      spdlog::get(LOGGER)->flush();
      (this->*contexts_.at(current_context_).eventmanager().handlers().at(choice))(0);
    }
    // If event not in context-event-manager print availible options.
    else {
      spdlog::get(LOGGER)->debug("ClientGame::GetAction: invalid action for this context.");
      spdlog::get(LOGGER)->flush();
      drawrer_.set_msg("Invalid option. Availible: " + contexts_.at(current_context_).eventmanager().options());
    }

    // Refresh field (only side-column)
    drawrer_.PrintGame(false, true); 
  }

  // Send server message to close game.
  nlohmann::json response = {{"command", "close"}, {"username", username_}, {"data", nlohmann::json()}};
  ws_srv_->SendMessage(response.dump());

  // Wrap up.
  refresh();
  clear();
  endwin();
}

void ClientGame::h_Quit(int) {
  drawrer_.ClearField();
  drawrer_.set_stop_render(true);
  drawrer_.PrintCenteredLine(LINES/2, "Are you sure you want to quit? (y/n/)");
  char choice = getch();
  if (choice == 'y') {
    status_ = CLOSING;
    nlohmann::json msg = {{"command", "resign"}, {"username", username_}, {"data", nlohmann::json()}};
    ws_srv_->SendMessage(msg.dump());
  }
  else {
    drawrer_.set_stop_render(false);
  }
}
void ClientGame::h_MoveSelectionUp(int) {
  drawrer_.inc_cur_sidebar_elem(1);
}

void ClientGame::h_MoveSelectionDown(int) {
  drawrer_.inc_cur_sidebar_elem(-1);
}

void ClientGame::h_ChangeViewPoint(int) {
  current_context_ = drawrer_.next_viewpoint();
  drawrer_.set_msg(contexts_.at(current_context_).msg());
}

void ClientGame::h_AddIron(int) {
  int resource = drawrer_.GetResource();
  nlohmann::json response = {{"command", "add_iron"}, {"username", username_}, {"data", 
    {{"resource", resource}} }};
  ws_srv_->SendMessage(response.dump());
}

void ClientGame::h_RemoveIron(int) {
  int resource = drawrer_.GetResource();
  nlohmann::json response = {{"command", "remove_iron"}, {"username", username_}, {"data", 
    {{"resource", resource}} }};
  ws_srv_->SendMessage(response.dump());
}

void ClientGame::h_AddTech(int) {
  int technology = drawrer_.GetTech();
  nlohmann::json response = {{"command", "add_technology"}, {"username", username_}, {"data", 
    {{"technology", technology}} }};
  ws_srv_->SendMessage(response.dump());
}

void ClientGame::m_SelectMode(nlohmann::json& msg) {
  spdlog::get(LOGGER)->debug("ClientGame::m_SelectMode: {}", msg.dump());
  // Print welcome text.
  drawrer_.PrintCenteredParagraphs(texts::welcome);
  
  // Select single-player, mulit-player (host/ client), observer.
  choice_mapping_t mapping = {
    {SINGLE_PLAYER, {"singe-player", COLOR_AVAILIBLE}}, 
    {MULTI_PLAYER, {"muli-player (host)", (muliplayer_availible_) ? COLOR_AVAILIBLE : COLOR_DEFAULT}}, 
    {MULTI_PLAYER_CLIENT, {"muli-player (client)", (muliplayer_availible_) ? COLOR_AVAILIBLE : COLOR_DEFAULT}}, 
    {OBSERVER, {"watch ki", COLOR_DEFAULT}}
  };
  // Update msg
  msg["command"] = "init_game";
  msg["data"] = {{"lines", drawrer_.field_height()}, {"cols", drawrer_.field_width()}, {"base_path", base_path_},
    {"num_players", 2}};
  msg["data"]["mode"] = SelectInteger("Select mode", true, mapping, {mapping.size()+1}, "Mode not available");
}

void ClientGame::m_SelectAudio(nlohmann::json& msg) {
  msg["command"] = "initialize_game";
  msg["data"]["source_path"] = SelectAudio();
}

void ClientGame::m_PrintMsg(nlohmann::json& msg) {
  drawrer_.ClearField();
  drawrer_.PrintCenteredLine(LINES/2, msg["data"]["msg"]);
  refresh();
  msg = nlohmann::json();
}

void ClientGame::m_PrintField(nlohmann::json& msg) {
  drawrer_.set_transfter(msg["data"]);
  drawrer_.PrintGame(false, false);
  msg = nlohmann::json();
}

void ClientGame::m_SetMsg(nlohmann::json& msg) {
  drawrer_.set_msg(msg["data"]["msg"]);
  msg = nlohmann::json();
}

void ClientGame::m_GameStart(nlohmann::json& msg) {
  status_ = RUNNING;
  drawrer_.set_msg(contexts_.at(current_context_).msg());
  msg = nlohmann::json();
}

void ClientGame::m_GameEnd(nlohmann::json& msg) {
  status_ = CLOSING;
  drawrer_.ClearField();
  drawrer_.PrintCenteredLine(LINES/2, msg["data"]["msg"]);
  getch();
  msg = nlohmann::json();
}

// Selection methods

int ClientGame::SelectInteger(std::string msg, bool omit, choice_mapping_t& mapping, std::vector<size_t> splits,
    std::string error_msg) {
  drawrer_.ClearField();
  bool end = false;

  std::vector<std::pair<std::string, int>> options;
  for (const auto& option : mapping) {
    options.push_back({utils::CharToString('a', option.first) + ": " + option.second.first + "    ", 
        option.second.second});
  }
  
  // Print matching the splits.
  int counter = 0;
  int last_split = 0;
  for (const auto& split : splits) {
    std::vector<std::pair<std::string, int>> option_part; 
    for (unsigned int i=last_split; i<split && i<options.size(); i++)
      option_part.push_back(options[i]);
    drawrer_.PrintCenteredLineColored(LINES/2+(counter+=2), option_part);
    last_split = split;
  }
  drawrer_.PrintCenteredLine(LINES/2-1, msg);
  drawrer_.PrintCenteredLine(LINES/2+counter+3, "> enter number...");

  while (!end) {
    // Get choice.
    char choice = getch();
    int int_choice = choice-'a';
    if (choice == 'q' && omit)
      end = true;
    else if (mapping.count(int_choice) > 0 && (mapping.at(int_choice).second == COLOR_AVAILIBLE || !omit)) {
      // TODO(fux): see above  `pause_ = false;`
      return int_choice;
    }
    else if (mapping.count(int_choice) > 0 && mapping.at(int_choice).second != COLOR_AVAILIBLE && omit)
      drawrer_.PrintCenteredLine(LINES/2+counter+5, "Selection not available (" + error_msg + "): " 
          + std::to_string(int_choice));
    else 
      drawrer_.PrintCenteredLine(LINES/2+counter+5, "Wrong selection: " + std::to_string(int_choice));
  }
  // TODO(fux): see above  `pause_ = false;`
  return -1;
}

std::string ClientGame::SelectAudio() {
  drawrer_.ClearField();

  // Create selector and define some variables
  AudioSelector selector = SetupAudioSelector("", "select audio", audio_paths_);
  selector.options_.push_back({"dissonance_recently_played", "recently played"});
  std::vector<std::string> recently_played = utils::LoadJsonFromDisc(base_path_ + "/settings/recently_played.json");
  std::string error = "";
  std::string help = "(use + to add paths, ENTER to select,  h/l or ←/→ to change directory "
    "and j/k or ↓/↑ to circle through songs,)";
  unsigned int selected = 0;
  int level = 0;
  unsigned int print_start = 0;
  unsigned int max = LINES/2;
  std::vector<std::pair<std::string, std::string>> visible_options;

  while(true) {
    unsigned int print_max = std::min((unsigned int)selector.options_.size(), max);
    visible_options = utils::SliceVector(selector.options_, print_start, print_max);
    
    drawrer_.PrintCenteredLine(10, utils::ToUpper(selector.title_));
    drawrer_.PrintCenteredLine(11, selector.path_);
    drawrer_.PrintCenteredLine(12, help);

    attron(COLOR_PAIR(COLOR_ERROR));
    drawrer_.PrintCenteredLine(13, error);
    error = "";
    attron(COLOR_PAIR(COLOR_DEFAULT));

    for (unsigned int i=0; i<visible_options.size(); i++) {
      if (i == selected)
        attron(COLOR_PAIR(COLOR_MARKED));
      drawrer_.PrintCenteredLine(15 + i, visible_options[i].second);
      attron(COLOR_PAIR(COLOR_DEFAULT));
    }

    // Get players choice.
    char choice = getch();
    if (utils::IsRight(choice)) {
      level++;
      if (visible_options[selected].first == "dissonance_recently_played")
        selector = SetupAudioSelector("", "Recently Played", recently_played);
      else if (std::filesystem::is_directory(visible_options[selected].first)) {
        selector = SetupAudioSelector(visible_options[selected].first, visible_options[selected].second, 
            utils::GetAllPathsInDirectory(visible_options[selected].first));
        selected = 0;
        print_start = 0;
      }
      else 
        error = "Not a directory!";
    }
    else if (utils::IsLeft(choice)) {
      if (level == 0) 
        error = "No parent directory.";
      else {
        level--;
        selected = 0;
        print_start = 0;
        if (level == 0) {
          selector = SetupAudioSelector("", "select audio", audio_paths_);
          selector.options_.push_back({"dissonance_recently_played", "recently played"});
        }
        else {
          std::filesystem::path p = selector.path_;
          selector = SetupAudioSelector(p.parent_path().string(), p.parent_path().filename().string(), 
              utils::GetAllPathsInDirectory(p.parent_path()));
        }
      }
    }
    else if (utils::IsDown(choice)) {
      if (selected == print_max-1 && selector.options_.size() > max)
        print_start++;
      else 
        selected = utils::Mod(selected+1, visible_options.size());
    }
    else if (utils::IsUp(choice)) {
      if (selected == 0 && print_start > 0)
        print_start--;
      else 
        selected = utils::Mod(selected-1, print_max);
    }
    else if (std::to_string(choice) == "10") {
      std::filesystem::path select_path = visible_options[selected].first;
      if (select_path.filename().extension() == ".mp3" || select_path.filename().extension() == ".wav")
        break;
      else 
        error = "Wrong file type. Select mp3 or wav";
    }
    else if (choice == '+') {
      std::string input = InputString("Absolute path: ");
      if (std::filesystem::exists(input)) {
        if (input.back() == '/')
          input.pop_back();
        audio_paths_.push_back(input);
        nlohmann::json audio_paths = utils::LoadJsonFromDisc(base_path_ + "/settings/music_paths.json");
        audio_paths.push_back(input);
        utils::WriteJsonFromDisc(base_path_ + "/settings/music_paths.json", audio_paths);
        selector = SetupAudioSelector("", "select audio", audio_paths_);
      }
      else {
        error = "Path does not exist.";
      }
    }
    drawrer_.ClearField();
  }

  // Add selected aubio-file to recently-played files.
  std::filesystem::path select_path = selector.options_[selected].first;
  bool exists=false;
  for (const auto& it : recently_played) {
    if (it == select_path.string()) {
      exists = true;
      break;
    }
  }
  if (!exists)
    recently_played.push_back(select_path.string());
  if (recently_played.size() > 10) 
    recently_played.erase(recently_played.begin());
  nlohmann::json j_recently_played = recently_played;
  utils::WriteJsonFromDisc(base_path_ + "/settings/recently_played.json", j_recently_played);

  // Return selected path.
  return select_path.string(); 
}

ClientGame::AudioSelector ClientGame::SetupAudioSelector(std::string path, std::string title, 
    std::vector<std::string> paths) {
  std::vector<std::pair<std::string, std::string>> options;
  for (const auto& it : paths) {
    std::filesystem::path path = it;
    if (path.extension() == ".mp3" || path.extension() == ".wav" || std::filesystem::is_directory(path))
      options.push_back({it, path.filename()});
  }
  return AudioSelector({path, title, options});
}

std::string ClientGame::InputString(std::string instruction) {
  drawrer_.ClearField();
  drawrer_.PrintCenteredLine(LINES/2, instruction.c_str());
  echo();
  curs_set(1);
  std::string input;
  int ch = getch();
  while (ch != '\n') {
    input.push_back(ch);
    ch = getch();
  }
  noecho();
  curs_set(0);
  return input;
}