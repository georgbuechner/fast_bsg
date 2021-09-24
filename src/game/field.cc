#include <bits/types/FILE.h>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <curses.h>
#include <exception>
#include <iostream>
#include <locale>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "game/field.h"
#include "objects/units.h"
#include "player/player.h"
#include "spdlog/spdlog.h"
#include "utils/utils.h"
#include "constants/codes.h"

#define COLOR_DEFAULT 3
#define COLOR_PLAYER 2 
#define COLOR_KI 1 
#define COLOR_RESOURCES 4 
#define COLOR_OK 5 
#define COLOR_HIGHLIGHT 6 

#define SECTIONS 8


Field::Field(int lines, int cols, Audio* audio) {
  lines_ = lines;
  cols_ = cols;
  audio_ = audio;

  // initialize empty field.
  for (int l=0; l<=lines_; l++) {
    field_.push_back({});
    for (int c=0; c<=cols_; c++)
      field_[l].push_back(SYMBOL_FREE);
  }

  highlight_ = {};
  range_ = ViewRange::HIDE;
}

// getter
int Field::lines() { 
  return lines_; 
}
int Field::cols() { 
  return cols_; 
}
std::vector<Position> Field::highlight() {
  return highlight_;
}

// setter:
void Field::set_highlight(std::vector<Position> positions) {
  highlight_ = positions;
}
void Field::set_range(int range) {
  range_ = range;
}
void Field::set_range_center(Position pos) {
  range_center_ = pos;
}
void Field::set_replace(std::map<Position, char> replacements) {
  replacements_ = replacements;
}

Position Field::AddNucleus(int section) {
  spdlog::get(LOGGER)->debug("Field::AddNucleus");
  auto positions_in_section = GetAllPositionsOfSection(section);
  Position pos = positions_in_section[getrandom_int(0, positions_in_section.size())];
  field_[pos.first][pos.second] = SYMBOL_DEN;
  // Mark positions surrounding nucleus as free:
  auto positions_arround_nucleus = GetAllInRange({pos.first, pos.second}, 1.5, 1);
  for (const auto& it : positions_arround_nucleus)
    field_[it.first][it.second] = SYMBOL_FREE;
  AddResources(pos);
  spdlog::get(LOGGER)->debug("Field::AddNucleus: done");
  return pos;
}

void Field::AddResources(Position start_pos) {
  spdlog::get(LOGGER)->debug("Field::AddResources");
  std::vector<Position> positions = GetAllInRange(start_pos, 4, 2);
  std::vector<std::string> symbols = {SYMBOL_POTASSIUM, SYMBOL_CHLORIDE, SYMBOL_GLUTAMATE, SYMBOL_SEROTONIN, 
    SYMBOL_DOPAMINE};
  for (const auto& symbol : symbols) {
    Position pos = positions[getrandom_int(0, positions.size()-1)];
    while (!IsFree(pos)) {
      pos = positions[getrandom_int(0, positions.size()-1)];
    }
    field_[pos.first][pos.second] = symbol;
  }
  /*
  field_[positions[0].first][positions[0]] = SYMBOL_POTASSIUM;
  pos = FindFree(start_pos.first, start_pos.second, 2, 4);
  field_[pos.first][pos.second] = SYMBOL_CHLORIDE;
  pos = FindFree(start_pos.first, start_pos.second, 2, 4);
  field_[pos.first][pos.second] = SYMBOL_GLUTAMATE;
  pos = FindFree(start_pos.first, start_pos.second, 2, 4);
  field_[pos.first][pos.second] = SYMBOL_DOPAMINE;
  pos = FindFree(start_pos.first, start_pos.second, 2, 4);
  field_[pos.first][pos.second] = SYMBOL_SEROTONIN;
  */
  spdlog::get(LOGGER)->debug("Field::AddResources: done");
}

void Field::BuildGraph(Position player_den, Position enemy_den) {
  // Add all nodes.
  for (int l=0; l<lines_; l++) {
    for (int c=0; c<cols_; c++) {
      if (field_[l][c] != SYMBOL_HILL)
        graph_.AddNode(l, c);
    }
  }

  // For each node, add edges.
  for (auto node : graph_.nodes()) {
    std::vector<Position> neighbors = GetAllInRange({node.second->line_, node.second->col_}, 1.5, 1);
    for (const auto& pos : neighbors) {
      if (InField(pos.first, pos.second) && field_[pos.first][pos.second] != SYMBOL_HILL 
          && graph_.InGraph(pos))
      graph_.AddEdge(node.second, graph_.nodes().at(pos));
    }
  }

  // Remove all nodes not in main circle
  graph_.RemoveInvalid(player_den);
  if (graph_.nodes().count(enemy_den) == 0)
    throw "Invalid world.";
}

void Field::AddHills() {
  spdlog::get(LOGGER)->debug("Field::AddHills");
  int num_hils = (lines_ + cols_) * 2;
  // Generate lines*2 mountains.
  for (int i=0; i<num_hils; i++) {
    // Generate random hill.
    int start_y = getrandom_int(0, lines_);
    int start_x = getrandom_int(0, cols_);
    field_[start_y][start_x] = SYMBOL_HILL;

    // Generate random 5 hills around this hill.
    for (int j=1; j<=5; j++) {
      int y = GetXInRange(random_coordinate_shift(start_y, 0, j), 0, lines_);
      int x = GetXInRange(random_coordinate_shift(start_x, 0, j), 0, cols_);
      field_[y][x] = SYMBOL_HILL;
    }
  }
  spdlog::get(LOGGER)->debug("Field::AddHills: done");
}

Position Field::GetNewSoldierPos(Position pos) {
  auto new_pos = FindFree(pos.first, pos.second, 1, 3);
  while(!graph_.InGraph(new_pos))
    new_pos = FindFree(pos.first, pos.second, 1, 3);
  return new_pos;
}

std::list<Position> Field::GetWayForSoldier(Position start_pos, std::vector<Position> way_points) {
  Position target_pos = way_points.back();
  way_points.pop_back();
  std::list<Position> way = {start_pos};
  // If there are way_points left, sort way-points by distance, then create way.
  if (way_points.size() > 0) {
    std::map<int, Position> sorted_way;
    for (const auto& it : way_points) 
      sorted_way[lines_+cols_-utils::dist(it, target_pos)] = it;
    for (const auto& it : sorted_way) {
      auto new_part = graph_.find_way(way.back(), it.second);
      way.pop_back();
      way.insert(way.end(), new_part.begin(), new_part.end());
    }
  }
  // Create way from last position to target.
  auto new_part = graph_.find_way(way.back(), target_pos);
  way.pop_back();
  way.insert(way.end(), new_part.begin(), new_part.end());
  return way;
}

void Field::AddNewUnitToPos(Position pos, int unit) {
  std::unique_lock ul_field(mutex_field_);
  if (unit == UnitsTech::ACTIVATEDNEURON)
    field_[pos.first][pos.second] = SYMBOL_DEF;
  else if (unit == UnitsTech::SYNAPSE)
    field_[pos.first][pos.second] = SYMBOL_BARACK;
  else if (unit == UnitsTech::NUCLEUS)
    field_[pos.first][pos.second] = SYMBOL_DEN;
}

void Field::UpdateField(Player *player, std::vector<std::vector<std::string>>& field) {
  // Accumulate all ipsps and epsps at their current positions.
  // Accumulate epsps with start symbol '0' and ipsps with start symbol 'a'.
  std::map<Position, std::map<char, int>> potentials_at_position;
  for (const auto& it : player->potential()) {
    if (it.second.type_ == UnitsTech::EPSP) 
      potentials_at_position[it.second.pos_]['0']++;
    else if (it.second.type_ == UnitsTech::IPSP)
      potentials_at_position[it.second.pos_]['a']++;
  }

  // Add ipsps with increasing letter-count (add ipsps first to prioritise epsps).
  for (const auto& pos : potentials_at_position) {
    int l = pos.first.first;
    int c = pos.first.second;
    // For each type (epsp/ ipsp) create add number of potentials (epsp: 1,2,..;
    // ipsps: a,b,..) to field, if position is free. Add infinity symbol if more
    // than 10 potentials of a kind on one field.
    for (const auto& potential : pos.second) {
      if (field[l][c] == SYMBOL_FREE && potential.second <= 10)
        field[l][c] = potential.first + potential.second;
      else if (field[l][c] == SYMBOL_FREE && potential.second > 10)
        field[l][c] = ":";
    }
  }
}

bool Field::CheckCollidingPotentials(Position pos, Player* player_one, Player* player_two) {
  std::string id_one = player_one->GetPotentialIdIfPotential(pos);
  std::string id_two = player_two->GetPotentialIdIfPotential(pos);
  // Not colliding potentials as at at least one position there is no potential.
  if (id_one == "" || id_two == "")
    return false;

  if (id_one.find("epsp") != std::string::npos && id_two.find("ipsp") != std::string::npos) {
    spdlog::get(LOGGER)->debug("Field::CheckCollidingPotentials: calling neutralize potential 1");
    player_one->NeutralizePotential(id_one, 1);
    spdlog::get(LOGGER)->debug("Field::CheckCollidingPotentials: calling neutralize potential 2");
    player_two->NeutralizePotential(id_two, -1); // -1 increase potential.
  }
  else if (id_one.find("ipsp") != std::string::npos && id_two.find("epsp") != std::string::npos) {
    spdlog::get(LOGGER)->debug("Field::CheckCollidingPotentials: calling neutralize potential 1");
    player_one->NeutralizePotential(id_one, -1);
    spdlog::get(LOGGER)->debug("Field::CheckCollidingPotentials: calling neutralize potential 2");
    player_two->NeutralizePotential(id_two, 1); // -1 increase potential.
  }
  return true;
}

void Field::PrintField(Player* player, Player* enemy) {
  std::shared_lock sl_field(mutex_field_);
  auto field = field_;
  sl_field.unlock();

  UpdateField(player, field);
  UpdateField(enemy, field);

  for (int l=0; l<lines_; l++) {
    for (int c=0; c<cols_; c++) {
      Position cur = {l, c};
      // highlight -> magenta
      if (std::find(highlight_.begin(), highlight_.end(), cur) != highlight_.end())
        attron(COLOR_PAIR(COLOR_HIGHLIGHT));
      // IPSP is on enemy neuron -> cyan.
      else if (player->IsNeuronBlocked(cur) || enemy->IsNeuronBlocked(cur))
          attron(COLOR_PAIR(COLOR_RESOURCES));
      // both players -> cyan
      else if (CheckCollidingPotentials(cur, player, enemy))
        attron(COLOR_PAIR(COLOR_RESOURCES));
      // player 2 -> red
      else if (enemy->GetNeuronTypeAtPosition(cur) != -1 || enemy->GetPotentialIdIfPotential(cur) != "")
        attron(COLOR_PAIR(COLOR_PLAYER));
      // player 1 -> blue 
      else if (player->GetNeuronTypeAtPosition(cur) != -1 || player->GetPotentialIdIfPotential(cur) != "")
        attron(COLOR_PAIR(COLOR_KI));
      // resources -> cyan
      else if (resources_symbol_mapping.count(field[l][c]) > 0) {
        int dist_player = utils::dist(cur, player->nucleus_pos());
        int dist_enemy = utils::dist(cur, enemy->nucleus_pos());
        if ((dist_player < dist_enemy 
            && player->IsActivatedResource(resources_symbol_mapping.at(field[l][c])) )
            || (dist_enemy < dist_player
            && enemy->IsActivatedResource(resources_symbol_mapping.at(field[l][c]))))
         attron(COLOR_PAIR(COLOR_RESOURCES));
      }
      // range -> green
      else if (InRange(cur, range_, range_center_) && player->GetNeuronTypeAtPosition(cur) != UnitsTech::NUCLEUS)
        attron(COLOR_PAIR(COLOR_OK));
      
      // Replace certain elements.
      if (replacements_.count(cur) > 0)
        mvaddch(10+l, 10+2*c, replacements_.at(cur));
      else {
        mvaddstr(10+l, 10+2*c, field[l][c].c_str());
      }
      mvaddch(10+l, 10+2*c+1, ' ' );
      attron(COLOR_PAIR(COLOR_DEFAULT));
    }
  }
}

bool Field::InRange(Position pos, int range, Position start) {
  if (range == ViewRange::GRAPH)
    return graph_.InGraph(pos);
  return utils::dist(pos, start) <= range;
}

Position Field::GetSelected(char replace, int num) {
  int counter = int('a')-1;
  for (int l=0; l<lines_; l++) {
    for (int c=0; c<cols_; c++) {
      if (field_[l][c].front() == replace)
        counter++;
      if (counter == num)
        return {l, c};
    }
  }
  return {-1, -1};
}

bool Field::InField(int l, int c) {
  return (l >= 0 && l <= lines_ && c >= 0 && c <= cols_);
}

Position Field::FindFree(int l, int c, int min, int max) {
  std::shared_lock sl_field(mutex_field_);

  auto positions = GetAllInRange({l, c}, max, min, true);
  if (positions.size() == 0)
    throw std::runtime_error("Game came to an strange end. No free positions!");

  return positions[getrandom_int(0, positions.size())];
}

bool Field::IsFree(Position pos) {
  return field_[pos.first][pos.second] == SYMBOL_FREE;
}

int Field::GetXInRange(int x, int min, int max) {
  if (x < min) 
    return min;
  if (x > max)
    return max;
  return x;
}

int Field::random_coordinate_shift(int x, int min, int max) {
  // Determine decrease or increase of values.
  int plus_minus = getrandom_int(0, 1);
  int random_faktor = getrandom_int(min, max);
  return x + pow(-1, plus_minus)*random_faktor;
}


std::vector<Position> Field::GetAllInRange(Position start, double max_dist, double min_dist, bool free) {
  std::vector<Position> positions_in_range;
  Position upper_corner = {start.first-max_dist, start.second-max_dist};
  for (int i=0; i<=max_dist*2; i++) {
    for (int j=0; j<=max_dist*2; j++) {
      Position pos = {upper_corner.first+i, upper_corner.second+j};
      if (InField(pos.first, pos.second) && utils::InRange(start, pos, min_dist, max_dist)
          && (!free || field_[pos.first][pos.second] == SYMBOL_FREE)
          && (!free || graph_.InGraph(pos)))
        positions_in_range.push_back(pos);
    }
  }
  return positions_in_range;
}

std::vector<Position> Field::GetAllCenterPositionsOfSections() {
  std::vector<Position> positions;
  for (int i=1; i<=SECTIONS; i++) {
    int l = (i-1)%(SECTIONS/2)*(cols_/4);
    int c = (i < (SECTIONS/2)+1) ? 0 : lines_/2;
    positions.push_back({(c+c+lines_/2)/2, (l+l+cols_/4)/2});
  }
  return positions;
}

std::vector<Position> Field::GetAllPositionsOfSection(unsigned int interval) {
  std::vector<Position> positions;
  int l = (interval-1)%(SECTIONS/2)*(cols_/4);
  int c = (interval < (SECTIONS/2)+1) ? 0 : lines_/2;
  for (int i=l; i<l+cols_/4; i++) {
    for (int j=c; j<c+lines_/2; j++)
      positions.push_back({j, i});
  }
  return positions;
}

int Field::getrandom_int(int min, int max) {
  if (audio_) 
    return audio_->RandomInt(min, max);
  return utils::getrandom_int(min, max);
}