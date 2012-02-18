#ifndef _UVC_COMPAT_H
#define _UVC_COMPAT_H

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
/*
 * Extended control API
 */
struct v4l2_ext_control
{
	__u32 id;
	__u32 reserved2[2];
	union {
		__s32 value;
		__s64 value64;
		void *reserved;
	};
}__attribute__ ((packed));

struct v4l2_ext_controls
{
	__u32 ctrl_class;
	__u32 count;
	__u32 error_idx;
	__u32 reserved[2];
	struct v4l2_ext_control *controls;
};

/* Values for ctrl_class field */
#define V4L2_CTRL_CLASS_USER		0x00980000	/* Old-style 'user' controls */
#define V4L2_CTRL_CLASS_MPEG		0x00990000	/* MPEG-compression controls */

#define V4L2_CTRL_ID_MASK		(0x0fffffff)
#define V4L2_CTRL_ID2CLASS(id)		((id) & 0x0fff0000UL)
#define V4L2_CTRL_DRIVER_PRIV(id)	(((id) & 0xffff) >= 0x1000)

/* User-class control IDs defined by V4L2 */
#undef	V4L2_CID_BASE
#define V4L2_CID_BASE			(V4L2_CTRL_CLASS_USER | 0x900)
#define V4L2_CID_USER_BASE		V4L2_CID_BASE
#define V4L2_CID_USER_CLASS		(V4L2_CTRL_CLASS_USER | 1)

#define VIDIOC_G_EXT_CTRLS		_IOWR ('V', 71, struct v4l2_ext_controls)
#define VIDIOC_S_EXT_CTRLS		_IOWR ('V', 72, struct v4l2_ext_controls)
#define VIDIOC_TRY_EXT_CTRLS		_IOWR ('V', 73, struct v4l2_ext_controls)

#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
/*
 * Frame size and frame rate enumeration
 *
 * Included in Linux 2.6.19
 */

#define VIDIOC_ENUM_FRAMESIZES		_IOWR ('V', 74, struct v4l2_frmsizeenum)
#define VIDIOC_ENUM_FRAMEINTERVALS	_IOWR ('V', 75, struct v4l2_frmivalenum)
#endif

#endif /* _UVC_COMPAT_H */

