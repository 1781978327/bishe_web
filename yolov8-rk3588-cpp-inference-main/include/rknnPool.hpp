#ifndef _RKNNPOOL_HPP
#define _RKNNPOOL_HPP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <chrono>
#include "opencv2/opencv.hpp"
#include "opencv2/highgui.hpp"
#include "postprocess.h"
#include "rk_common.h"
#include "rknn_api.h"
#include "RgaUtils.h"
#include "im2d.h"
#include "rga.h"
#include "deepsort/include/deepsort.h"
#include "deepsort/include/track.h"
#include "bytetrack/include/BYTETracker.h"
#include <mutex>
#include <cmath>

// forward declaration
class DeepSort;
class Track;

/*
 * YAML 类别配置文件格式：
 *   classes:
 *     - name: person
 *       visible: true
 *     - name: fall
 *       visible: false
 * 文件放在模型同目录，如 model/RK3588/yolov8s.rknn → model/RK3588/classes.yaml
 * visible: true/false 控制该类是否在推理框中显示（推理始终执行）
 * 不需要外部依赖。
 */
static std::vector<std::string> parse_classes_yaml(const char* model_path,
                                                    std::vector<bool>* out_visible = nullptr) {
    std::vector<std::string> labels;
    char yaml_path[512];
    const char* last_slash = nullptr;
    for (const char* p = model_path; *p; ++p)
        if (*p == '/') last_slash = p;
    if (last_slash) {
        size_t base_len = last_slash - model_path;
        strncpy(yaml_path, model_path, base_len);
        yaml_path[base_len] = '\0';
        strcat(yaml_path, "/classes.yaml");
    } else {
        strcpy(yaml_path, "classes.yaml");
    }

    FILE* fp = fopen(yaml_path, "r");
    if (!fp) {
        printf("警告: 未找到类别配置 %s，将使用默认标签\n", yaml_path);
        return labels;
    }
    printf("从 %s 加载类别配置\n", yaml_path);

    char line[256];
    bool in_classes = false;

    // 当前正在解析的条目
    std::string cur_name;
    bool cur_visible = true;
    bool cur_has_name = false;

    while (fgets(line, sizeof(line), fp)) {
        // 去除行尾换行和空格
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' ' || line[len-1] == '\t')) {
            line[--len] = '\0';
        }

        // 跳过空行
        if (line[0] == '\0') continue;

        // 跳过注释行
        if (line[0] == '#') continue;

        // 找冒号位置（必须在 "classes" 之后）
        const char* colon_ptr = strchr(line, ':');
        const char* first_content = line;
        while (*first_content == ' ' || *first_content == '\t') ++first_content;

        // 真正的 classes: 行：行首非特殊字符 + 冒号前是 classes
        if (!in_classes) {
            if (colon_ptr && strncmp(first_content, "classes", 7) == 0 &&
                first_content + 7 == colon_ptr) {
                in_classes = true;
            }
            continue;
        }

        // 行内容指针，去掉前导空格
        const char* ptr = line;
        while (*ptr == ' ' || *ptr == '\t') ++ptr;

        // 空行
        if (*ptr == '\0') continue;

        // 该行前导空格数（缩进级别）
        int indent = ptr - line;

        // 找原始行第一个非空格字符
        char raw_first = line[0];
        if (raw_first == ' ' || raw_first == '\t') {
            const char* rp = line + 1;
            while (*rp == ' ' || *rp == '\t') ++rp;
            raw_first = *rp;
        }

        // raw_first == '-'  → 列表项，indent ≥ 1
        // raw_first == '\0' → 空行（已跳过）
        // 其他字母且 indent == 0 → 真正的顶层 key（如 `fall:`），离开
        if (raw_first != '-' && raw_first != '\0' && indent == 0) break;
        // 新列表项开始：先把前一项保存，再重置
        if (ptr[0] == '-') {
            if (cur_has_name) {
                labels.push_back(cur_name);
                if (out_visible) out_visible->push_back(cur_visible);
            }
            cur_name.clear();
            cur_visible = true;
            cur_has_name = false;
            ptr++;
            while (*ptr == ' ' || *ptr == '\t') ++ptr;
        }

        // 解析 key: value
        const char* colon = strchr(ptr, ':');
        if (!colon) continue;
        size_t key_len = colon - ptr;
        if (strncmp(ptr, "name", key_len) == 0) {
            const char* val = colon + 1;
            while (*val == ' ' || *val == '\t') ++val;
            cur_name = val;
            cur_has_name = true;
        } else if (strncmp(ptr, "visible", key_len) == 0) {
            const char* val = colon + 1;
            while (*val == ' ' || *val == '\t') ++val;
            cur_visible = (strncmp(val, "true", 4) == 0 || strncmp(val, "1", 1) == 0);
        }
    }

    // 保存最后一项
    if (cur_has_name) {
        labels.push_back(cur_name);
        if (out_visible) out_visible->push_back(cur_visible);
    }

    fclose(fp);
    return labels;
}

// 全局可见性数组，parse 后填充
static std::vector<bool> class_visible;
static std::mutex g_tracker_mutex;
static long long g_bt_frame_count = 0;
static int g_ds_skip_frames = 0;  // 0=每帧都跟踪, 1=跳1帧(每2帧1次), 2=跳2帧(每3帧1次)

// 全局 fallback 标签
static const char* coco_labels_fall_person[] = { "fall", "person" };
static const char* coco_labels_person[] = { "person" };
static const char** coco_labels = coco_labels_fall_person;
static int coco_class_num = 2;

class rknn_lite {
private:
    rknn_app_context_t app_ctx;
    int ret;

public:
    cv::Mat ori_img;
    DeepSort* tracker;
    BYTETracker* bt_tracker;
    int slot_idx;
    long long bt_frame_count;
    int bt_fps;

    // DeepSort 隔帧跟踪状态（每个 slot 独立）
    int ds_frame_counter;
    std::vector<Track> last_ds_tracks;
    std::vector<DetectBox> last_detections;

    rknn_lite(char* model_path, int core_id, DeepSort* ds_tracker = nullptr,
              BYTETracker* bt = nullptr, int slot_idx_ = 0, int fps = 25);
    ~rknn_lite();
    int interf();
    int RGA_bgr_to_rgb(const cv::Mat& bgr_image, cv::Mat &rgb_image);
    int RGA_resize(const cv::Mat& src, cv::Mat& dst, int dst_width, int dst_height);
};

// 构造函数：初始化RKNN模型
rknn_lite::rknn_lite(char* model_path, int core_id, DeepSort* ds_tracker,
                     BYTETracker* bt, int slot_idx_, int fps)
    : tracker(ds_tracker), bt_tracker(bt), slot_idx(slot_idx_), bt_frame_count(0), bt_fps(fps),
      ds_frame_counter(0) {
    memset(&app_ctx, 0, sizeof(rknn_app_context_t));

    // 加载模型文件
    int model_data_size = 0;
    unsigned char* model_data = load_model(model_path, model_data_size);
    
    // 初始化RKNN上下文
    ret = rknn_init(&app_ctx.rknn_ctx, model_data, model_data_size, 0, NULL);
    free(model_data);
    if (ret < 0) {
        printf("rknn_init 错误 ret=%d\n", ret);
        exit(-1);
    }

    // 设置NPU核心掩码
    rknn_core_mask core_mask;
    switch(core_id % 3) {
        case 0: core_mask = RKNN_NPU_CORE_0; break;
        case 1: core_mask = RKNN_NPU_CORE_1; break;
        default: core_mask = RKNN_NPU_CORE_2;
    }
    ret = rknn_set_core_mask(app_ctx.rknn_ctx, core_mask);
    if (ret < 0) {
        printf("rknn_set_core_mask 错误 ret=%d\n", ret);
        exit(-1);
    }

    // 获取模型输入输出信息
    ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_IN_OUT_NUM, &app_ctx.io_num, sizeof(app_ctx.io_num));
    if (ret < 0) {
        printf("rknn_query io_num 错误 ret=%d\n", ret);
        exit(-1);
    }

    // 获取输入属性
    app_ctx.input_attrs = new rknn_tensor_attr[app_ctx.io_num.n_input];
    memset(app_ctx.input_attrs, 0, sizeof(rknn_tensor_attr) * app_ctx.io_num.n_input);
    for (uint32_t i = 0; i < app_ctx.io_num.n_input; i++) {
        app_ctx.input_attrs[i].index = i;
        ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_INPUT_ATTR, &(app_ctx.input_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_query input_attrs 错误 ret=%d\n", ret);
            exit(-1);
        }
    }

    // 获取输出属性
    app_ctx.output_attrs = new rknn_tensor_attr[app_ctx.io_num.n_output];
    memset(app_ctx.output_attrs, 0, sizeof(rknn_tensor_attr) * app_ctx.io_num.n_output);
    for (uint32_t i = 0; i < app_ctx.io_num.n_output; i++) {
        app_ctx.output_attrs[i].index = i;
        ret = rknn_query(app_ctx.rknn_ctx, RKNN_QUERY_OUTPUT_ATTR, &(app_ctx.output_attrs[i]), sizeof(rknn_tensor_attr));
        if (ret < 0) {
            printf("rknn_query output_attrs 错误 ret=%d\n", ret);
            exit(-1);
        }
    }

    // 动态检测类别数：从 score 输出维度推断（YOLOv8: [1, 4+class_num, h, w]）
    // score 输出是 box_idx+1，维度顺序为 [n, (4+cls), h, w]
    int output_per_branch = app_ctx.io_num.n_output / 3;
    int score_idx = output_per_branch == 3 ? 1 : 0;  // box, score[, score_sum]
    int class_num = app_ctx.output_attrs[score_idx].dims[1] - 4;
    if (class_num < 1) class_num = 1;
    app_ctx.class_num = class_num;

    // 优先从模型同目录的 classes.yaml 加载类别名称
    std::vector<std::string> yaml_labels = parse_classes_yaml(model_path, &class_visible);
    if (!yaml_labels.empty()) {
        static std::vector<std::string> s_labels_store;
        s_labels_store = yaml_labels;
        static const char* s_labels[64];
        for (size_t i = 0; i < s_labels_store.size(); ++i)
            s_labels[i] = s_labels_store[i].c_str();
        coco_labels = s_labels;
        coco_class_num = (int)s_labels_store.size();
        // 如果 YAML 条目数少于模型 class_num，剩余默认可见
        while ((int)class_visible.size() < coco_class_num)
            class_visible.push_back(true);
        printf("从 YAML 加载类别配置 (class_num=%d):", coco_class_num);
        for (int i = 0; i < coco_class_num; ++i)
            printf(" [%d:%s %s]", i, coco_labels[i], class_visible[i] ? "show" : "hide");
        printf("\n");
    } else if (class_num == 1) {
        coco_labels = coco_labels_person;
        coco_class_num = 1;
        class_visible.assign(1, true);
        printf("检测到单类模型: person (class_num=1)\n");
    } else {
        coco_labels = coco_labels_fall_person;
        coco_class_num = 2;
        class_visible.assign(2, true);
        printf("检测到多类模型: fall, person (class_num=%d)\n", class_num);
    }

    // 检查量化类型
    if (app_ctx.output_attrs[0].qnt_type == RKNN_TENSOR_QNT_AFFINE_ASYMMETRIC && 
        app_ctx.output_attrs[0].type != RKNN_TENSOR_FLOAT16) {
        app_ctx.is_quant = true;
    } else {
        app_ctx.is_quant = false;
    }

    // 设置模型维度
    if (app_ctx.input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_height = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[3];
    } else {
        app_ctx.model_height = app_ctx.input_attrs[0].dims[1];
        app_ctx.model_width = app_ctx.input_attrs[0].dims[2];
        app_ctx.model_channel = app_ctx.input_attrs[0].dims[3];
    }
}

// 析构函数：释放资源
rknn_lite::~rknn_lite() {
    if (app_ctx.rknn_ctx != 0) {
        rknn_destroy(app_ctx.rknn_ctx);
    }
    if (app_ctx.input_attrs != nullptr) {
        delete[] app_ctx.input_attrs;
    }
    if (app_ctx.output_attrs != nullptr) {
        delete[] app_ctx.output_attrs;
    }
}

// BGR转RGB函数（使用RGA加速）
int rknn_lite::RGA_bgr_to_rgb(const cv::Mat& bgr_image, cv::Mat &rgb_image) {
    // 创建输出图像
    rgb_image.create(bgr_image.size(), bgr_image.type());
    
    rga_buffer_t src_img, dst_img;
    memset(&src_img, 0, sizeof(src_img));
    memset(&dst_img, 0, sizeof(dst_img));

    // 设置输入输出参数
    int src_width = bgr_image.cols;
    int src_height = bgr_image.rows;
    int dst_width = rgb_image.cols;
    int dst_height = rgb_image.rows;

    // 设置图像格式
    int src_format = RK_FORMAT_BGR_888;
    int dst_format = RK_FORMAT_RGB_888;

    // 包装图像数据到RGA缓冲区
    src_img = wrapbuffer_virtualaddr((void *)bgr_image.data, src_width, src_height, src_format);
    dst_img = wrapbuffer_virtualaddr((void *)rgb_image.data, dst_width, dst_height, dst_format);

    // 执行颜色空间转换
    IM_STATUS status = imcvtcolor(src_img, dst_img, src_format, dst_format);
    if (status != IM_STATUS_SUCCESS) {
        fprintf(stderr, "RGA BGR转RGB错误: %s\n", imStrError(status));
        return -1;
    }
    
    return 0;
}

// 图像缩放函数（使用RGA加速）
int rknn_lite::RGA_resize(const cv::Mat& src, cv::Mat& dst, int dst_width, int dst_height) {
    // 创建目标图像
    dst.create(dst_height, dst_width, src.type());
    
    rga_buffer_t src_img, dst_img;
    im_rect src_rect, dst_rect;
    
    memset(&src_img, 0, sizeof(src_img));
    memset(&dst_img, 0, sizeof(dst_img));
    memset(&src_rect, 0, sizeof(src_rect));
    memset(&dst_rect, 0, sizeof(dst_rect));

    // 设置图像格式
    int format = RK_FORMAT_RGB_888;  

    // 包装图像数据
    src_img = wrapbuffer_virtualaddr((void*)src.data, src.cols, src.rows, format);
    dst_img = wrapbuffer_virtualaddr((void*)dst.data, dst.cols, dst.rows, format);

    // 执行缩放操作
    IM_STATUS status = imresize(src_img, dst_img);
    if (status != IM_STATUS_SUCCESS) {
        fprintf(stderr, "RGA缩放错误: %s\n", imStrError(status));
        return -1;
    }
    
    return 0;
}

// 推理接口函数
int rknn_lite::interf() {
    cv::Mat img;
    // 使用RGA进行BGR到RGB转换
    if (RGA_bgr_to_rgb(ori_img, img) != 0) {
        printf("RGA BGR转RGB失败，回退到OpenCV\n");
        return -1;
    }
    
    int img_width = img.cols;
    int img_height = img.rows;
    
    // 准备输入张量
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].size = app_ctx.model_width * app_ctx.model_height * app_ctx.model_channel;
    
    // 使用RGA进行图像缩放
    cv::Mat resized_img;
    void* buf = nullptr;
    if (img_width != app_ctx.model_width || img_height != app_ctx.model_height) {
        if (RGA_resize(img, resized_img, app_ctx.model_width, app_ctx.model_height) != 0) {
            printf("RGA缩放失败，回退到OpenCV\n");
            cv::resize(img, resized_img, cv::Size(app_ctx.model_width, app_ctx.model_height));
        }
        buf = (void*)resized_img.data;
    } else {
        buf = (void*)img.data;
    }
    inputs[0].buf = buf;
    
    // 设置输入
    ret = rknn_inputs_set(app_ctx.rknn_ctx, app_ctx.io_num.n_input, inputs);
    if (ret < 0) {
        printf("rknn_inputs_set 错误 ret=%d\n", ret);
        return -1;
    }
    
    // 准备输出
    rknn_output outputs[app_ctx.io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    for (uint32_t i = 0; i < app_ctx.io_num.n_output; i++) {
        outputs[i].index = i;
        outputs[i].want_float = (!app_ctx.is_quant);
    }
    
    // 执行推理
    ret = rknn_run(app_ctx.rknn_ctx, nullptr);
    ret = rknn_outputs_get(app_ctx.rknn_ctx, app_ctx.io_num.n_output, outputs, NULL);
    
    // 后处理
    float scale_w = (float)app_ctx.model_width / img_width;
    float scale_h = (float)app_ctx.model_height / img_height;
    
    object_detect_result_list od_results;
    post_process(&app_ctx, outputs, BOX_THRESH, NMS_THRESH, scale_w, scale_h, &od_results);

    char text[256];

    // DeepSORT 跟踪（如果 tracker 已初始化）
    if (tracker != nullptr && od_results.count > 0) {
        vector<DetectBox> detections;
        for (int i = 0; i < od_results.count; i++) {
            object_detect_result* det_result = &(od_results.results[i]);
            DetectBox box;
            box.x1 = det_result->box.left;
            box.y1 = det_result->box.top;
            box.x2 = det_result->box.right;
            box.y2 = det_result->box.bottom;
            box.confidence = det_result->prop;
            box.classID = det_result->cls_id;
            detections.push_back(box);
        }

        std::vector<Track> confirmed_tracks;
        bool do_track = (g_ds_skip_frames == 0 || (ds_frame_counter % (g_ds_skip_frames + 1)) == 0);

        if (do_track) {
            tracker->sort(img, detections);
            last_ds_tracks = tracker->get_confirmed_tracks();
            last_detections = detections;  // 保存当前帧跟踪结果，供下一跳帧使用
        }
        ds_frame_counter++;

        // ---------- 绘制 ----------
        if (do_track) {
            // 完整跟踪帧：绘制轨迹 + 所有框
            for (const Track& t : last_ds_tracks) {
                const auto& traj = t.get_trajectory();
                if (traj.size() < 2) continue;
                int cls_id = t.cls;
                if (cls_id >= 0 && cls_id < (int)class_visible.size() && !class_visible[cls_id])
                    continue;
                for (size_t k = 1; k < traj.size(); k++) {
                    cv::line(ori_img,
                        cv::Point((int)traj[k-1].first, (int)traj[k-1].second),
                        cv::Point((int)traj[k].first,   (int)traj[k].second),
                        cv::Scalar(0, 255, 0), 2);
                }
                cv::circle(ori_img,
                    cv::Point((int)traj.front().first, (int)traj.front().second),
                    3, cv::Scalar(0, 255, 0), -1);
            }
            // 绘制跟踪框
            for (const auto& det : detections) {
                int cls_id = (int)det.classID;
                if (cls_id >= 0 && cls_id < (int)class_visible.size() && !class_visible[cls_id])
                    continue;
                int x1 = (int)det.x1, y1 = (int)det.y1;
                int x2 = (int)det.x2, y2 = (int)det.y2;
                int track_id = (int)det.trackID;
                cv::rectangle(ori_img, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0), 2);
                const char* cls_name = (cls_id >= 0 && cls_id < coco_class_num) ? coco_labels[cls_id] : "unknown";
                sprintf(text, "ID:%d %s %.1f%%", track_id, cls_name, det.confidence * 100);
                int baseLine = 0;
                cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
                int x = x1, y = y1 - label_size.height - baseLine;
                if (y < 0) y = 0;
                if (x + label_size.width > ori_img.cols) x = ori_img.cols - label_size.width;
                cv::rectangle(ori_img, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)), cv::Scalar(255, 255, 255), -1);
                cv::putText(ori_img, text, cv::Point(x, y + label_size.height), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));
            }
        } else {
            // 跳帧：用 IoU 匹配复用上一帧跟踪结果的 ID
            for (auto& det : detections) {
                det.trackID = -1;
            }
            if (!last_detections.empty()) {
                // IoU 匹配：当前检测框 vs 上一帧跟踪框
                for (auto& cur : detections) {
                    float best_iou = 0.25f;  // IoU 阈值，太低不匹配
                    int matched_id = -1;
                    for (const auto& prev : last_detections) {
                        // 计算 IoU
                        float xx1 = std::max(cur.x1, prev.x1);
                        float yy1 = std::max(cur.y1, prev.y1);
                        float xx2 = std::min(cur.x2, prev.x2);
                        float yy2 = std::min(cur.y2, prev.y2);
                        float inter_w = std::max(0.0f, xx2 - xx1);
                        float inter_h = std::max(0.0f, yy2 - yy1);
                        float inter = inter_w * inter_h;
                        float area1 = (cur.x2 - cur.x1) * (cur.y2 - cur.y1);
                        float area2 = (prev.x2 - prev.x1) * (prev.y2 - prev.y1);
                        float uni = area1 + area2 - inter;
                        float iou = (uni > 0.0f) ? (inter / uni) : 0.0f;
                        if (iou > best_iou) {
                            best_iou = iou;
                            matched_id = (int)prev.trackID;
                        }
                    }
                    if (matched_id >= 0) {
                        cur.trackID = (float)matched_id;
                    }
                }
            }
            // 绘制跳帧检测框（有匹配到 ID 才画）
            for (const auto& det : detections) {
                if ((int)det.trackID < 0) continue;
                int cls_id = (int)det.classID;
                if (cls_id >= 0 && cls_id < (int)class_visible.size() && !class_visible[cls_id])
                    continue;
                int x1 = (int)det.x1, y1 = (int)det.y1;
                int x2 = (int)det.x2, y2 = (int)det.y2;
                cv::rectangle(ori_img, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0), 2);
                const char* cls_name = (cls_id >= 0 && cls_id < coco_class_num) ? coco_labels[cls_id] : "unknown";
                sprintf(text, "ID:%d %s %.1f%%", (int)det.trackID, cls_name, det.confidence * 100);
                int baseLine = 0;
                cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
                int x = x1, y = y1 - label_size.height - baseLine;
                if (y < 0) y = 0;
                if (x + label_size.width > ori_img.cols) x = ori_img.cols - label_size.width;
                cv::rectangle(ori_img, cv::Rect(cv::Point(x, y), cv::Size(label_size.width, label_size.height + baseLine)), cv::Scalar(255, 255, 255), -1);
                cv::putText(ori_img, text, cv::Point(x, y + label_size.height), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));
            }
            // 不画绿线，因为跳帧时位置不连续
        }
    } else if (bt_tracker != nullptr) {
        // -------- ByteTrack 路径 --------
        vector<Object> objects;
        for (int i = 0; i < od_results.count; i++) {
            object_detect_result* det_result = &(od_results.results[i]);
            Object obj;
            obj.rect.x      = det_result->box.left;
            obj.rect.y      = det_result->box.top;
            obj.rect.width  = det_result->box.right  - det_result->box.left;
            obj.rect.height = det_result->box.bottom - det_result->box.top;
            obj.prob        = det_result->prop;
            obj.label       = det_result->cls_id;
            int cid         = det_result->cls_id;
            obj.name        = (cid >= 0 && cid < coco_class_num) ? coco_labels[cid] : "unknown";
            objects.push_back(obj);
        }
        int fps_val = (bt_fps > 0) ? bt_fps : 25;
        vector<STrack> tracked;
        // 注意：不要在这里加锁！调用者(main.cc)已经加了 g_tracker_mutex
        ++g_bt_frame_count;
        tracked = bt_tracker->update(objects, fps_val, g_bt_frame_count);

        // 绘制绿色运动轨迹
        for (const STrack& t : tracked) {
            if (!t.is_activated) continue;
            int cls_id = t.label;
            if (cls_id >= 0 && cls_id < (int)class_visible.size() && !class_visible[cls_id])
                continue;
            const auto& traj = t.get_trajectory();
            if (traj.size() >= 2) {
                for (size_t k = 1; k < traj.size(); k++) {
                    cv::line(ori_img,
                        cv::Point((int)traj[k-1].first, (int)traj[k-1].second),
                        cv::Point((int)traj[k].first,   (int)traj[k].second),
                        cv::Scalar(0, 255, 0), 2);
                }
                cv::circle(ori_img,
                    cv::Point((int)traj.front().first, (int)traj.front().second),
                    3, cv::Scalar(0, 255, 0), -1);
            }
        }

        for (const STrack& t : tracked) {
            if (!t.is_activated) continue;
            int cls_id = t.label;
            if (cls_id >= 0 && cls_id < (int)class_visible.size() && !class_visible[cls_id])
                continue;
            int x1 = (int)t.tlwh[0];
            int y1 = (int)t.tlwh[1];
            int x2 = (int)(t.tlwh[0] + t.tlwh[2]);
            int y2 = (int)(t.tlwh[1] + t.tlwh[3]);
            cv::Scalar color = bt_tracker->get_color(t.track_id);
            cv::rectangle(ori_img, cv::Point(x1, y1), cv::Point(x2, y2), color, 2);
            const char* cls_name = (cls_id >= 0 && cls_id < coco_class_num)
                                   ? coco_labels[cls_id] : "unknown";
            snprintf(text, sizeof(text), "ID:%d %s %.1f%% spd:%.0f",
                     t.track_id, cls_name, t.score * 100, t.speed_1s);
            int baseLine = 0;
            cv::Size sz = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            int tx = x1, ty = y1 - sz.height - baseLine;
            if (ty < 0) ty = 0;
            if (tx + sz.width > ori_img.cols) tx = ori_img.cols - sz.width;
            cv::rectangle(ori_img, cv::Rect(cv::Point(tx, ty),
                          cv::Size(sz.width, sz.height + baseLine)),
                          cv::Scalar(0, 0, 0), -1);
            cv::putText(ori_img, text, cv::Point(tx, ty + sz.height),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);
        }
    } else {
        // 原有绘制逻辑（无 tracker 时）
        for (int i = 0; i < od_results.count; i++) {
            object_detect_result* det_result = &(od_results.results[i]);
            int cls_id = det_result->cls_id;

            // YAML 中 visible=false 的类不绘制
            if (cls_id >= 0 && cls_id < (int)class_visible.size() && !class_visible[cls_id])
                continue;

            int x1 = det_result->box.left;
            int y1 = det_result->box.top;
            int x2 = det_result->box.right;
            int y2 = det_result->box.bottom;
            cv::rectangle(ori_img, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0));
            sprintf(text, "%s %.1f%%",
                    (cls_id >= 0 && cls_id < coco_class_num) ? coco_labels[cls_id] : "unknown",
                    det_result->prop * 100);
            int baseLine = 0;
            cv::Size label_size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            int x = x1;
            int y = y1 - label_size.height - baseLine;
            if (y < 0) y = 0;
            if (x + label_size.width > ori_img.cols) x = ori_img.cols - label_size.width;
            cv::rectangle(ori_img, cv::Rect(cv::Point(x, y),
                          cv::Size(label_size.width, label_size.height + baseLine)),
                          cv::Scalar(255, 255, 255), -1);
            cv::putText(ori_img, text, cv::Point(x, y + label_size.height),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));
        }
    }
    
    // 释放输出
    ret = rknn_outputs_release(app_ctx.rknn_ctx, app_ctx.io_num.n_output, outputs);
    return 0;
}

#endif // _RKNNPOOL_HPP