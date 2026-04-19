#include "http_ctrl_camera_io.h"

#include <linux/videodev2.h>
#include <opencv2/imgproc.hpp>

#include "rga.h"

void release_camera_dmabuf_frame(v4l2_dmabuf::CaptureContext* dmabuf_cap,
                                 CameraDmabufFrameInfo* frame_info) {
    if (!frame_info) return;
    if (dmabuf_cap && dmabuf_cap->fd >= 0 && dmabuf_cap->streaming &&
        frame_info->valid && frame_info->index >= 0) {
        (void)v4l2_dmabuf::queue_buffer(dmabuf_cap, frame_info->index);
    }
    *frame_info = CameraDmabufFrameInfo{};
}

bool convert_camera_dmabuf_to_bgr(const v4l2_dmabuf::CaptureContext* dmabuf_cap,
                                  const CameraDmabufFrameInfo* frame_info,
                                  cv::Mat* frame_out) {
    if (!dmabuf_cap || !frame_info || !frame_out || !frame_info->valid || !frame_info->va) {
        if (frame_out) frame_out->release();
        return false;
    }
    if (frame_info->width <= 0 || frame_info->height <= 0) {
        frame_out->release();
        return false;
    }

    if (dmabuf_cap->pixfmt == V4L2_PIX_FMT_NV12) {
        size_t step = (size_t)(dmabuf_cap->bytesperline > 0 ? dmabuf_cap->bytesperline
                                                            : frame_info->width);
        cv::Mat nv12(frame_info->height + frame_info->height / 2,
                     frame_info->width,
                     CV_8UC1,
                     frame_info->va,
                     step);
        cv::cvtColor(nv12, *frame_out, cv::COLOR_YUV2BGR_NV12);
    } else if (dmabuf_cap->pixfmt == V4L2_PIX_FMT_UYVY) {
        size_t step = (size_t)(dmabuf_cap->bytesperline > 0 ? dmabuf_cap->bytesperline
                                                            : frame_info->width * 2);
        cv::Mat uyvy(frame_info->height, frame_info->width, CV_8UC2, frame_info->va, step);
        cv::cvtColor(uyvy, *frame_out, cv::COLOR_YUV2BGR_UYVY);
    } else {
        size_t step = (size_t)(dmabuf_cap->bytesperline > 0 ? dmabuf_cap->bytesperline
                                                            : frame_info->width * 2);
        cv::Mat yuyv(frame_info->height, frame_info->width, CV_8UC2, frame_info->va, step);
        cv::cvtColor(yuyv, *frame_out, cv::COLOR_YUV2BGR_YUYV);
    }

    return !frame_out->empty();
}

bool acquire_camera_frame(v4l2_dmabuf::CaptureContext* dmabuf_cap,
                          cv::VideoCapture* cv_cap,
                          cv::Mat* frame_out,
                          CameraDmabufFrameInfo* frame_info,
                          bool convert_to_bgr) {
    if (!frame_out) return false;
    if (frame_info) {
        *frame_info = CameraDmabufFrameInfo{};
    }

    if (dmabuf_cap && dmabuf_cap->fd >= 0 && dmabuf_cap->streaming) {
        int index = -1;
        int ret = v4l2_dmabuf::dequeue_buffer(dmabuf_cap, 100, &index);
        if (ret == 1 && index >= 0 && index < (int)dmabuf_cap->buffers.size()) {
            auto& buffer = dmabuf_cap->buffers[index];
            if (frame_info) {
                frame_info->valid = true;
                frame_info->index = index;
                frame_info->fd = buffer.fd;
                frame_info->va = buffer.va;
                frame_info->size = (int)buffer.size;
                frame_info->width = dmabuf_cap->width;
                frame_info->height = dmabuf_cap->height;
                frame_info->wstride = dmabuf_cap->wstride > 0 ? dmabuf_cap->wstride : dmabuf_cap->width;
                frame_info->hstride = dmabuf_cap->hstride > 0 ? dmabuf_cap->hstride : dmabuf_cap->height;
                if (dmabuf_cap->pixfmt == V4L2_PIX_FMT_NV12) {
                    frame_info->rga_format = RK_FORMAT_YCbCr_420_SP;
                } else if (dmabuf_cap->pixfmt == V4L2_PIX_FMT_UYVY) {
                    frame_info->rga_format = RK_FORMAT_UYVY_422;
                } else {
                    frame_info->rga_format = RK_FORMAT_YUYV_422;
                }
            }
            if (convert_to_bgr) {
                return convert_camera_dmabuf_to_bgr(dmabuf_cap, frame_info, frame_out);
            }
            frame_out->release();
            return frame_info ? frame_info->valid : true;
        }
        frame_out->release();
        return false;
    }

    if (cv_cap && cv_cap->isOpened()) {
        bool ok = cv_cap->read(*frame_out) && !frame_out->empty();
        if (!ok) frame_out->release();
        return ok;
    }

    frame_out->release();
    return false;
}

bool read_camera_frame(v4l2_dmabuf::CaptureContext* dmabuf_cap,
                       cv::VideoCapture* cv_cap,
                       cv::Mat* frame_out) {
    CameraDmabufFrameInfo frame_info;
    bool ok = acquire_camera_frame(dmabuf_cap, cv_cap, frame_out, &frame_info, true);
    release_camera_dmabuf_frame(dmabuf_cap, &frame_info);
    return ok;
}
