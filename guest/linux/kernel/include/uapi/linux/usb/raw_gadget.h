/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * USB Raw Gadget driver.
 *
 * See Documentation/usb/raw-gadget.rst for more details.
 */

#ifndef _UAPI__LINUX_USB_RAW_GADGET_H
#define _UAPI__LINUX_USB_RAW_GADGET_H

#include <asm/ioctl.h>
#include <linux/types.h>
#include <linux/usb/ch9.h>

/* Maximum length of driver_name/device_name in the usb_raw_init struct. */
#define UDC_NAME_LENGTH_MAX 128

/*
 * struct usb_raw_init - argument for USB_RAW_IOCTL_INIT ioctl.
 * @speed: The speed of the emulated USB device, takes the same values as
 *     the usb_device_speed enum: USB_SPEED_FULL, USB_SPEED_HIGH, etc.
 * @driver_name: The name of the UDC driver.
 * @device_name: The name of a UDC instance.
 *
 * The last two fields identify a UDC the gadget driver should bind to.
 * For example, Dummy UDC has "dummy_udc" as its driver_name and "dummy_udc.N"
 * as its device_name, where N in the index of the Dummy UDC instance.
 * At the same time the dwc2 driver that is used on Raspberry Pi Zero, has
 * "20980000.usb" as both driver_name and device_name.
 */
struct usb_raw_init {
	__u8	driver_name[UDC_NAME_LENGTH_MAX];
	__u8	device_name[UDC_NAME_LENGTH_MAX];
	__u8	speed;
};

/* The type of event fetched with the USB_RAW_IOCTL_EVENT_FETCH ioctl. */
enum usb_raw_event_type {
	USB_RAW_EVENT_INVALID,

	/* This event is queued when the driver has bound to a UDC. */
	USB_RAW_EVENT_CONNECT,

	/* This event is queued when a new control request arrived to ep0. */
	USB_RAW_EVENT_CONTROL,

	/* The list might grow in the future. */
};

/*
 * struct usb_raw_event - argument for USB_RAW_IOCTL_EVENT_FETCH ioctl.
 * @type: The type of the fetched event.
 * @length: Length of the data buffer. Updated by the driver and set to the
 *     actual length of the fetched event data.
 * @data: A buffer to store the fetched event data.
 *
 * Currently the fetched data buffer is empty for USB_RAW_EVENT_CONNECT,
 * and contains struct usb_ctrlrequest for USB_RAW_EVENT_CONTROL.
 */
struct usb_raw_event {
	__u32		type;
	__u32		length;
	__u8		data[0];
};

#define USB_RAW_IO_FLAGS_ZERO	0x0001
#define USB_RAW_IO_FLAGS_MASK	0x0001

static int usb_raw_io_flags_valid(__u16 flags)
{
	return (flags & ~USB_RAW_IO_FLAGS_MASK) == 0;
}

static int usb_raw_io_flags_zero(__u16 flags)
{
	return (flags & USB_RAW_IO_FLAGS_ZERO);
}

/*
 * struct usb_raw_ep_io - argument for USB_RAW_IOCTL_EP0/EP_WRITE/READ ioctls.
 * @ep: Endpoint handle as returned by USB_RAW_IOCTL_EP_ENABLE for
 *     USB_RAW_IOCTL_EP_WRITE/READ. Ignored for USB_RAW_IOCTL_EP0_WRITE/READ.
 * @flags: When USB_RAW_IO_FLAGS_ZERO is specified, the zero flag is set on
 *     the submitted USB request, see include/linux/usb/gadget.h for details.
 * @length: Length of data.
 * @data: Data to send for USB_RAW_IOCTL_EP0/EP_WRITE. Buffer to store received
 *     data for USB_RAW_IOCTL_EP0/EP_READ.
 */
struct usb_raw_ep_io {
	__u16		ep;
	__u16		flags;
	__u32		length;
	__u8		data[0];
};

/*
 * Initializes a Raw Gadget instance.
 * Accepts a pointer to the usb_raw_init struct as an argument.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_INIT		_IOW('U', 0, struct usb_raw_init)

/*
 * Instructs Raw Gadget to bind to a UDC and start emulating a USB device.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_RUN		_IO('U', 1)

/*
 * A blocking ioctl that waits for an event and returns fetched event data to
 * the user.
 * Accepts a pointer to the usb_raw_event struct.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_EVENT_FETCH	_IOR('U', 2, struct usb_raw_event)

/*
 * Queues an IN (OUT for READ) urb as a response to the last control request
 * received on endpoint 0, provided that was an IN (OUT for READ) request and
 * waits until the urb is completed. Copies received data to user for READ.
 * Accepts a pointer to the usb_raw_ep_io struct as an argument.
 * Returns length of trasferred data on success or negative error code on
 * failure.
 */
#define USB_RAW_IOCTL_EP0_WRITE		_IOW('U', 3, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP0_READ		_IOWR('U', 4, struct usb_raw_ep_io)

/*
 * Finds an endpoint that supports the transfer type specified in the
 * descriptor and enables it.
 * Accepts a pointer to the usb_endpoint_descriptor struct as an argument.
 * Returns enabled endpoint handle on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_EP_ENABLE		_IOW('U', 5, struct usb_endpoint_descriptor)

/* Disables specified endpoint.
 * Accepts endpoint handle as an argument.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_EP_DISABLE	_IOW('U', 6, __u32)

/*
 * Queues an IN (OUT for READ) urb as a response to the last control request
 * received on endpoint usb_raw_ep_io.ep, provided that was an IN (OUT for READ)
 * request and waits until the urb is completed. Copies received data to user
 * for READ.
 * Accepts a pointer to the usb_raw_ep_io struct as an argument.
 * Returns length of trasferred data on success or negative error code on
 * failure.
 */
#define USB_RAW_IOCTL_EP_WRITE		_IOW('U', 7, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP_READ		_IOWR('U', 8, struct usb_raw_ep_io)

/*
 * Switches the gadget into the configured state.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_CONFIGURE		_IO('U', 9)

/*
 * Constrains UDC VBUS power usage.
 * Accepts current limit in 2 mA units as an argument.
 * Returns 0 on success or negative error code on failure.
 */
#define USB_RAW_IOCTL_VBUS_DRAW		_IOW('U', 10, __u32)

#endif /* _UAPI__LINUX_USB_RAW_GADGET_H */
