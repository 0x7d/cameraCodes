#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <asm/types.h>
#include <linux/videodev2.h>

#define CAMERA_DEVICE "/dev/video0"
#define CAPTURE_FILE "frame.raw"
#define CAPTURE_PIC "capture.jpg"

#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480
#define VIDEO_FORMAT V4L2_PIX_FMT_YUYV
#define BUFFER_COUNT 4
#define CLIP(color) (unsigned char)(((color)>0xFF)?0xff:(((color)<0)?0:(color)))


#define Rcoef 299
#define Gcoef 587
#define Bcoef 114
#define Vrcoef 711 //656 //877
#define Ubcoef 560 //500 //493 564
#define CoefRv 1402
#define CoefGu 714 // 344
#define CoefGv 344 // 714
#define CoefBu 1772


static int *LutYr = NULL;
static int *LutYg = NULL;
static int *LutYb = NULL;
static int *LutVr = NULL;
static int *LutVrY = NULL;
static int *LutUb = NULL;
static int *LutUbY = NULL;
static int *LutRv = NULL;
static int *LutGu = NULL;
static int *LutGv = NULL;
static int *LutBu = NULL;

unsigned char R_FROMYV(unsigned char y, unsigned char v) {
	return CLIP((y) + LutRv[(v)]);
}
unsigned char G_FROMYUV(unsigned char y, unsigned char u, unsigned char v) {
	return CLIP((y) + LutGu[(u)] + LutGv[(v)]);
}
unsigned char B_FROMYU(unsigned char y, unsigned char u) {
	return CLIP((y) + LutBu[(u)]);
}

void initLut(void) {
	int i;

	LutYr = (int *) malloc(256 * sizeof(int));
	LutYg = (int *) malloc(256 * sizeof(int));
	LutYb = (int *) malloc(256 * sizeof(int));
	LutVr = (int *) malloc(256 * sizeof(int));
	LutVrY = (int *) malloc(256 * sizeof(int));
	LutUb = (int *) malloc(256 * sizeof(int));
	LutUbY = (int *) malloc(256 * sizeof(int));
	LutRv = (int *) malloc(256 * sizeof(int));
	LutGu = (int *) malloc(256 * sizeof(int));
	LutGv = (int *) malloc(256 * sizeof(int));
	LutBu = (int *) malloc(256 * sizeof(int));

	for (i = 0; i < 256; i++) {
		LutYr[i] = i * Rcoef / 1000;
		LutYg[i] = i * Gcoef / 1000;
		LutYb[i] = i * Bcoef / 1000;
		LutVr[i] = i * Vrcoef / 1000;
		LutUb[i] = i * Ubcoef / 1000;
		LutVrY[i] = 128 - (i * Vrcoef / 1000);
		LutUbY[i] = 128 - (i * Ubcoef / 1000);
		LutRv[i] = (i - 128) * CoefRv / 1000;
		LutBu[i] = (i - 128) * CoefBu / 1000;
		LutGu[i] = (128 - i) * CoefGu / 1000;
		LutGv[i] = (128 - i) * CoefGv / 1000;
	}
}

unsigned int Pyuv422torgb24(unsigned char * input_ptr,
		unsigned char * output_ptr, unsigned int image_width,
		unsigned int image_height) {
	unsigned int i, size;
	unsigned char Y, Y1, U, V;
	unsigned char *buff = input_ptr;
	unsigned char *output_pt = output_ptr;
	size = image_width * image_height / 2;
	for (i = size; i > 0; i--) {
		Y = buff[0];
		U = buff[1];
		Y1 = buff[2];
		V = buff[3];
		buff += 4;
		*output_pt++ = R_FROMYV(Y, V);
		*output_pt++ = G_FROMYUV(Y, U, V);
		*output_pt++ = B_FROMYU(Y, U);

		*output_pt++ = R_FROMYV(Y1, V);
		*output_pt++ = G_FROMYUV(Y1, U, V);
		*output_pt++ = B_FROMYU(Y1, U);
	}
	return 2;
}


int main()
{
	int i, ret;

	initLut();

	// 打开设备
	int fd;
	fd = open(CAMERA_DEVICE, O_RDWR, 0);
	if (fd < 0) {
		printf("Open %s failed\n", CAMERA_DEVICE);
		return -1;
	}

	// 获取驱动信息
	struct v4l2_capability cap;
	ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0) {
		printf("VIDIOC_QUERYCAP failed (%d)\n", ret);
		return ret;
	}
	// Print capability infomations
	printf("Capability Informations:\n");
	printf(" driver: %s\n", cap.driver);
	printf(" card: %s\n", cap.card);
	printf(" bus_info: %s\n", cap.bus_info);
	printf(" version: %08X\n", cap.version);
	printf(" capabilities: %08X\n", cap.capabilities);

	// 设置视频格式
	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width       = VIDEO_WIDTH;
	fmt.fmt.pix.height      = VIDEO_HEIGHT;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
	ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("VIDIOC_S_FMT failed (%d)\n", ret);
		return ret;
	}

	// 获取视频格式
	ret = ioctl(fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0) {
		printf("VIDIOC_G_FMT failed (%d)\n", ret);
		return ret;
	}
	// Print Stream Format
	printf("Stream Format Informations:\n");
	printf(" type: %d\n", fmt.type);
	printf(" width: %d\n", fmt.fmt.pix.width);
	printf(" height: %d\n", fmt.fmt.pix.height);
	char fmtstr[8];
	memset(fmtstr, 0, 8);
	memcpy(fmtstr, &fmt.fmt.pix.pixelformat, 4);
	printf(" pixelformat: %s\n", fmtstr);
	printf(" field: %d\n", fmt.fmt.pix.field);
	printf(" bytesperline: %d\n", fmt.fmt.pix.bytesperline);
	printf(" sizeimage: %d\n", fmt.fmt.pix.sizeimage);
	printf(" colorspace: %d\n", fmt.fmt.pix.colorspace);
	printf(" priv: %d\n", fmt.fmt.pix.priv);

	// 请求分配内存
	struct v4l2_requestbuffers reqbuf;

	memset(&reqbuf, 0, sizeof(struct v4l2_requestbuffers));
	reqbuf.count = BUFFER_COUNT;
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.memory = V4L2_MEMORY_MMAP;

	ret = ioctl(fd , VIDIOC_REQBUFS, &reqbuf);
	if(ret < 0) {
		printf("VIDIOC_REQBUFS failed (%d)\n", ret);
		return ret;
	}

	// 获取空间
	char*  buffers = (char *)calloc( BUFFER_COUNT, VIDEO_WIDTH * VIDEO_HEIGHT * 2);
	struct v4l2_buffer buf;
	void * framebuf[BUFFER_COUNT];

	for (i = 0; i < BUFFER_COUNT; i++)
	{
		buf.index = i;
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(fd , VIDIOC_QUERYBUF, &buf);
		if(ret < 0) {
			printf("VIDIOC_QUERYBUF (%d) failed (%d)\n", i, ret);
			return ret;
		}

		// mmap buffer
		framebuf[i] = (char *) mmap(0, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
		if (framebuf[i] == MAP_FAILED) {
			printf("mmap (%d) failed: %s\n", i, strerror(errno));
			return -1;
		}

		// Queen buffer
		ret = ioctl(fd , VIDIOC_QBUF, &buf);
		if (ret < 0) {
			printf("VIDIOC_QBUF (%d) failed (%d)\n", i, ret);
			return -1;
		}

		printf("Frame buffer %d: address=0x%x, length=%d\n", i, (unsigned int)framebuf[i], buf.length);
	}

	// 开始录制
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ret = ioctl(fd, VIDIOC_STREAMON, &type);
	if (ret < 0) {
		printf("VIDIOC_STREAMON failed (%d)\n", ret);
		return ret;
	}

	// Get frame
	memset(&buf, 0, sizeof(struct v4l2_buffer));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(fd, VIDIOC_DQBUF, &buf);
	if (ret < 0) {
		printf("VIDIOC_DQBUF failed (%d)\n", ret);
		return ret;
	}

	// Process the frame
	FILE *fp = fopen(CAPTURE_FILE, "wb");
	if (fp < 0) {
		printf("open frame data file failed\n");
		return -1;
	}

	printf("buf length %d, buf bytes used %d\n", buf.length, buf.bytesused);
	fwrite(framebuf[buf.index], 1, buf.bytesused, fp);
	fclose(fp);
	printf("Capture one frame to file %s\n", CAPTURE_FILE);

	memcpy(buffers, framebuf[buf.index], (size_t) buf.bytesused);
	char *name = CAPTURE_PIC;
	FILE *foutpict;
	unsigned char *picture = (unsigned char *) malloc(VIDEO_WIDTH * VIDEO_HEIGHT * 3 * sizeof(char));
	if (picture) {
		Pyuv422torgb24((unsigned char *)buffers, picture, VIDEO_WIDTH, VIDEO_HEIGHT);
	} else {
		printf("no room to take a picture \n");
		return 0;
	}

	foutpict = fopen(name, "wb");
	fprintf(foutpict, "P6\n%d %d\n255\n", VIDEO_WIDTH, VIDEO_HEIGHT);
	fwrite(picture, sizeof(char), VIDEO_WIDTH * VIDEO_HEIGHT * 3, foutpict);
	fclose(foutpict);
	free(picture);
	picture = NULL;
	printf("Capture one frame to picture %s\n", CAPTURE_PIC);

	// Re-queen buffer
	ret = ioctl(fd, VIDIOC_QBUF, &buf);
	if (ret < 0) {
		printf("VIDIOC_QBUF failed (%d)\n", ret);
		return ret;
	}

	// Release the resource
	for (i=0; i< 4; i++)
	{
		munmap(framebuf[i], buf.length);
	}

	close(fd);
	printf("Camera test Done.\n");
	return 0;
}
