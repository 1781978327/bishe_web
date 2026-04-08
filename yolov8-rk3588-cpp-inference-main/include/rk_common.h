#ifndef _RK_COMMON_H_
#define _RK_COMMON_H_

#include "rknn_api.h"
#include <stdint.h>

#define OBJ_CLASS_NUM      2   // fall, person 2 类（class 0=fall, class 1=person）
#define OBJ_NUMB_MAX_SIZE  128

typedef struct {
    int left;
    int right;
    int top;
    int bottom;
} image_rect_t;

typedef struct {
    image_rect_t box;
    float prop;
    int cls_id;
} object_detect_result;

typedef struct {
    int id;
    int count;
    object_detect_result results[OBJ_NUMB_MAX_SIZE];
} object_detect_result_list;

#define DETECTBOX_DEFINED
typedef struct {
    float x1, y1, x2, y2;
    float confidence;
    float classID;
    float trackID;
} DetectBox;

typedef struct {
    rknn_context rknn_ctx;
    rknn_input_output_num io_num;
    rknn_tensor_attr* input_attrs;
    rknn_tensor_attr* output_attrs;
    int model_channel;
    int model_width;
    int model_height;
    int is_quant;
    int class_num;       // 动态检测的类别数
} rknn_app_context_t;

void dump_tensor_attr(rknn_tensor_attr* attr);
unsigned char* load_model(const char* filename, int& fileSize);

#endif //_RK_COMMON_H_
