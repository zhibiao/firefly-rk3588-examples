#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <drm_fourcc.h>
#include <drm.h>
#include <drm_mode.h>

#include <linux/videodev2.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

int video_fd = -1;
struct v4l2_rect video_rect = {0};
uint32_t n_video_planes = 1;
uint32_t n_video_buffers = 2;
struct
{
    int dma_fd;
    struct v4l2_plane planes[1];
} video_buffers[4];

int drm_fd = -1;
uint32_t n_drm_buffers = 2;
struct
{
    uint32_t handle;
    uint32_t buf_id;
} drm_buffers[4];
uint32_t drm_connector_id = 219;
uint32_t drm_plane_id = 142;
uint32_t drm_crtc_id = 0;
struct v4l2_rect drm_crtc_rect = {0};

void open_hdmi_input(const char *dev_name)
{
    struct stat st;

    if (-1 == stat(dev_name, &st))
    {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n",
                dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (!S_ISCHR(st.st_mode))
    {
        fprintf(stderr, "%s is no devicen", dev_name);
        exit(EXIT_FAILURE);
    }

    video_fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);

    if (-1 == video_fd)
    {
        fprintf(stderr, "Cannot open '%s': %d, %s\n",
                dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct v4l2_capability cap;
    if (-1 == ioctl(video_fd, VIDIOC_QUERYCAP, &cap))
    {
        if (EINVAL == errno)
        {
            fprintf(stderr, "%s is no V4L2 device\n",
                    dev_name);
            exit(EXIT_FAILURE);
        }
        else
        {
            fprintf(stderr, "VIDIOC_STREAMOFF error %d, %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE))
    {
        fprintf(stderr, "%s is no video capture device\n",
                dev_name);
        exit(EXIT_FAILURE);
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING))
    {
        fprintf(stderr, "%s does not support streaming i/o\n",
                dev_name);
        exit(EXIT_FAILURE);
    }

    struct v4l2_format video_fmt;
    video_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (-1 == ioctl(video_fd, VIDIOC_G_FMT, &video_fmt))
    {
        fprintf(stderr, "VIDIOC_G_FMT error %d, %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (-1 == ioctl(video_fd, VIDIOC_S_FMT, &video_fmt))
    {
        fprintf(stderr, "VIDIOC_S_FMT error %d, %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    video_rect.width = video_fmt.fmt.pix_mp.width;
    video_rect.height = video_fmt.fmt.pix_mp.height;

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof req);
    req.count = n_video_buffers;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == ioctl(video_fd, VIDIOC_REQBUFS, &req))
    {
        if (EINVAL == errno)
        {
            fprintf(stderr, "%s does not support "
                            "memory dmabuf\n",
                    dev_name);
            exit(EXIT_FAILURE);
        }
        else
        {

            fprintf(stderr, "VIDIOC_REQBUFS error %d, %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    for (int n = 0; n < n_video_buffers; n++)
    {
        struct v4l2_exportbuffer expbuf;
        memset(&expbuf, 0, sizeof(expbuf));

        expbuf.index = n;
        expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        expbuf.plane = 0;
        if (ioctl(video_fd, VIDIOC_EXPBUF, &expbuf) == -1)
        {
            fprintf(stderr, "VIDIOC_EXPBUF error %d, %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
        video_buffers[n].dma_fd = expbuf.fd;
    }

    for (int n = 0; n < n_video_buffers; n++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(struct v4l2_buffer));

        buf.index = n;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.m.planes = video_buffers[n].planes;
        buf.length = n_video_planes;

        if (-1 == ioctl(video_fd, VIDIOC_QUERYBUF, &buf))
        {
            fprintf(stderr, "VIDIOC_QUERYBUF error %d, %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    for (int n = 0; n < n_video_buffers; n++)
    {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        buf.index = n;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = n_video_planes;
        buf.m.planes = video_buffers[n].planes;

        if (-1 == ioctl(video_fd, VIDIOC_QBUF, &buf))
        {
            fprintf(stderr, "VIDIOC_QBUF error %d, %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (-1 == ioctl(video_fd, VIDIOC_STREAMON, &type))
    {
        fprintf(stderr, "VIDIOC_STREAMON error %d, %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void open_drm_device(const char *module)
{
    drm_fd = drmOpen(module, NULL);
    if (-1 == drm_fd)
    {
        fprintf(stderr, "Cannot open '%s': %d, %s\n",
                module, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    drmModeRes *res = drmModeGetResources(drm_fd);
    if (res == NULL)
    {
        fprintf(stderr, "drmPrimeFDToHandle error %d, %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    int j;
    for (j = 0; j < res->count_connectors; j++)
    {
        drmModeConnector *con =
            drmModeGetConnector(drm_fd, res->connectors[j]);
        if (con->connector_id == drm_connector_id)
        {
            if (con->encoder_id)
            {
                drmModeEncoder *enc = drmModeGetEncoder(drm_fd, con->encoder_id);
                if (enc->crtc_id)
                {
                    drmModeCrtc *crtc = drmModeGetCrtc(drm_fd, enc->crtc_id);
                    drm_crtc_id = crtc->crtc_id;
                    drm_crtc_rect.width = crtc->width;
                    drm_crtc_rect.height = crtc->height;
                    drm_crtc_rect.left = crtc->x;
                    drm_crtc_rect.top = crtc->y;
                    break;
                }
            }
        }
    }
    drmModeFreeResources(res);

    if (j == res->count_connectors)
    {
    }

    drmModePlaneRes *planes = drmModeGetPlaneResources(drm_fd);
    int k;
    for (k = 0; k < planes->count_planes; k++)
    {
        drmModePlane *plane = drmModeGetPlane(drm_fd, planes->planes[k]);
        if (plane->plane_id == drm_plane_id)
        {
            break;
        }
    }
    drmModeFreePlaneResources(planes);

    if (k == planes->count_planes)
    {
    }

    printf("connector_id=%d, crtc_id=%d (%d,%d,%d,%d), plane_id=%d \n",
           drm_connector_id,
           drm_crtc_id,
           drm_crtc_rect.top,
           drm_crtc_rect.left,
           drm_crtc_rect.width,
           drm_crtc_rect.height,
           drm_plane_id);

    for (int n = 0; n < n_video_buffers; n++)
    {
        if (drmPrimeFDToHandle(drm_fd,
                               video_buffers[n].dma_fd,
                               &drm_buffers[n].handle))
        {
            fprintf(stderr, "drmPrimeFDToHandle error(n=%d) %d, %s\n", n, errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    for (int n = 0; n < n_drm_buffers; n++)
    {
        uint32_t width = video_rect.width,
                 height = video_rect.height,
                 bpp = 24,
                 size = width * height * 3,
                 pitch = width * 3,
                 pixel_format = DRM_FORMAT_RGB888;

        uint32_t handles[4] = {0},
                 pitches[4] = {0},
                 offsets[4] = {0};

        handles[0] = drm_buffers[n].handle;
        pitches[0] = pitch;

        if (drmModeAddFB2(drm_fd,
                          width,
                          height,
                          pixel_format,
                          handles,
                          pitches,
                          offsets,
                          &drm_buffers[n].buf_id,
                          0))
        {
            fprintf(stderr, "drmModeAddFB2 error(n=%d) %d, %s\n", n, errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char *argv[])
{
    open_hdmi_input("/dev/video11");

    open_drm_device("rockchip");

    struct pollfd fds[] = {
        {.fd = video_fd, .events = POLLIN},
    };

    struct v4l2_buffer buf;
    struct v4l2_plane planes[1] = {0};
    while (poll(fds, 1, 1000) > 0)
    {
        memset(&buf, 0, sizeof buf);

        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.length = n_video_planes;
        buf.m.planes = planes;

        if (-1 == ioctl(video_fd, VIDIOC_DQBUF, &buf))
        {
            fprintf(stderr, "VIDIOC_DQBUF error %d, %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }

        int ret = drmModeSetPlane(drm_fd,
                                  drm_plane_id,
                                  drm_crtc_id,
                                  drm_buffers[buf.index].buf_id,
                                  0,
                                  drm_crtc_rect.left,
                                  drm_crtc_rect.top,
                                  drm_crtc_rect.width,
                                  drm_crtc_rect.height,
                                  video_rect.left << 16,
                                  video_rect.top << 16,
                                  video_rect.width << 16,
                                  video_rect.height << 16);

        if (ret)
        {
            fprintf(stderr, "drmModeSetPlane error %d, %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (-1 == ioctl(video_fd, VIDIOC_QBUF, &buf))
        {
            fprintf(stderr, "VIDIOC_QBUF error %d, %s\n", errno, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}
