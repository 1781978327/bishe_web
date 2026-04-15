#include "asr_vosk.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

typedef void VoskModel;
typedef void VoskRecognizer;

struct VoskApiFns {
    VoskModel *(*model_new)(const char *model_path) = nullptr;
    void (*model_free)(VoskModel *model) = nullptr;
    VoskRecognizer *(*recognizer_new)(VoskModel *model, float sample_rate) = nullptr;
    void (*recognizer_free)(VoskRecognizer *recognizer) = nullptr;
    int (*recognizer_accept_waveform)(VoskRecognizer *recognizer, const char *data, int length) = nullptr;
    const char *(*recognizer_final_result)(VoskRecognizer *recognizer) = nullptr;
    void (*recognizer_set_max_alternatives)(VoskRecognizer *recognizer, int max_alternatives) = nullptr;
    void (*set_log_level)(int log_level) = nullptr;
};

bool PathExists(const char *path)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    struct stat st;
    return stat(path, &st) == 0;
}

std::string ExtractJsonStringField(const std::string &json, const std::string &key)
{
    std::size_t key_pos = json.find(key);
    if (key_pos == std::string::npos) {
        return "";
    }

    std::size_t colon_pos = json.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return "";
    }

    std::size_t first_quote = json.find('"', colon_pos + 1);
    if (first_quote == std::string::npos) {
        return "";
    }

    std::string out;
    for (std::size_t i = first_quote + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\') {
            if (i + 1 >= json.size()) {
                break;
            }
            char next = json[++i];
            if (next == 'n') {
                out.push_back('\n');
            } else if (next == 't') {
                out.push_back('\t');
            } else if (next == 'r') {
                out.push_back('\r');
            } else {
                out.push_back(next);
            }
        } else if (c == '"') {
            break;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

double ExtractJsonNumberField(const std::string &json, const std::string &key)
{
    std::size_t key_pos = json.find(key);
    if (key_pos == std::string::npos) {
        return -std::numeric_limits<double>::infinity();
    }

    std::size_t colon_pos = json.find(':', key_pos + key.size());
    if (colon_pos == std::string::npos) {
        return -std::numeric_limits<double>::infinity();
    }

    std::size_t num_begin = json.find_first_of("+-0123456789.", colon_pos + 1);
    if (num_begin == std::string::npos) {
        return -std::numeric_limits<double>::infinity();
    }

    std::size_t num_end = num_begin;
    while (num_end < json.size()) {
        char c = json[num_end];
        if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E') {
            ++num_end;
            continue;
        }
        break;
    }

    try {
        return std::stod(json.substr(num_begin, num_end - num_begin));
    } catch (...) {
        return -std::numeric_limits<double>::infinity();
    }
}

std::vector<int16_t> FloatToPcm16(const float *data, int num_frames)
{
    std::vector<int16_t> pcm;
    pcm.resize(std::max(num_frames, 0));
    for (int i = 0; i < num_frames; ++i) {
        float v = data[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        pcm[i] = static_cast<int16_t>(std::lrintf(v * 32767.0f));
    }
    return pcm;
}

}  // namespace

struct VoskAsrEngine::Api {
    void *lib_handle = nullptr;
    VoskApiFns fn;
};

VoskAsrEngine::VoskAsrEngine()
    : api_(nullptr), model_cn_(nullptr), model_en_(nullptr), enabled_(false) {}

VoskAsrEngine::~VoskAsrEngine()
{
    Close();
}

void VoskAsrEngine::SetError(const std::string &msg)
{
    last_error_ = msg;
}

bool VoskAsrEngine::LoadApi()
{
    api_ = new Api();

    const char *env_lib = std::getenv("VOSK_LIB_PATH");
    const char *candidates[] = {
        env_lib,
        "libvosk.so",
        "/usr/local/lib/libvosk.so",
        "/usr/lib/libvosk.so",
        "/home/orangepi/miniconda3/lib/python3.12/site-packages/vosk/libvosk.so",
        nullptr
    };

    for (int i = 0; candidates[i] != nullptr; ++i) {
        if (candidates[i] == nullptr || candidates[i][0] == '\0') {
            continue;
        }
        api_->lib_handle = dlopen(candidates[i], RTLD_LAZY | RTLD_LOCAL);
        if (api_->lib_handle != nullptr) {
            break;
        }
    }

    if (api_->lib_handle == nullptr) {
        SetError("Failed to load libvosk.so. Set VOSK_LIB_PATH to the full .so path.");
        delete api_;
        api_ = nullptr;
        return false;
    }

    api_->fn.model_new = (VoskModel *(*)(const char *))dlsym(api_->lib_handle, "vosk_model_new");
    api_->fn.model_free = (void (*)(VoskModel *))dlsym(api_->lib_handle, "vosk_model_free");
    api_->fn.recognizer_new = (VoskRecognizer *(*)(VoskModel *, float))dlsym(api_->lib_handle, "vosk_recognizer_new");
    api_->fn.recognizer_free = (void (*)(VoskRecognizer *))dlsym(api_->lib_handle, "vosk_recognizer_free");
    api_->fn.recognizer_accept_waveform =
        (int (*)(VoskRecognizer *, const char *, int))dlsym(api_->lib_handle, "vosk_recognizer_accept_waveform");
    api_->fn.recognizer_final_result =
        (const char *(*)(VoskRecognizer *))dlsym(api_->lib_handle, "vosk_recognizer_final_result");
    api_->fn.recognizer_set_max_alternatives =
        (void (*)(VoskRecognizer *, int))dlsym(api_->lib_handle, "vosk_recognizer_set_max_alternatives");
    api_->fn.set_log_level = (void (*)(int))dlsym(api_->lib_handle, "vosk_set_log_level");

    if (api_->fn.model_new == nullptr || api_->fn.model_free == nullptr ||
        api_->fn.recognizer_new == nullptr || api_->fn.recognizer_free == nullptr ||
        api_->fn.recognizer_accept_waveform == nullptr || api_->fn.recognizer_final_result == nullptr ||
        api_->fn.recognizer_set_max_alternatives == nullptr || api_->fn.set_log_level == nullptr) {
        SetError("Invalid Vosk library: required symbols are missing.");
        dlclose(api_->lib_handle);
        delete api_;
        api_ = nullptr;
        return false;
    }

    api_->fn.set_log_level(-1);
    return true;
}

bool VoskAsrEngine::Init(const char *model_cn_path, const char *model_en_path)
{
    Close();
    last_error_.clear();

    if (!LoadApi()) {
        return false;
    }

    if (PathExists(model_cn_path)) {
        model_cn_ = api_->fn.model_new(model_cn_path);
        if (model_cn_ == nullptr) {
            SetError(std::string("Failed to load Chinese ASR model: ") + model_cn_path);
        }
    }

    if (PathExists(model_en_path)) {
        model_en_ = api_->fn.model_new(model_en_path);
        if (model_en_ == nullptr) {
            if (!last_error_.empty()) {
                last_error_ += "; ";
            }
            last_error_ += std::string("Failed to load English ASR model: ") + model_en_path;
        }
    }

    enabled_ = (model_cn_ != nullptr || model_en_ != nullptr);
    if (!enabled_ && last_error_.empty()) {
        SetError("No available ASR models. Set VOSK_MODEL_CN / VOSK_MODEL_EN.");
    }
    return enabled_;
}

void VoskAsrEngine::Close()
{
    enabled_ = false;

    if (api_ != nullptr && api_->fn.model_free != nullptr) {
        if (model_cn_ != nullptr) {
            api_->fn.model_free((VoskModel *)model_cn_);
        }
        if (model_en_ != nullptr) {
            api_->fn.model_free((VoskModel *)model_en_);
        }
    }
    model_cn_ = nullptr;
    model_en_ = nullptr;

    if (api_ != nullptr) {
        if (api_->lib_handle != nullptr) {
            dlclose(api_->lib_handle);
        }
        delete api_;
        api_ = nullptr;
    }
}

bool VoskAsrEngine::IsEnabled() const
{
    return enabled_;
}

const std::string &VoskAsrEngine::LastError() const
{
    return last_error_;
}

AsrResult VoskAsrEngine::TranscribeFloatMono16k(const float *data, int num_frames)
{
    AsrResult out;
    out.enabled = enabled_;

    if (!enabled_) {
        out.error = last_error_.empty() ? "ASR disabled" : last_error_;
        return out;
    }
    if (data == nullptr || num_frames <= 0) {
        out.ok = true;
        out.text = "";
        out.lang = "";
        out.confidence = 0.0;
        return out;
    }

    std::vector<int16_t> pcm = FloatToPcm16(data, num_frames);

    auto run_model = [&](void *model_ptr, const char *lang_tag) -> AsrResult {
        AsrResult one;
        one.enabled = true;
        one.lang = lang_tag;

        if (model_ptr == nullptr) {
            one.error = "model unavailable";
            return one;
        }

        VoskRecognizer *rec = api_->fn.recognizer_new((VoskModel *)model_ptr, 16000.0f);
        if (rec == nullptr) {
            one.error = "failed to create recognizer";
            return one;
        }

        api_->fn.recognizer_set_max_alternatives(rec, 1);

        const int kChunkBytes = 4000;
        const int total_bytes = static_cast<int>(pcm.size() * sizeof(int16_t));
        const char *raw = reinterpret_cast<const char *>(pcm.data());
        int offset = 0;
        while (offset < total_bytes) {
            int take = std::min(kChunkBytes, total_bytes - offset);
            int rc = api_->fn.recognizer_accept_waveform(rec, raw + offset, take);
            if (rc < 0) {
                api_->fn.recognizer_free(rec);
                one.error = "accept_waveform failed";
                return one;
            }
            offset += take;
        }

        const char *json_cstr = api_->fn.recognizer_final_result(rec);
        std::string json = json_cstr ? json_cstr : "";
        api_->fn.recognizer_free(rec);

        one.text = ExtractJsonStringField(json, "\"text\"");
        one.confidence = ExtractJsonNumberField(json, "\"confidence\"");
        if (!std::isfinite(one.confidence)) {
            one.confidence = one.text.empty() ? 0.0 : 1e-6;
        }
        one.ok = true;
        return one;
    };

    AsrResult cn = run_model(model_cn_, "cn");
    AsrResult en = run_model(model_en_, "en");

    if (cn.ok && en.ok) {
        out = (en.confidence > cn.confidence) ? en : cn;
    } else if (cn.ok) {
        out = cn;
    } else if (en.ok) {
        out = en;
    } else {
        out.ok = false;
        out.enabled = true;
        out.error = "Both CN and EN ASR failed";
    }
    return out;
}
