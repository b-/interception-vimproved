#include <csignal>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <linux/input.h>
#include <yaml-cpp/yaml.h>

volatile sig_atomic_t interception_enabled = 1;

void toggle_handler(int) {
  interception_enabled = !interception_enabled;
}

class Intercept;
using Event = input_event;
using Key = unsigned short;
using Mapping = std::unordered_map<Key, Key>;
using Intercepts = std::vector<Intercept*>;

const auto KEY_STROKE_UP = 0;
const auto KEY_STROKE_DOWN = 1;

// if less than this many microseconds have elapsed since the last key event,
// the user is considered to be "typing" and intercept keys act as plain taps
//const auto TYPING_TIMEOUT_US = 100000L; // 100 ms
const auto TYPING_TIMEOUT_US = 150000L; // 150 ms
//const auto TYPING_TIMEOUT_US = 200000L; // 200 ms

auto timeval_diff_us(struct timeval a, struct timeval b) -> long {
  return (a.tv_sec - b.tv_sec) * 1000000L + (a.tv_usec - b.tv_usec);
}

auto is_keyup(Event input) -> bool { return input.value == KEY_STROKE_UP; }
auto is_keydown(Event input) -> bool { return input.value == KEY_STROKE_DOWN; }
auto is_modifier(Key key) -> bool {
  // TODO: handle capslock as not modifier?
  static const auto MODIFIERS = std::unordered_set<Key> {
    KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL, KEY_RIGHTCTRL,
    KEY_LEFTALT, KEY_RIGHTALT, KEY_LEFTMETA, KEY_RIGHTMETA, KEY_CAPSLOCK,
  };
  return MODIFIERS.contains(key);
}

// auxilliary renamings for accessibility
// in the real world, braces = `{`, `}`, brackets: `[`, `]`
const auto KEY_BACKTICK = KEY_GRAVE;
const auto KEY_LEFTBRACKET = KEY_LEFTBRACE;
const auto KEY_RIGHTBRACKET = KEY_RIGHTBRACE;
const auto KEY_PRT_SC = KEY_SYSRQ;

// all mappable keys (for now)
// see: github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h
const auto KEYS = std::unordered_map<std::string, Key> {
// KEY(KEY_K) preprocesses to {"KEY_K", KEY_K}
#define KEY(KEYCODE) {#KEYCODE, KEYCODE}

  KEY(KEY_ESC), KEY(KEY_F1), KEY(KEY_F2), KEY(KEY_F3), KEY(KEY_F4), KEY(KEY_F5),
    KEY(KEY_F6), KEY(KEY_F7), KEY(KEY_F8), KEY(KEY_F9), KEY(KEY_F10), KEY(KEY_F11), KEY(KEY_F12),

  KEY(KEY_BACKTICK), KEY(KEY_1), KEY(KEY_2), KEY(KEY_3), KEY(KEY_4), KEY(KEY_5), KEY(KEY_6),
    KEY(KEY_7), KEY(KEY_8), KEY(KEY_9), KEY(KEY_0), KEY(KEY_MINUS), KEY(KEY_EQUAL), KEY(KEY_BACKSPACE),
 
  KEY(KEY_TAB), KEY(KEY_Q), KEY(KEY_W), KEY(KEY_E), KEY(KEY_R), KEY(KEY_T), KEY(KEY_Y),
    KEY(KEY_U), KEY(KEY_I), KEY(KEY_O), KEY(KEY_P), KEY(KEY_LEFTBRACKET), KEY(KEY_RIGHTBRACKET), KEY(KEY_BACKSLASH),

  KEY(KEY_CAPSLOCK), KEY(KEY_A), KEY(KEY_S), KEY(KEY_D), KEY(KEY_F), KEY(KEY_G), KEY(KEY_H),
    KEY(KEY_J), KEY(KEY_K), KEY(KEY_L), KEY(KEY_SEMICOLON), KEY(KEY_APOSTROPHE), KEY(KEY_ENTER),

  KEY(KEY_LEFTSHIFT), KEY(KEY_Z), KEY(KEY_X), KEY(KEY_C), KEY(KEY_V), KEY(KEY_B), KEY(KEY_N),
    KEY(KEY_M), KEY(KEY_COMMA), KEY(KEY_DOT), KEY(KEY_SLASH), KEY(KEY_RIGHTSHIFT),

  KEY(KEY_LEFTCTRL), KEY(KEY_LEFTMETA), KEY(KEY_LEFTALT), KEY(KEY_SPACE), KEY(KEY_RIGHTALT), KEY(KEY_RIGHTMETA), KEY(KEY_RIGHTCTRL),

  KEY(KEY_LEFT), KEY(KEY_DOWN), KEY(KEY_UP), KEY(KEY_RIGHT), KEY(KEY_PAGEDOWN), KEY(KEY_PAGEUP),

  KEY(KEY_HOME), KEY(KEY_END), KEY(KEY_INSERT), KEY(KEY_DELETE),

  // mouse buttons
  KEY(BTN_LEFT), KEY(BTN_RIGHT), KEY(BTN_BACK), KEY(BTN_FORWARD),

  // display brightness
  KEY(KEY_BRIGHTNESSDOWN), KEY(KEY_BRIGHTNESSUP),

  // audio
  KEY(KEY_MUTE), KEY(KEY_VOLUMEDOWN), KEY(KEY_VOLUMEUP),

  KEY(KEY_PRT_SC), KEY(KEY_CONTEXT_MENU), KEY(KEY_PRINT),
#undef KEY
};

auto key_event(int value, Key code, Key type=EV_KEY) -> Event {
  return {.time={.tv_sec=0, .tv_usec=0}, .type=type, .code=code, .value=value};
}

const auto SYNC = key_event(KEY_STROKE_UP, SYN_REPORT, EV_SYN);

auto read_event() -> std::optional<Event> {
  if (auto event = Event{}; std::fread(&event, sizeof(Event), 1, stdin) == 1) {
    return event;
  }
  return {};
}

auto write_events(auto... events) {
  for (auto event : {events...}) {
    if (std::fwrite(&event, sizeof(Event), 1, stdout) != 1) {
      std::exit(EXIT_FAILURE);
    }
  }
}

auto write_keytap(Key key) {
  write_events(key_event(KEY_STROKE_DOWN, key), SYNC, key_event(KEY_STROKE_UP, key));
}

class Intercept {
public:
  auto process(Event input, bool is_typing) -> bool {
    switch (state) {
      case State::START: return process_start(input, is_typing);
      case State::INTERCEPT_KEY_HELD: return process_intercept_held(input);
      case State::OTHER_KEY_HELD: return process_other_key_held(input);
      case State::TYPING_PASSTHROUGH: return process_typing_passthrough(input);
      default: throw std::exception();
    }
  }

  virtual auto reset() -> void {
    if (state == State::TYPING_PASSTHROUGH) {
      write_events(key_event(KEY_STROKE_UP, tap), SYNC);
    }
    state = State::START;
    emit_tap = true;
  }

protected:
  Intercept(Key intercept, Key tap)
      : intercept{intercept}, tap{tap}, state{State::START}, emit_tap{true} {}

  enum class State {START, INTERCEPT_KEY_HELD, OTHER_KEY_HELD, TYPING_PASSTHROUGH};
  Key intercept;
  Key tap;
  State state;
  bool emit_tap;

  auto is_intercept(Event input) -> bool { return input.code == intercept; }

  auto remapped(Event input, Key key) -> Event {
    input.code = key;
    return input;
  }

  auto process_start(Event input, bool is_typing) -> bool {
    if (is_intercept(input) && is_keydown(input)) {
      if (is_typing) {
        // user is actively typing — treat intercept key as a plain tap
        write_events(remapped(input, tap));
        state = State::TYPING_PASSTHROUGH;
        return false;
      }
      emit_tap = true;
      state = State::INTERCEPT_KEY_HELD;
      return false;
    }
    return true;
  };

  virtual auto process_intercept_held(Event) -> bool = 0;
  virtual auto process_other_key_held(Event) -> bool { return false; }

  auto process_typing_passthrough(Event input) -> bool {
    if (is_intercept(input)) {
      if (is_keyup(input)) {
        write_events(remapped(input, tap));
        state = State::START;
        return false;
      }
      // repeat events — emit as tap repeat
      write_events(remapped(input, tap));
      return false;
    }
    // all other keys pass through normally
    return true;
  }
};

class Modifier : public Intercept {
public:
  Modifier(Key intercept, Key tap, Key modifier)
      : Intercept{intercept, tap}, modifier{modifier} {}

private:
  Key modifier;

  using Intercept::remapped;

  auto reset() -> void override {
    if (state == State::INTERCEPT_KEY_HELD && !emit_tap) {
      write_events(key_event(KEY_STROKE_UP, modifier), SYNC);
    }
    Intercept::reset();
  }

  auto process_intercept_held(Event input) -> bool override {
    if (is_intercept(input)) {
      if (!is_keyup(input)) return false;
      if (emit_tap) {
        write_keytap(tap);
      } else {
        write_events(remapped(input, modifier));
      }
      state = State::START;
      return false;
    }
    if (is_keydown(input) && emit_tap) {
      // on first non-matching input after a matching input
      // for some reason, need to push "SYNC" after modifier here
      write_events(remapped(input, modifier), SYNC);
      emit_tap = false;
    }
    return true;
  }
};

class Layer : public Intercept {
public:
  Layer(Key intercept, Key tap, Mapping mapping)
      : Intercept{intercept, tap}, mapping{mapping} {}

private:
  Mapping mapping;
  std::unordered_set<Key> held_keys;

  auto is_mapped(Event input) -> bool { return mapping.contains(input.code); }

  auto is_held(Event input) -> bool { return held_keys.contains(input.code); }

  auto reset() -> void override {
    for (auto held_key : held_keys) {
      write_events(key_event(KEY_STROKE_UP, mapping[held_key]), SYNC);
    }
    held_keys.clear();
    Intercept::reset();
  }

  using Intercept::remapped;
  auto remapped(Event input) -> Event { return remapped(input, mapping[input.code]); }

  auto process_intercept_held(Event input) -> bool override {
    if (is_intercept(input)) {
      // don't emit anything
      if (!is_keyup(input)) return false;
      // TODO: find a way to have a mouse click mark the key as intercepted
      // or just make it time based
      // emit_tap &= mouse clicked || timeout;
      if (emit_tap) {
        write_keytap(tap);
        emit_tap = false;
      }
      state = State::START;
      return false;
    }
    if (is_keydown(input)) {
      // any other key
      // NOTE: if we don't blindly set emit_tap to false on any
      // keypress, we can type faster because only in case of mapped key down,
      // the intercepted key will not be emitted - useful for scenario:
      // L_DOWN, SPACE_DOWN, A_DOWN, L_UP, A_UP, SPACE_UP
      emit_tap &= !is_mapped(input) && !is_modifier(input.code);

      if (is_mapped(input)) {
        held_keys.insert(input.code);
        write_events(remapped(input));
        state = State::OTHER_KEY_HELD;
        return false;
      }
    }
    return true;
  }

  auto process_other_key_held(Event input) -> bool override {
    if (is_intercept(input) && !is_keyup(input)) return false;
    if (is_keydown(input) && is_held(input)) return false;
    if (!is_keyup(input) && !is_mapped(input)) return true;
    if (is_keyup(input)) {
      // one of mapped held keys goes up
      if (is_held(input)) {
        write_events(remapped(input));
        held_keys.erase(input.code);
        if (held_keys.empty()) {
          state = State::INTERCEPT_KEY_HELD;
        }
      } else if (is_intercept(input)) {
        // key that was not mapped & held goes up
        for (auto held_key : held_keys) {
          write_events(key_event(KEY_STROKE_UP, mapping[held_key]), SYNC);
        }
        held_keys.clear();
        state = State::START;
      }
    } else if (is_mapped(input)) {
      write_events(remapped(input));
      if (is_keydown(input)) {
        held_keys.insert(input.code);
      }
    }
    return false;
  }
};

class Interceptor {
public:
  Interceptor(Intercepts intercepts) : intercepts{intercepts} {}

  auto event_loop() {
    while (auto input = read_event()) {
      intercept(*input);
    }
  }

private:
  Intercepts intercepts;
  bool was_enabled = true;
  struct timeval last_key_time = {0, 0};

  auto intercept(Event input) -> void {
    if (!interception_enabled) {
      if (was_enabled) {
        for (auto& i : intercepts) i->reset();
        was_enabled = false;
        std::fprintf(stderr, "interception-vimproved: disabled\n");
      }
      write_events(input);
      return;
    }
    if (!was_enabled) {
      was_enabled = true;
      std::fprintf(stderr, "interception-vimproved: enabled\n");
    }
    if (input.type == EV_MSC && input.code == MSC_SCAN) return;
    if (input.type == EV_KEY) {
      auto is_typing = compute_is_typing(input);
      if ((is_keydown(input) || is_keyup(input)) && !is_modifier(input.code)) {
        last_key_time = input.time;
      }
      if (!processed(input, is_typing)) return;
    }
    write_events(input);
  }

  auto compute_is_typing(Event input) -> bool {
    if (last_key_time.tv_sec == 0 && last_key_time.tv_usec == 0) return false;
    auto elapsed = timeval_diff_us(input.time, last_key_time);
    return elapsed > 0 && elapsed < TYPING_TIMEOUT_US;
  }

  auto processed(Event input, bool is_typing) -> bool {
    auto processed = true;
    for (auto& intercept : intercepts) {
      processed &= intercept->process(input, is_typing);
    }
    return processed;
  }
};

auto default_config() -> Intercepts {
  auto caps = new Layer{KEY_CAPSLOCK, KEY_CAPSLOCK, Mapping {
    // vim home row
    {KEY_H, KEY_LEFT},
    {KEY_J, KEY_DOWN},
    {KEY_K, KEY_UP},
    {KEY_L, KEY_RIGHT},
  }};

  auto enter = new Modifier{KEY_ENTER, KEY_ENTER, KEY_RIGHTCTRL};
  auto space = new Modifier{KEY_SPACE, KEY_SPACE, KEY_LEFTMETA};
  return Intercepts{caps, enter, space};
}

auto read_config_or_default(int argc, char** argv) -> Intercepts {
  if (argc < 2) {
    std::fprintf(stderr, "No config file provided, using default config\n");
    return default_config();
  }
  // NOTE: modifiers must go first because layer.process_intercept_held
  // emits mapped key as soon as layer.process is called
  // if layer.process is run before modifier.process, the modifier is not emitted
  auto modifiers = Intercepts{};
  auto layers = Intercepts{};

  auto read_key = [&](auto node){return KEYS.at(node.template as<std::string>());};

  auto read_mapping = [&](auto node) {
    auto mapping = Mapping{};
    for (const auto& node : node) {
      mapping[read_key(node["from"])] = read_key(node["to"]);
    }
    return mapping;
  };

  auto read_intercept = [&](auto node) {
    auto intercept = read_key(node["intercept"]);
    auto ontap = node["ontap"] ? read_key(node["ontap"]) : intercept;
    auto onhold = node["onhold"];

    if (onhold.IsScalar()) {
      auto modifier = read_key(onhold);
      if (!is_modifier(modifier)) throw std::invalid_argument(
        "Specified wrong modifier key"
      );
      modifiers.push_back(new Modifier{intercept, ontap, modifier});
    } else if (onhold.IsSequence()) {
      layers.push_back(new Layer{intercept, ontap, read_mapping(onhold)});
    } else {
      // TODO: treat empty `onhold` as a simple remapping (like caps2esc)
      throw std::invalid_argument(
        "Invalid Configuration: onhold must be sequence or scalar"
      );
    }
  };
  try {
    auto intercepts = Intercepts{};
    std::ranges::for_each(YAML::LoadFile(argv[1]), read_intercept);
    std::ranges::merge(modifiers, layers, std::back_inserter(intercepts));
    return intercepts;
  } catch (...) {
    return default_config();
  }
}

auto main(int argc, char** argv) -> int {
  std::setbuf(stdin, NULL);
  std::setbuf(stdout, NULL);
  std::signal(SIGUSR1, toggle_handler);
  Interceptor(read_config_or_default(argc, argv)).event_loop();
}
