#ifndef HTTP_CTRL_RECORDING_H
#define HTTP_CTRL_RECORDING_H

// Camera recording management (start/stop/status).
// Extracted from main_http_ctrl.cc during refactoring.

#include <string>

// Human-readable camera name for log messages.
std::string record_camera_name(int cam);

// Parse camera index from HTTP query parameters (cam/camera/cameraId).
int parse_record_camera_index(const std::string& path);

// Return the RTSP URL for the given camera index.
std::string recording_rtsp_url_for_camera(int cam);

// Refresh recording state for a single camera (caller must hold g_record_mutex).
void refresh_recording_state_locked(int cam);

// Refresh recording state for both cameras.
void refresh_all_recording_states();

// Build a sanitized .mp4 filename for a recording.
std::string build_recording_filename(int cam, const std::string& requested_name);

// Ensure RTSP sender is ready for recording the given camera.
bool ensure_camera_rtsp_ready_for_recording(int cam, std::string* detail);

// Start recording a camera via ffmpeg fork.
bool start_camera_recording(int cam, const std::string& requested_name,
                            std::string* out_message, int* out_status_code);

// Stop recording a camera.
bool stop_camera_recording(int cam, std::string* out_message, int* out_status_code);

// Stop all active recordings.
void stop_all_recordings();

#endif // HTTP_CTRL_RECORDING_H
