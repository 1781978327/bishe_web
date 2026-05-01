#include "sound_audio_config.h"
#include "sound_globals.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int parse_bool_text_local(const char *value, int default_value) {
    if (!value || value[0] == '\0') return default_value;
    if (strcmp(value, "1") == 0 ||
        strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0) {
        return 1;
    }
    if (strcmp(value, "0") == 0 ||
        strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0) {
        return 0;
    }
    return default_value;
}

std::string trim_copy(const std::string &value) {
    size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string strip_yaml_comment(const std::string &line) {
    bool in_single = false;
    bool in_double = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"' && !in_single) {
            in_double = !in_double;
            continue;
        }
        if (c == '\'' && !in_double) {
            in_single = !in_single;
            continue;
        }
        if (c == '#' && !in_single && !in_double) {
            return trim_copy(line.substr(0, i));
        }
    }
    return trim_copy(line);
}

std::string unquote_yaml_value(const std::string &value) {
    std::string trimmed = trim_copy(value);
    if (trimmed == "null" || trimmed == "~") return "";
    if (trimmed.size() >= 2) {
        char first = trimmed.front();
        char last = trimmed.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            return trimmed.substr(1, trimmed.size() - 2);
        }
    }
    return trimmed;
}

void copy_string_value(char *dest, size_t dest_size, const std::string &value) {
    if (!dest || dest_size == 0) return;
    strncpy(dest, value.c_str(), dest_size - 1);
    dest[dest_size - 1] = '\0';
}

void apply_runtime_audio_yaml_value(const std::string &key_path, const std::string &raw_value) {
    std::string value = unquote_yaml_value(raw_value);

    if (key_path == "realtime.device") {
        copy_string_value(g_rt_default_device, sizeof(g_rt_default_device), value);
    } else if (key_path == "realtime.fallback_device") {
        copy_string_value(g_rt_fallback_device, sizeof(g_rt_fallback_device), value);
    } else if (key_path == "realtime.auto_start") {
        g_auto_start_realtime = parse_bool_text_local(value.c_str(), g_auto_start_realtime);
    } else if (key_path == "realtime.print_asr") {
        g_rt_print_asr = parse_bool_text_local(value.c_str(), g_rt_print_asr);
    } else if (key_path == "realtime.print_window") {
        g_rt_print_window = parse_bool_text_local(value.c_str(), g_rt_print_window);
    } else if (key_path == "realtime.capture_volume") {
        float capture_volume = (float)atof(value.c_str());
        if (capture_volume > 2.0f && capture_volume <= 200.0f) {
            capture_volume /= 100.0f;
        }
        if (capture_volume >= 0.0f && capture_volume <= 2.0f) {
            g_rt_capture_volume = capture_volume;
        }
    } else if (key_path == "realtime.pulse.set_default_source") {
        g_rt_pulse_set_default_source = parse_bool_text_local(value.c_str(), g_rt_pulse_set_default_source);
    } else if (key_path == "realtime.pulse.source_volume") {
        copy_string_value(g_rt_pulse_source_volume, sizeof(g_rt_pulse_source_volume), value);
    } else if (key_path == "realtime.mixer.enabled") {
        g_rt_amixer_enabled = parse_bool_text_local(value.c_str(), g_rt_amixer_enabled);
    } else if (key_path == "realtime.mixer.card") {
        copy_string_value(g_rt_amixer_card, sizeof(g_rt_amixer_card), value);
    } else if (key_path == "realtime.mixer.control") {
        copy_string_value(g_rt_amixer_control, sizeof(g_rt_amixer_control), value);
    } else if (key_path == "realtime.mixer.volume") {
        copy_string_value(g_rt_amixer_volume, sizeof(g_rt_amixer_volume), value);
    } else if (key_path == "realtime.mixer.auto_gain_control") {
        copy_string_value(g_rt_amixer_auto_gain_control, sizeof(g_rt_amixer_auto_gain_control), value);
    } else if (key_path == "realtime.filter.ffmpeg.enabled") {
        g_rt_ffmpeg_filter_enabled = parse_bool_text_local(value.c_str(), g_rt_ffmpeg_filter_enabled);
    } else if (key_path == "realtime.filter.ffmpeg.bin") {
        copy_string_value(g_rt_ffmpeg_bin, sizeof(g_rt_ffmpeg_bin), value);
    } else if (key_path == "realtime.filter.ffmpeg.audio_filter") {
        copy_string_value(g_rt_ffmpeg_audio_filter, sizeof(g_rt_ffmpeg_audio_filter), value);
    } else if (key_path == "realtime.filter.sox.enabled") {
        g_sox_denoise_enabled = parse_bool_text_local(value.c_str(), g_sox_denoise_enabled);
    } else if (key_path == "realtime.filter.sox.bin") {
        copy_string_value(g_sox_bin, sizeof(g_sox_bin), value);
    } else if (key_path == "realtime.filter.sox.profile") {
        copy_string_value(g_sox_denoise_profile, sizeof(g_sox_denoise_profile), value);
    } else if (key_path == "realtime.filter.sox.amount") {
        float sox_amount = (float)atof(value.c_str());
        if (sox_amount > 0.0f && sox_amount <= 1.0f) {
            g_sox_denoise_amount = sox_amount;
        }
    }
}

int resolve_runtime_audio_config_path(char *path_out, size_t path_size) {
    if (!path_out || path_size == 0) return -1;

    const char *env_path = getenv("SOUND_MONITORING_CONFIG");
    const char *candidates[] = {
        "./config/runtime_audio.yaml",
        "../config/runtime_audio.yaml",
        "../../config/runtime_audio.yaml",
        NULL
    };

    if (env_path && env_path[0]) {
        std::ifstream env_file(env_path);
        if (env_file.good()) {
            copy_string_value(path_out, path_size, env_path);
            return 0;
        }
        printf("[CONFIG] SOUND_MONITORING_CONFIG not found: %s\n", env_path);
    }

    for (int i = 0; candidates[i] != NULL; ++i) {
        const char *candidate = candidates[i];
        std::ifstream candidate_file(candidate);
        if (candidate_file.good()) {
            copy_string_value(path_out, path_size, candidate);
            return 0;
        }
    }

    path_out[0] = '\0';
    return -1;
}

void init_runtime_audio_yaml_config() {
    char config_path[512] = {0};
    if (resolve_runtime_audio_config_path(config_path, sizeof(config_path)) != 0) {
        printf("[CONFIG] runtime_audio.yaml not found, use built-in defaults\n");
        return;
    }

    std::ifstream in(config_path);
    if (!in.is_open()) {
        printf("[CONFIG] failed to open runtime audio config: %s\n", config_path);
        return;
    }

    copy_string_value(g_runtime_audio_config_path, sizeof(g_runtime_audio_config_path), config_path);

    std::vector<std::string> sections;
    std::string raw_line;
    while (std::getline(in, raw_line)) {
        std::string line = strip_yaml_comment(raw_line);
        if (line.empty()) continue;

        size_t non_space = raw_line.find_first_not_of(" \t");
        if (non_space == std::string::npos) continue;

        int indent = 0;
        for (size_t i = 0; i < raw_line.size(); ++i) {
            if (raw_line[i] == ' ') {
                indent += 1;
            } else if (raw_line[i] == '\t') {
                indent += 2;
            } else {
                break;
            }
        }

        std::string working = raw_line.substr(non_space);
        working = strip_yaml_comment(working);
        if (working.empty() || working[0] == '-') continue;

        size_t colon = working.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim_copy(working.substr(0, colon));
        std::string value = trim_copy(working.substr(colon + 1));
        int level = indent / 2;

        if ((int)sections.size() > level) {
            sections.resize(level);
        }

        if (value.empty()) {
            if ((int)sections.size() == level) {
                sections.push_back(key);
            } else if ((int)sections.size() > level) {
                sections[level] = key;
            }
            continue;
        }

        std::string key_path;
        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i].empty()) continue;
            if (!key_path.empty()) key_path += ".";
            key_path += sections[i];
        }
        if (!key_path.empty()) key_path += ".";
        key_path += key;

        apply_runtime_audio_yaml_value(key_path, value);
    }

    copy_string_value(rt_device, sizeof(rt_device), g_rt_default_device);
    printf("[CONFIG] loaded runtime audio config: %s\n", g_runtime_audio_config_path);
    printf("[CONFIG] realtime defaults: device=%s, mixer=%s, ffmpeg=%s, sox=%s\n",
           g_rt_default_device,
           g_rt_amixer_enabled ? "on" : "off",
           g_rt_ffmpeg_filter_enabled ? "on" : "off",
           g_sox_denoise_enabled ? "on" : "off");
}

std::string shell_quote_single(const char *input) {
    std::string out = "'";
    if (input) {
        for (const char *p = input; *p; ++p) {
            if (*p == '\'') {
                out += "'\\''";
            } else {
                out.push_back(*p);
            }
        }
    }
    out += "'";
    return out;
}

std::string run_command_capture(const std::string &cmd) {
    std::string output;
    FILE *pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return output;
    }

    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        output += buffer;
    }
    pclose(pipe);
    return output;
}

std::string resolve_default_pulse_source_name() {
    std::string info = run_command_capture("pactl info 2>/dev/null");
    std::istringstream iss(info);
    std::string line;
    while (std::getline(iss, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.rfind("Default Source:", 0) == 0) {
            return trim_copy(trimmed.substr(strlen("Default Source:")));
        }
        if (trimmed.rfind("默认信源：", 0) == 0) {
            return trim_copy(trimmed.substr(strlen("默认信源：")));
        }
        if (trimmed.rfind("默认信源:", 0) == 0) {
            return trim_copy(trimmed.substr(strlen("默认信源:")));
        }
    }
    return "";
}

std::string extract_pulse_source_name_from_device(const char *device) {
    if (!device || device[0] == '\0') {
        return "";
    }
    if (strncmp(device, "parec:", 6) == 0) {
        return std::string(device + 6);
    }
    if (strncmp(device, "pulse:", 6) == 0) {
        return std::string(device + 6);
    }
    if (strcmp(device, "parec") == 0 || strcmp(device, "pulse") == 0) {
        return resolve_default_pulse_source_name();
    }
    return "";
}

int resolve_alsa_card_from_pulse_source(const char *source_name, char *card_out, size_t card_out_size) {
    if (!source_name || source_name[0] == '\0' || !card_out || card_out_size == 0) {
        return -1;
    }

    std::string sources = run_command_capture("pactl list sources 2>/dev/null");
    std::istringstream iss(sources);
    std::string line;
    bool in_target = false;

    while (std::getline(iss, line)) {
        std::string trimmed = trim_copy(line);
        if (trimmed.rfind("Source #", 0) == 0 || trimmed.rfind("信源 #", 0) == 0) {
            in_target = false;
            continue;
        }

        if (trimmed.rfind("Name:", 0) == 0) {
            in_target = trim_copy(trimmed.substr(strlen("Name:"))) == source_name;
            continue;
        }
        if (trimmed.rfind("名称：", 0) == 0) {
            in_target = trim_copy(trimmed.substr(strlen("名称："))) == source_name;
            continue;
        }
        if (trimmed.rfind("名称:", 0) == 0) {
            in_target = trim_copy(trimmed.substr(strlen("名称:"))) == source_name;
            continue;
        }

        if (!in_target) {
            continue;
        }

        size_t pos = trimmed.find("alsa.card = \"");
        if (pos != std::string::npos) {
            pos += strlen("alsa.card = \"");
            size_t end = trimmed.find('"', pos);
            if (end != std::string::npos && end > pos) {
                copy_string_value(card_out, card_out_size, trimmed.substr(pos, end - pos));
                return 0;
            }
        }
    }

    return -1;
}

void apply_rt_capture_pulse_settings(const char *device) {
    std::string source_name = extract_pulse_source_name_from_device(device);
    if (source_name.empty()) {
        return;
    }

    if (g_rt_pulse_set_default_source) {
        std::ostringstream cmd;
        cmd << "pactl set-default-source " << shell_quote_single(source_name.c_str()) << " >/dev/null 2>&1";
        int ret = system(cmd.str().c_str());
        if (ret != 0) {
            printf("[RT WARN] Failed to set Pulse default source: %s (ret=%d)\n", source_name.c_str(), ret);
        } else {
            printf("[RT] Pulse default source set to: %s\n", source_name.c_str());
        }
    }

    if (g_rt_pulse_source_volume[0] != '\0') {
        std::ostringstream cmd;
        cmd << "pactl set-source-volume " << shell_quote_single(source_name.c_str())
            << " " << shell_quote_single(g_rt_pulse_source_volume) << " >/dev/null 2>&1";
        int ret = system(cmd.str().c_str());
        if (ret != 0) {
            printf("[RT WARN] Failed to set Pulse source volume: %s -> %s (ret=%d)\n",
                   source_name.c_str(), g_rt_pulse_source_volume, ret);
        } else {
            printf("[RT] Pulse source volume applied: %s -> %s\n",
                   source_name.c_str(), g_rt_pulse_source_volume);
        }
    }
}

void init_rt_capture_mixer_config() {
    const char *env_enabled = getenv("RT_AMIXER_ENABLED");
    const char *env_card = getenv("RT_AMIXER_CARD");
    const char *env_control = getenv("RT_AMIXER_CONTROL");
    const char *env_volume = getenv("RT_AMIXER_VOLUME");
    const char *env_auto_gain = getenv("RT_AMIXER_AUTO_GAIN_CONTROL");

    g_rt_amixer_enabled = parse_bool_text_local(env_enabled, g_rt_amixer_enabled);
    if (!g_rt_amixer_enabled) {
        printf("[RT] Hardware amixer setup disabled by RT_AMIXER_ENABLED=%s\n",
               env_enabled ? env_enabled : "0");
        return;
    }

    if (env_card && env_card[0]) {
        strncpy(g_rt_amixer_card, env_card, sizeof(g_rt_amixer_card) - 1);
        g_rt_amixer_card[sizeof(g_rt_amixer_card) - 1] = '\0';
    }
    if (env_control && env_control[0]) {
        strncpy(g_rt_amixer_control, env_control, sizeof(g_rt_amixer_control) - 1);
        g_rt_amixer_control[sizeof(g_rt_amixer_control) - 1] = '\0';
    }
    if (env_volume && env_volume[0]) {
        strncpy(g_rt_amixer_volume, env_volume, sizeof(g_rt_amixer_volume) - 1);
        g_rt_amixer_volume[sizeof(g_rt_amixer_volume) - 1] = '\0';
    }
    if (env_auto_gain && env_auto_gain[0]) {
        strncpy(g_rt_amixer_auto_gain_control, env_auto_gain, sizeof(g_rt_amixer_auto_gain_control) - 1);
        g_rt_amixer_auto_gain_control[sizeof(g_rt_amixer_auto_gain_control) - 1] = '\0';
    }

    if (system("command -v amixer >/dev/null 2>&1") != 0) {
        g_rt_amixer_enabled = 0;
        printf("[RT] amixer not found, skip hardware mic setup\n");
        return;
    }

    printf("[RT] Hardware mic setup enabled: amixer -c %s sset %s %s%s%s\n",
           g_rt_amixer_card,
           g_rt_amixer_control,
           g_rt_amixer_volume,
           g_rt_amixer_auto_gain_control[0] ? ", Auto Gain Control=" : "",
           g_rt_amixer_auto_gain_control[0] ? g_rt_amixer_auto_gain_control : "");
}

void apply_rt_capture_mixer(const char *device) {
    if (!g_rt_amixer_enabled) return;

    char effective_card[32];
    copy_string_value(effective_card, sizeof(effective_card), g_rt_amixer_card);

    if (strcasecmp(effective_card, "auto") == 0 || effective_card[0] == '\0') {
        std::string source_name = extract_pulse_source_name_from_device(device);
        if (!source_name.empty() &&
            resolve_alsa_card_from_pulse_source(source_name.c_str(), effective_card, sizeof(effective_card)) == 0) {
            printf("[RT] Resolved mixer card from Pulse source %s -> card %s\n",
                   source_name.c_str(), effective_card);
        } else {
            printf("[RT WARN] Failed to resolve mixer card automatically for device: %s\n",
                   device ? device : "(null)");
            return;
        }
    }

    std::ostringstream cmd;
    cmd << "amixer -c " << shell_quote_single(effective_card)
        << " sset " << shell_quote_single(g_rt_amixer_control)
        << " " << shell_quote_single(g_rt_amixer_volume)
        << " >/dev/null 2>&1";

    int ret = system(cmd.str().c_str());
    if (ret != 0) {
        printf("[RT WARN] Failed to apply hardware mic setup: amixer -c %s sset %s %s (ret=%d)\n",
               effective_card, g_rt_amixer_control, g_rt_amixer_volume, ret);
        return;
    }

    printf("[RT] Applied hardware mic setup: amixer -c %s sset %s %s\n",
           effective_card, g_rt_amixer_control, g_rt_amixer_volume);

    if (g_rt_amixer_auto_gain_control[0] != '\0') {
        std::ostringstream agc_cmd;
        agc_cmd << "amixer -c " << shell_quote_single(effective_card)
                << " sset 'Auto Gain Control' " << shell_quote_single(g_rt_amixer_auto_gain_control)
                << " >/dev/null 2>&1";

        int agc_ret = system(agc_cmd.str().c_str());
        if (agc_ret != 0) {
            printf("[RT WARN] Failed to apply auto gain control: amixer -c %s sset 'Auto Gain Control' %s (ret=%d)\n",
                   effective_card, g_rt_amixer_auto_gain_control, agc_ret);
        } else {
            printf("[RT] Applied auto gain control: amixer -c %s sset 'Auto Gain Control' %s\n",
                   effective_card, g_rt_amixer_auto_gain_control);
        }
    }
}
