#pragma once

#include <cstddef>
#include <string>

// Parse a boolean text value ("true"/"1"/"yes"/"on" => 1, "false"/"0"/"no"/"off" => 0)
int parse_bool_text_local(const char *value, int default_value);

// Trim whitespace from both ends, return a new string
std::string trim_copy(const std::string &value);

// Strip YAML comments (respecting quoted strings)
std::string strip_yaml_comment(const std::string &line);

// Remove surrounding quotes from a YAML value
std::string unquote_yaml_value(const std::string &value);

// Safe string copy into a fixed-size buffer
void copy_string_value(char *dest, size_t dest_size, const std::string &value);

// Apply a single YAML key_path=value to the runtime audio globals
void apply_runtime_audio_yaml_value(const std::string &key_path, const std::string &raw_value);

// Search for the runtime_audio.yaml config file
int resolve_runtime_audio_config_path(char *path_out, size_t path_size);

// Load and apply the runtime_audio.yaml configuration
void init_runtime_audio_yaml_config();

// Shell-quote a string using single quotes
std::string shell_quote_single(const char *input);

// Run a shell command and capture its stdout
std::string run_command_capture(const std::string &cmd);

// Resolve the default PulseAudio source name
std::string resolve_default_pulse_source_name();

// Extract PulseAudio source name from a device string (e.g. "parec:alsa_input...")
std::string extract_pulse_source_name_from_device(const char *device);

// Resolve the ALSA card number from a PulseAudio source name
int resolve_alsa_card_from_pulse_source(const char *source_name, char *card_out, size_t card_out_size);

// Apply PulseAudio source settings (default source, volume) for the capture device
void apply_rt_capture_pulse_settings(const char *device);

// Initialize hardware mixer configuration from environment variables
void init_rt_capture_mixer_config();

// Apply hardware mixer settings (amixer) for the capture device
void apply_rt_capture_mixer(const char *device);
