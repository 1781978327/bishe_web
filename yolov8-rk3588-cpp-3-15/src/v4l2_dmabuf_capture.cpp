#include "v4l2_dmabuf_capture.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#if __has_include(<linux/dma-heap.h>)
#include <linux/dma-heap.h>
#else
#ifndef DMA_HEAP_IOC_MAGIC
#define DMA_HEAP_IOC_MAGIC 'H'
struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)
#endif
#endif

namespace v4l2_dmabuf {
namespace {

int dma_buf_alloc(const std::string& heap_path, size_t size, int* fd_out, void** va_out) {
    int heap_fd = open(heap_path.c_str(), O_RDWR | O_CLOEXEC);
    if (heap_fd < 0) {
        return -1;
    }

    struct dma_heap_allocation_data alloc_data;
    memset(&alloc_data, 0, sizeof(alloc_data));
    alloc_data.len = size;
    alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
    alloc_data.heap_flags = 0;

    if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0) {
        close(heap_fd);
        return -1;
    }
    close(heap_fd);

    void* va = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, alloc_data.fd, 0);
    if (va == MAP_FAILED) {
        close(alloc_data.fd);
        return -1;
    }

    *fd_out = alloc_data.fd;
    *va_out = va;
    return 0;
}

void dma_buf_free(int fd, void* va, size_t size) {
    if (va && va != MAP_FAILED) {
        munmap(va, size);
    }
    if (fd >= 0) {
        close(fd);
    }
}

std::string fourcc_to_string(uint32_t v) {
    std::string s(4, ' ');
    s[0] = static_cast<char>(v & 0xFF);
    s[1] = static_cast<char>((v >> 8) & 0xFF);
    s[2] = static_cast<char>((v >> 16) & 0xFF);
    s[3] = static_cast<char>((v >> 24) & 0xFF);
    return s;
}

bool is_supported_pixfmt(uint32_t pixfmt) {
    return pixfmt == V4L2_PIX_FMT_NV12 ||
           pixfmt == V4L2_PIX_FMT_YUYV ||
           pixfmt == V4L2_PIX_FMT_UYVY;
}

uint32_t default_bytesperline_for_pixfmt(uint32_t pixfmt, uint32_t width) {
    if (pixfmt == V4L2_PIX_FMT_NV12) {
        return width;
    }
    return width * 2;
}

size_t minimum_sizeimage_for_pixfmt(uint32_t pixfmt, uint32_t bytesperline, uint32_t height) {
    if (pixfmt == V4L2_PIX_FMT_NV12) {
        return static_cast<size_t>(bytesperline) * static_cast<size_t>(height) * 3 / 2;
    }
    return static_cast<size_t>(bytesperline) * static_cast<size_t>(height);
}

int wstride_for_pixfmt(uint32_t pixfmt, uint32_t bytesperline, uint32_t width) {
    const uint32_t effective_bpl =
        bytesperline > 0 ? bytesperline : default_bytesperline_for_pixfmt(pixfmt, width);
    if (pixfmt == V4L2_PIX_FMT_NV12) {
        return static_cast<int>(effective_bpl);
    }
    return static_cast<int>(effective_bpl / 2);
}

int hstride_for_pixfmt(uint32_t pixfmt, size_t sizeimage, uint32_t bytesperline, uint32_t height) {
    if (bytesperline == 0 || sizeimage == 0) {
        return static_cast<int>(height);
    }

    if (pixfmt == V4L2_PIX_FMT_NV12) {
        const size_t denom = static_cast<size_t>(bytesperline) * 3;
        if (denom > 0) {
            const size_t aligned_height = (sizeimage * 2) / denom;
            if (aligned_height >= height) {
                return static_cast<int>(aligned_height);
            }
        }
        return static_cast<int>(height);
    }

    const size_t aligned_height = sizeimage / bytesperline;
    if (aligned_height >= height) {
        return static_cast<int>(aligned_height);
    }
    return static_cast<int>(height);
}

std::string errno_message(const std::string& prefix) {
    std::ostringstream oss;
    oss << prefix << ": errno=" << errno << " (" << std::strerror(errno) << ")";
    return oss.str();
}

void free_buffers(std::vector<DmaBuffer>* buffers) {
    if (!buffers) return;
    for (auto& b : *buffers) {
        dma_buf_free(b.fd, b.va, b.size);
        b.fd = -1;
        b.va = nullptr;
        b.size = 0;
    }
    buffers->clear();
}

void release_driver_buffers(int fd, int buf_type, int memory) {
    if (fd < 0 || buf_type == 0 || memory == 0) return;
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = static_cast<uint32_t>(buf_type);
    req.memory = static_cast<uint32_t>(memory);
    (void)ioctl(fd, VIDIOC_REQBUFS, &req);
}

int export_capture_buffer(int fd, int buf_type, int index, int plane, int* export_fd_out) {
    if (!export_fd_out) return -1;
    *export_fd_out = -1;

    struct v4l2_exportbuffer expbuf;
    memset(&expbuf, 0, sizeof(expbuf));
    expbuf.type = static_cast<uint32_t>(buf_type);
    expbuf.index = static_cast<uint32_t>(index);
    expbuf.plane = static_cast<uint32_t>(plane);
    expbuf.flags = O_CLOEXEC;

    if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) != 0) {
        return -1;
    }

    *export_fd_out = expbuf.fd;
    return 0;
}

int queue_imported_dmabuf_buffer(CaptureContext* cap, int index) {
    if (!cap || cap->fd < 0 || index < 0 || index >= (int)cap->buffers.size()) return -1;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = static_cast<uint32_t>(cap->buf_type);
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.index = static_cast<uint32_t>(index);

    if (cap->mplane) {
        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        memset(planes, 0, sizeof(planes));
        buf.length = static_cast<uint32_t>(cap->num_planes);
        buf.m.planes = planes;
        planes[0].m.fd = cap->buffers[index].fd;
        planes[0].length = static_cast<uint32_t>(cap->plane0_size);
        planes[0].bytesused = static_cast<uint32_t>(cap->plane0_size);
    } else {
        buf.length = static_cast<uint32_t>(cap->plane0_size);
        buf.m.fd = cap->buffers[index].fd;
    }

    return ioctl(cap->fd, VIDIOC_QBUF, &buf);
}

int queue_mmap_buffer(CaptureContext* cap, int index) {
    if (!cap || cap->fd < 0 || index < 0 || index >= (int)cap->buffers.size()) return -1;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = static_cast<uint32_t>(cap->buf_type);
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = static_cast<uint32_t>(index);

    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    if (cap->mplane) {
        memset(planes, 0, sizeof(planes));
        buf.length = static_cast<uint32_t>(cap->num_planes);
        buf.m.planes = planes;
        planes[0].length = static_cast<uint32_t>(cap->buffers[index].size);
    } else {
        buf.length = static_cast<uint32_t>(cap->buffers[index].size);
    }

    return ioctl(cap->fd, VIDIOC_QBUF, &buf);
}

int init_imported_dmabuf_buffers(const CaptureConfig& config,
                                 CaptureContext* out,
                                 uint32_t request_count,
                                 std::string* err) {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = request_count;
    req.type = static_cast<uint32_t>(out->buf_type);
    req.memory = V4L2_MEMORY_DMABUF;
    if (ioctl(out->fd, VIDIOC_REQBUFS, &req) != 0 || req.count < 2) {
        if (err) *err = errno_message("VIDIOC_REQBUFS DMABUF failed");
        release_driver_buffers(out->fd, out->buf_type, V4L2_MEMORY_DMABUF);
        return -1;
    }

    out->memory = V4L2_MEMORY_DMABUF;
    out->buffers.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        auto& b = out->buffers[i];
        b.size = out->plane0_size;
        if (dma_buf_alloc(config.dma_heap, out->plane0_size, &b.fd, &b.va) != 0) {
            if (err) *err = errno_message("dma_buf_alloc failed");
            free_buffers(&out->buffers);
            release_driver_buffers(out->fd, out->buf_type, V4L2_MEMORY_DMABUF);
            out->memory = 0;
            return -1;
        }
        if (queue_imported_dmabuf_buffer(out, static_cast<int>(i)) != 0) {
            if (err) *err = errno_message("VIDIOC_QBUF DMABUF failed");
            free_buffers(&out->buffers);
            release_driver_buffers(out->fd, out->buf_type, V4L2_MEMORY_DMABUF);
            out->memory = 0;
            return -1;
        }
    }

    return 0;
}

int init_mmap_export_buffers(CaptureContext* out,
                             uint32_t request_count,
                             std::string* err) {
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = request_count;
    req.type = static_cast<uint32_t>(out->buf_type);
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(out->fd, VIDIOC_REQBUFS, &req) != 0 || req.count < 2) {
        if (err) *err = errno_message("VIDIOC_REQBUFS MMAP failed");
        release_driver_buffers(out->fd, out->buf_type, V4L2_MEMORY_MMAP);
        return -1;
    }

    out->memory = V4L2_MEMORY_MMAP;
    out->buffers.resize(req.count);

    for (uint32_t i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = static_cast<uint32_t>(out->buf_type);
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        struct v4l2_plane planes[VIDEO_MAX_PLANES];
        memset(planes, 0, sizeof(planes));
        if (out->mplane) {
            buf.length = static_cast<uint32_t>(out->num_planes);
            buf.m.planes = planes;
        }

        if (ioctl(out->fd, VIDIOC_QUERYBUF, &buf) != 0) {
            if (err) *err = errno_message("VIDIOC_QUERYBUF MMAP failed");
            free_buffers(&out->buffers);
            release_driver_buffers(out->fd, out->buf_type, V4L2_MEMORY_MMAP);
            out->memory = 0;
            return -1;
        }

        size_t map_len = 0;
        off_t map_offset = 0;
        if (out->mplane) {
            map_len = planes[0].length;
            map_offset = static_cast<off_t>(planes[0].m.mem_offset);
        } else {
            map_len = buf.length;
            map_offset = static_cast<off_t>(buf.m.offset);
        }

        void* va = mmap(nullptr, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, out->fd, map_offset);
        if (va == MAP_FAILED) {
            if (err) *err = errno_message("mmap capture buffer failed");
            free_buffers(&out->buffers);
            release_driver_buffers(out->fd, out->buf_type, V4L2_MEMORY_MMAP);
            out->memory = 0;
            return -1;
        }

        int export_fd = -1;
        if (export_capture_buffer(out->fd, out->buf_type, static_cast<int>(i), 0, &export_fd) != 0) {
            if (err) *err = errno_message("VIDIOC_EXPBUF failed");
            munmap(va, map_len);
            free_buffers(&out->buffers);
            release_driver_buffers(out->fd, out->buf_type, V4L2_MEMORY_MMAP);
            out->memory = 0;
            return -1;
        }

        out->buffers[i].fd = export_fd;
        out->buffers[i].va = va;
        out->buffers[i].size = map_len;

        if (queue_mmap_buffer(out, static_cast<int>(i)) != 0) {
            if (err) *err = errno_message("VIDIOC_QBUF MMAP failed");
            free_buffers(&out->buffers);
            release_driver_buffers(out->fd, out->buf_type, V4L2_MEMORY_MMAP);
            out->memory = 0;
            return -1;
        }
    }

    return 0;
}

}  // namespace

void close_capture(CaptureContext* cap) {
    if (!cap) return;
    if (cap->fd >= 0 && cap->streaming) {
        int type = cap->buf_type;
        ioctl(cap->fd, VIDIOC_STREAMOFF, &type);
    }
    cap->streaming = false;
    release_driver_buffers(cap->fd, cap->buf_type, cap->memory);
    if (cap->fd >= 0) {
        close(cap->fd);
        cap->fd = -1;
    }
    free_buffers(&cap->buffers);
    cap->memory = 0;
}

int queue_buffer(CaptureContext* cap, int index) {
    if (!cap || cap->fd < 0 || index < 0 || index >= (int)cap->buffers.size()) return -1;
    if (cap->memory == V4L2_MEMORY_MMAP) {
        return queue_mmap_buffer(cap, index);
    }
    return queue_imported_dmabuf_buffer(cap, index);
}

int open_capture(const CaptureConfig& config, CaptureContext* out, std::string* err) {
    if (!out) return -1;
    *out = CaptureContext{};

    int fd = open(config.device_name.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        if (err) *err = "open failed";
        return -1;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
        if (err) *err = "VIDIOC_QUERYCAP failed";
        close(fd);
        return -1;
    }

    uint32_t caps = cap.capabilities;
    if (caps & V4L2_CAP_DEVICE_CAPS) caps = cap.device_caps;
    if (!(caps & V4L2_CAP_STREAMING)) {
        if (err) *err = "device does not support streaming";
        close(fd);
        return -1;
    }

    int buf_type = -1;
    bool mplane = false;
    if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        mplane = true;
    } else if (caps & V4L2_CAP_VIDEO_CAPTURE) {
        buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mplane = false;
    } else {
        if (err) *err = "device has no VIDEO_CAPTURE capability";
        close(fd);
        return -1;
    }

    struct v4l2_format fmt;
    uint32_t requested_formats[3];
    int requested_format_count = 0;
    if (mplane) {
        requested_formats[requested_format_count++] = V4L2_PIX_FMT_NV12;
        requested_formats[requested_format_count++] = V4L2_PIX_FMT_UYVY;
        requested_formats[requested_format_count++] = V4L2_PIX_FMT_YUYV;
    } else {
        requested_formats[requested_format_count++] = V4L2_PIX_FMT_YUYV;
        requested_formats[requested_format_count++] = V4L2_PIX_FMT_UYVY;
        requested_formats[requested_format_count++] = V4L2_PIX_FMT_NV12;
    }

    bool fmt_ok = false;
    std::string fmt_err = "VIDIOC_S_FMT failed";
    uint32_t actual_pixfmt = 0;
    for (int i = 0; i < requested_format_count; ++i) {
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = buf_type;
        if (mplane) {
            fmt.fmt.pix_mp.width = static_cast<uint32_t>(config.width);
            fmt.fmt.pix_mp.height = static_cast<uint32_t>(config.height);
            fmt.fmt.pix_mp.pixelformat = requested_formats[i];
            fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
            fmt.fmt.pix_mp.num_planes = 1;
        } else {
            fmt.fmt.pix.width = static_cast<uint32_t>(config.width);
            fmt.fmt.pix.height = static_cast<uint32_t>(config.height);
            fmt.fmt.pix.pixelformat = requested_formats[i];
            fmt.fmt.pix.field = V4L2_FIELD_ANY;
        }

        if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
            fmt_err = "VIDIOC_S_FMT failed for " + fourcc_to_string(requested_formats[i]);
            continue;
        }

        actual_pixfmt = mplane ? fmt.fmt.pix_mp.pixelformat : fmt.fmt.pix.pixelformat;
        if (!is_supported_pixfmt(actual_pixfmt)) {
            fmt_err = "camera returned unsupported pixfmt=" + fourcc_to_string(actual_pixfmt);
            continue;
        }

        fmt_ok = true;
        break;
    }

    if (!fmt_ok) {
        if (err) *err = fmt_err;
        close(fd);
        return -1;
    }

    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = buf_type;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = static_cast<uint32_t>(std::max(1, config.fps));
    (void)ioctl(fd, VIDIOC_S_PARM, &parm);

    const uint32_t fmt_width = mplane ? fmt.fmt.pix_mp.width : fmt.fmt.pix.width;
    const uint32_t fmt_height = mplane ? fmt.fmt.pix_mp.height : fmt.fmt.pix.height;
    const uint32_t fmt_sizeimage =
        mplane ? fmt.fmt.pix_mp.plane_fmt[0].sizeimage : fmt.fmt.pix.sizeimage;
    const uint32_t fmt_bytesperline =
        mplane ? fmt.fmt.pix_mp.plane_fmt[0].bytesperline : fmt.fmt.pix.bytesperline;
    const uint32_t effective_bytesperline =
        fmt_bytesperline > 0 ? fmt_bytesperline : default_bytesperline_for_pixfmt(actual_pixfmt, fmt_width);
    const size_t plane0_size = std::max<size_t>(
        static_cast<size_t>(fmt_sizeimage),
        minimum_sizeimage_for_pixfmt(actual_pixfmt, effective_bytesperline, fmt_height));
    const int num_planes = mplane ? static_cast<int>(fmt.fmt.pix_mp.num_planes) : 1;
    if (num_planes != 1) {
        if (err) {
            *err = "unsupported multi-plane layout: pixfmt=" + fourcc_to_string(actual_pixfmt) +
                   ", num_planes=" + std::to_string(num_planes);
        }
        close(fd);
        return -1;
    }

    out->fd = fd;
    out->buf_type = buf_type;
    out->mplane = mplane;
    out->width = static_cast<int>(fmt_width);
    out->height = static_cast<int>(fmt_height);
    out->bytesperline = static_cast<int>(effective_bytesperline);
    out->wstride = wstride_for_pixfmt(actual_pixfmt, effective_bytesperline, fmt_width);
    out->hstride = hstride_for_pixfmt(actual_pixfmt, plane0_size, effective_bytesperline, fmt_height);
    out->pixfmt = actual_pixfmt;
    out->num_planes = num_planes;
    out->plane0_size = plane0_size;
    out->memory = 0;

    const uint32_t request_count = static_cast<uint32_t>(std::max(2, config.dma_buffers));
    std::string imported_err;
    if (init_imported_dmabuf_buffers(config, out, request_count, &imported_err) != 0) {
        std::string mmap_err;
        if (init_mmap_export_buffers(out, request_count, &mmap_err) != 0) {
            if (err) {
                *err = "import-dmabuf path failed [" + imported_err + "], mmap-expbuf path failed [" + mmap_err + "]";
            }
            close_capture(out);
            return -1;
        }
    }

    int type = buf_type;
    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        if (err) *err = errno_message("VIDIOC_STREAMON failed");
        close_capture(out);
        return -1;
    }

    out->streaming = true;
    return 0;
}

int dequeue_buffer(CaptureContext* cap, int timeout_ms, int* index_out) {
    if (!cap || cap->fd < 0 || !index_out) return -1;
    *index_out = -1;

    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = cap->fd;
    pfd.events = POLLIN | POLLERR;
    int pr = poll(&pfd, 1, timeout_ms);
    if (pr == 0) return 0;
    if (pr < 0) {
        if (errno == EINTR) return 0;
        return -1;
    }
    if (pfd.revents & POLLERR) return -1;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = static_cast<uint32_t>(cap->buf_type);
    buf.memory = static_cast<uint32_t>(cap->memory == V4L2_MEMORY_MMAP ? V4L2_MEMORY_MMAP
                                                                        : V4L2_MEMORY_DMABUF);

    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    if (cap->mplane) {
        memset(planes, 0, sizeof(planes));
        buf.length = static_cast<uint32_t>(cap->num_planes);
        buf.m.planes = planes;
    }

    if (ioctl(cap->fd, VIDIOC_DQBUF, &buf) != 0) {
        if (errno == EAGAIN || errno == EINTR) return 0;
        return -1;
    }
    if (buf.index >= cap->buffers.size()) return -1;
    *index_out = static_cast<int>(buf.index);
    return 1;
}

}  // namespace v4l2_dmabuf
