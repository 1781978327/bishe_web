#ifndef HTTP_CTRL_MODEL_H
#define HTTP_CTRL_MODEL_H

// Model loading, unloading, and status functions.
// Extracted from main_http_ctrl.cc during refactoring.

#include <string>
#include <opencv2/core/core.hpp>

// Update the global latest-frame storage (used by HTTP snapshot API).
void update_latest_frame_global(const cv::Mat& frame, int slot);

// Update the per-camera latest-frame storage.
void update_latest_frame_for_cam(int cam, const cv::Mat& frame, int slot);

// Detect the best available model path (caller must hold g_model_mutex).
std::string detect_model_path_locked();

// Build the JSON status response string for /api/status.
std::string build_status_response();

// Release all rknn_lite workers in g_rkpool (caller must hold g_model_mutex).
void release_model_workers_locked();

// Ensure the model is loaded; loads it if necessary. Returns true on success.
bool ensure_model_loaded(std::string* msg_out = nullptr);

// Unload the model and stop all inference. Returns true on success.
bool unload_model_runtime(std::string* msg_out = nullptr);

#endif // HTTP_CTRL_MODEL_H
