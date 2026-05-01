#ifndef _RKNN_POSTPROCESS_H_
#define _RKNN_POSTPROCESS_H_

#include <stdint.h>
#include <vector>
#include <string>
#include <utility>
#include "rknn_api.h"
#include "rk_common.h"

#define NMS_THRESH 0.45
#define BOX_THRESH 0.25
#define PROP_BOX_SIZE (5 + OBJ_CLASS_NUM)

struct DetectionResultItem {
    int label = -1;
    float score = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
};

struct TrackerResultItem {
    int track_id = -1;
    int label = -1;
    float score = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float x2 = 0.0f;
    float y2 = 0.0f;
    bool active = false;
    std::vector<std::pair<float, float>> trajectory;
};

int post_process(rknn_app_context_t *app_ctx, void *outputs, float conf_threshold, float nms_threshold, float scale_w, float scale_h, object_detect_result_list *od_results);

#endif //_RKNN_POSTPROCESS_H_
