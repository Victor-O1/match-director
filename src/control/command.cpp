#include "md/control/command.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace md {

namespace {

using nlohmann::json;

CommandType type_from(std::string_view s) {
  if (s == "goal") return CommandType::Goal;
  if (s == "score") return CommandType::SetScore;
  if (s == "teams") return CommandType::SetTeams;
  if (s == "lower_third") return CommandType::LowerThird;
  if (s == "clear_lower_third") return CommandType::ClearLowerThird;
  if (s == "replay") return CommandType::Replay;
  if (s == "kickoff") return CommandType::Kickoff;
  throw std::invalid_argument("unknown command type '" + std::string(s) + "'");
}

int team_from(const json& j) {
  const std::string team = j.value("team", "home");
  if (team == "home") return 0;
  if (team == "away") return 1;
  throw std::invalid_argument("team must be \"home\" or \"away\"");
}

std::string limited_text(const json& j, const char* key, std::size_t max_len) {
  std::string s = j.value(key, "");
  if (s.size() > max_len) {
    s.resize(max_len);
  }
  return s;
}

Command from_json(const json& j) {
  if (!j.is_object()) {
    throw std::invalid_argument("command must be a JSON object");
  }
  Command c;
  c.type = type_from(j.at("type").get<std::string>());
  c.t = j.value("t", -1.0);
  switch (c.type) {
    case CommandType::Goal:
      c.team = team_from(j);
      c.text = limited_text(j, "text", 40);
      c.seconds = j.value("replay_seconds", 7.0);
      break;
    case CommandType::SetScore:
      c.home_score = j.at("home").get<int>();
      c.away_score = j.at("away").get<int>();
      if (c.home_score < 0 || c.away_score < 0 || c.home_score > 99 || c.away_score > 99) {
        throw std::invalid_argument("scores must be 0..99");
      }
      break;
    case CommandType::SetTeams:
      c.text = limited_text(j, "home", 12);
      c.text2 = limited_text(j, "away", 12);
      break;
    case CommandType::LowerThird:
      c.text = limited_text(j, "text", 60);
      c.seconds = j.value("seconds", 6.0);
      break;
    case CommandType::Replay:
      c.seconds = j.value("seconds", 8.0);
      break;
    case CommandType::ClearLowerThird:
    case CommandType::Kickoff:
    case CommandType::None:
      break;
  }
  if (c.seconds < 0.0 || c.seconds > 30.0) {
    throw std::invalid_argument("seconds must be 0..30");
  }
  return c;
}

}  // namespace

Command parse_command(std::string_view text) {
  try {
    return from_json(json::parse(text));
  } catch (const json::exception& e) {
    throw std::invalid_argument(std::string("bad command JSON: ") + e.what());
  }
}

std::vector<Command> parse_events(std::string_view text) {
  std::vector<Command> out;
  try {
    const json j = json::parse(text);
    if (!j.is_array()) {
      throw std::invalid_argument("events file must be a JSON array");
    }
    for (const json& e : j) {
      Command c = from_json(e);
      if (c.t < 0.0) {
        throw std::invalid_argument("every event needs \"t\" (source seconds) >= 0");
      }
      out.push_back(std::move(c));
    }
  } catch (const json::exception& e) {
    throw std::invalid_argument(std::string("bad events JSON: ") + e.what());
  }
  std::stable_sort(out.begin(), out.end(),
                   [](const Command& a, const Command& b) { return a.t < b.t; });
  return out;
}

std::string_view to_string(CommandType t) {
  switch (t) {
    case CommandType::Goal:
      return "goal";
    case CommandType::SetScore:
      return "score";
    case CommandType::SetTeams:
      return "teams";
    case CommandType::LowerThird:
      return "lower_third";
    case CommandType::ClearLowerThird:
      return "clear_lower_third";
    case CommandType::Replay:
      return "replay";
    case CommandType::Kickoff:
      return "kickoff";
    case CommandType::None:
      break;
  }
  return "none";
}

void ScoreboardState::apply(const Command& c, double out_time, double source_time) {
  switch (c.type) {
    case CommandType::Goal: {
      int& score = c.team == 0 ? home_score : away_score;
      score = std::min(score + 1, 99);
      lower_third = "GOAL  " + (c.team == 0 ? home : away);
      if (!c.text.empty()) {
        lower_third += "  -  " + c.text;
      }
      lower_third_start = out_time;
      lower_third_end = out_time + 6.0;
      break;
    }
    case CommandType::SetScore:
      home_score = c.home_score;
      away_score = c.away_score;
      break;
    case CommandType::SetTeams:
      if (!c.text.empty()) home = c.text;
      if (!c.text2.empty()) away = c.text2;
      break;
    case CommandType::LowerThird:
      lower_third = c.text;
      lower_third_start = out_time;
      lower_third_end = out_time + (c.seconds > 0.0 ? c.seconds : 6.0);
      break;
    case CommandType::ClearLowerThird:
      lower_third_end = std::min(lower_third_end, out_time);
      break;
    case CommandType::Kickoff:
      kickoff_source_time = source_time;
      break;
    case CommandType::Replay:
    case CommandType::None:
      break;
  }
}

std::string ScoreboardState::to_json() const {
  json j;
  j["home"] = home;
  j["away"] = away;
  j["home_score"] = home_score;
  j["away_score"] = away_score;
  j["lower_third"] = lower_third;
  return j.dump();
}

}  // namespace md
