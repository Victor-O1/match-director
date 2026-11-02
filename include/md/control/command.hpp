#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace md {

enum class CommandType : std::uint8_t {
  None,
  Goal,        // team scores: +1, lower third, automatic replay
  SetScore,    // explicit score
  SetTeams,    // team names
  LowerThird,  // caption bar for `seconds`
  ClearLowerThird,
  Replay,   // replay the last `seconds`
  Kickoff,  // match clock = 00:00 at this moment
};

// One operator action. Comes from the events file (with a source time `t`) or
// from the HTTP control API (applied at the next frame, t < 0).
struct Command {
  CommandType type{CommandType::None};
  int team{0};  // 0 = home, 1 = away
  int home_score{0};
  int away_score{0};
  std::string text;      // lower-third text, scorer, or home name
  std::string text2;     // away name
  double seconds{0.0};   // replay length / lower-third duration
  double t{-1.0};        // source time (s) to apply at; < 0 = immediately
  std::uint32_t seq{0};  // assigned by the pipeline (frame-accurate ordering)
};

// Parses one command from JSON text, e.g. {"type":"goal","team":"home","text":"No. 9"}.
// Throws std::invalid_argument with a readable message on bad input.
[[nodiscard]] Command parse_command(std::string_view json);

// Parses an events file: a JSON array of commands, each with "t" (seconds of
// source time). Returns them sorted by t.
[[nodiscard]] std::vector<Command> parse_events(std::string_view json);

[[nodiscard]] std::string_view to_string(CommandType t);

// Scoreboard / graphics model. Owned by the compositor thread.
struct ScoreboardState {
  std::string home{"HOME"};
  std::string away{"AWAY"};
  int home_score{0};
  int away_score{0};
  double kickoff_source_time{0.0};
  std::string lower_third;
  double lower_third_start{-1.0};  // output time (s)
  double lower_third_end{-1.0};

  // `out_time` = current output time in seconds, `source_time` = current source time.
  void apply(const Command& c, double out_time, double source_time);
  [[nodiscard]] std::string to_json() const;
};

}  // namespace md
