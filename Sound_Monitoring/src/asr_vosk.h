#ifndef SOUND_MONITORING_ASR_VOSK_H_
#define SOUND_MONITORING_ASR_VOSK_H_

#include <string>

struct AsrResult {
    bool ok = false;
    bool enabled = false;
    std::string text;
    std::string lang;
    double confidence = 0.0;
    std::string error;
};

class VoskAsrEngine {
  public:
    VoskAsrEngine();
    ~VoskAsrEngine();

    bool Init(const char *model_cn_path, const char *model_en_path);
    void Close();

    bool IsEnabled() const;
    const std::string &LastError() const;

    // Input audio must be mono 16kHz float PCM in [-1, 1].
    AsrResult TranscribeFloatMono16k(const float *data, int num_frames);

  private:
    struct Api;
    Api *api_;
    void *model_cn_;
    void *model_en_;
    bool enabled_;
    std::string last_error_;

    bool LoadApi();
    void SetError(const std::string &msg);
};

#endif  // SOUND_MONITORING_ASR_VOSK_H_
