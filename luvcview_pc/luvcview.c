/*******************************************************************************
 #	 	luvcview: Sdl video Usb Video Class grabber           .        #
 #This package work with the Logitech UVC based webcams with the mjpeg feature. #
 #All the decoding is in user space with the embedded jpeg decoder              #
 #.                                                                             #
 # 		Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard     #
 #                                                                              #
 # This program is free software; you can redistribute it and/or modify         #
 # it under the terms of the GNU General Public License as published by         #
 # the Free Software Foundation; either version 2 of the License, or            #
 # (at your option) any later version.                                          #
 #                                                                              #
 # This program is distributed in the hope that it will be useful,              #
 # but WITHOUT ANY WARRANTY; without even the implied warranty of               #
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
 # GNU General Public License for more details.                                 #
 #                                                                              #
 # You should have received a copy of the GNU General Public License            #
 # along with this program; if not, write to the Free Software                  #
 # Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
 #                                                                              #
 *******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <string.h>
#include <pthread.h>

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include "v4l2uvc.h"
#include "utils.h"
#include "color.h"
/* Fixed point arithmetic */
/*
 #define FIXED Sint32
 #define FIXED_BITS 16
 #define TO_FIXED(X) (((Sint32)(X))<<(FIXED_BITS))
 #define FROM_FIXED(X) (((Sint32)(X))>>(FIXED_BITS))
 */
#define VERSION "v0.2.4"
#define INCPANTILT 64 // 1?
static const char version[] = VERSION;
struct vdIn *videoIn;
int getpictureflag = 1;

int main(int argc, char *argv[]) {
	char driver[128];
	int status;
	//Uint32 currtime;
	//Uint32 lasttime;
	unsigned char *p = NULL;
	int hwaccel = 0;
	const char *videodevice = NULL;
	const char *mode = NULL;
	int format = V4L2_PIX_FMT_MJPEG;
	int i;
	int grabmethod = 1;
	int width = 320;
	int height = 240;
	int fps = 15;
	unsigned char frmrate = 0;
	char *avifilename = NULL;
	int queryformats = 0;
	int querycontrols = 0;
	int readconfigfile = 0;
	char *separateur;
	char *sizestring = NULL;
	char *fpsstring = NULL;
	int enableRawStreamCapture = 0;
	int enableRawFrameCapture = 0;

	printf("luvcview version %s \n", version);
	for (i = 1; i < argc; i++) {
		/* skip bad arguments */
		if (argv[i] == NULL || *argv[i] == 0 || *argv[i] != '-') {
			continue;
		}
		if (strcmp(argv[i], "-d") == 0) {
			if (i + 1 >= argc) {
				printf("No parameter specified with -d, aborting.\n");
				exit(1);
			}
			videodevice = strdup(argv[i + 1]);
		}
		if (strcmp(argv[i], "-g") == 0) {
			/* Ask for read instead default  mmap */
			grabmethod = 0;
		}
		if (strcmp(argv[i], "-w") == 0) {
			/* disable hw acceleration */
			hwaccel = 1;
		}
		if (strcmp(argv[i], "-f") == 0) {
			if (i + 1 >= argc) {
				printf("No parameter specified with -f, aborting.\n");
				exit(1);
			}
			mode = strdup(argv[i + 1]);

			if (strncmp(mode, "yuv", 3) == 0) {
				format = V4L2_PIX_FMT_YUYV;

			} else if (strncmp(mode, "jpg", 3) == 0) {
				format = V4L2_PIX_FMT_MJPEG;

			} else {
				format = V4L2_PIX_FMT_JPEG;

			}
		}
		if (strcmp(argv[i], "-s") == 0) {
			if (i + 1 >= argc) {
				printf("No parameter specified with -s, aborting.\n");
				exit(1);
			}

			sizestring = strdup(argv[i + 1]);

			width = strtoul(sizestring, &separateur, 10);
			if (*separateur != 'x') {
				printf("Error in size use -s widthxheight \n");
				exit(1);
			} else {
				++separateur;
				height = strtoul(separateur, &separateur, 10);
				if (*separateur != 0)
					printf("hmm.. dont like that!! trying this height \n");
				printf(" size width: %d height: %d \n", width, height);
			}
		}
		if (strcmp(argv[i], "-i") == 0) {
			if (i + 1 >= argc) {
				printf("No parameter specified with -i, aborting. \n");
				exit(1);
			}
			fpsstring = strdup(argv[i + 1]);
			fps = strtoul(fpsstring, &separateur, 10);
			printf(" interval: %d fps \n", fps);
		}
		if (strcmp(argv[i], "-S") == 0) {
			/* Enable raw stream capture from the start */
			enableRawStreamCapture = 1;
		}
		if (strcmp(argv[i], "-c") == 0) {
			/* Enable raw frame capture for the first frame */
			enableRawFrameCapture = 1;
		}
		if (strcmp(argv[i], "-C") == 0) {
			/* Enable raw frame stream capture from the start*/
			enableRawFrameCapture = 2;
		}
		if (strcmp(argv[i], "-o") == 0) {
			/* set the avi filename */
			if (i + 1 >= argc) {
				printf("No parameter specified with -o, aborting.\n");
				exit(1);
			}
			avifilename = strdup(argv[i + 1]);
		}
		if (strcmp(argv[i], "-L") == 0) {
			/* query list of valid video formats */
			queryformats = 1;
		}
		if (strcmp(argv[i], "-l") == 0) {
			/* query list of valid video formats */
			querycontrols = 1;
		}

		if (strcmp(argv[i], "-r") == 0) {
			/* query list of valid video formats */
			readconfigfile = 1;
		}

		if (strcmp(argv[i], "-O") == 0) {
			/* get picture */
			getpictureflag = 1;
		}

		if (strcmp(argv[i], "-h") == 0) {
			printf(
					"usage: uvcview [-h -d -g -f -s -i -c -o -C -S -L -l -r] \n");
			printf("-h	print this message \n");
			printf("-d	/dev/videoX       use videoX device\n");
			printf("-g	use read method for grab instead mmap \n");
			printf("-w	disable SDL hardware accel. \n");
			printf(
					"-f	video format  default jpg  others options are yuv jpg \n");
			printf("-i	fps           use specified frame interval \n");
			printf("-s	widthxheight      use specified input size \n");
			printf("-c	enable raw frame capturing for the first frame\n");
			printf("-C	enable raw frame stream capturing from the start\n");
			printf("-S	enable raw stream capturing from the start\n");
			printf("-o	avifile  create avifile, default video.avi\n");
			printf("-L	query valid video formats\n");
			printf("-l	query valid controls and settings\n");
			printf("-r	read and set control settings from luvcview.cfg\n");
			printf("-O	get picture.\n");
			exit(0);
		}
	}

	if (videodevice == NULL || *videodevice == 0) {
		videodevice = "/dev/video0";
	}

	if (avifilename == NULL || *avifilename == 0) {
		avifilename = "video.avi";
	}

	videoIn = (struct vdIn *) calloc(1, sizeof(struct vdIn));
	if (queryformats) {
		/* if we're supposed to list the video formats, do that now and go out */
		check_videoIn(videoIn, (char *) videodevice);
		free(videoIn);
		exit(1);
	}
	if (init_videoIn(videoIn, (char *) videodevice, width, height, fps, format,
			grabmethod, avifilename) < 0)
		exit(1);
	/* if we're supposed to list the controls, do that now */
	if (querycontrols)
		enum_controls(videoIn->fd);

	/* if we're supposed to read the control settings from a configfile, do that now */
	if (readconfigfile)
		load_controls(videoIn->fd);

    printf("Enable Raw Stream Capture\n");
	if (enableRawStreamCapture) {
		videoIn->captureFile = fopen("stream.raw", "wb");
		if (videoIn->captureFile == NULL) {
			perror("Unable to open file for raw stream capturing");
		} else {
			printf("Starting raw stream capturing to stream.raw ...\n");
		}
	}
	if (enableRawFrameCapture)
		videoIn->rawFrameCapture = enableRawFrameCapture;

	initLut();

    printf("Begin main big loop\n");
    int loopNum = 0;
	/* main big loop */
	while (videoIn->signalquit) {
		//
		if (uvcGrab(videoIn) < 0) {
			printf("Error grabbing \n");
			break;
		}
		//
		/* if we're grabbing video, show the frame rate */
		if (videoIn->toggleAvi)
			printf("\rframe rate: %d ", frmrate);
		//

		if (getpictureflag) {
			//if (videoIn->getPict) { 
			switch (videoIn->formatIn) {
			case V4L2_PIX_FMT_MJPEG:
				get_picture(videoIn->tmpbuffer, videoIn->buf.bytesused);
				break;
			case V4L2_PIX_FMT_YUYV:
				printf("get picture yuv...\n");
				get_pictureYV2(videoIn->framebuffer, videoIn->width,
						videoIn->height);
				break;
			default:
				break;
			}
			videoIn->getPict = 0;
			printf("get picture !\n");
		}
        printf("loop number %d\n",loopNum);
        loopNum ++;
	}

	/* if avifile is defined, we made a video: compute the exact fps and
	 set it in the video */
	if (videoIn->avifile != NULL) {
		float fps = (videoIn->framecount / (videoIn->recordtime / 1000));
		fprintf(stderr, "setting fps to %f\n", fps);
		AVI_set_video(videoIn->avifile, videoIn->width, videoIn->height, fps,
				"MJPG");
		AVI_close(videoIn->avifile);
	}

	close_v4l2(videoIn);
	free(videoIn);
	freeLut();
	printf(" Clean Up done Quit \n");
}

