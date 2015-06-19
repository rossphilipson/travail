/******************************************************************************
 * vusb.c
 *
 * OpenXT vUSB frontend driver
 *
 * Copyright (c) 2015, Assured Information Security, Inc.
 *
 * Author:
 * Ross Philipson <philipsonr@ainfosec.com>
 *
 * Previous version:
 * Julien Grall
 * Thomas Horsten
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* TODO
 * Use DMA buffers
 * Handle errors on internal cmds
 * Deal with USBIF_RSP_USB_DEVRMVD
 * Sleep/resume and recover functionality
 * Refactor vusb_put_urb and vusb_put_isochronous_urb into one function.
 * Add branch prediction
 */

#include <linux/mm.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb.h>
#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0) )
#include <linux/aio.h>
#endif

#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/page.h>
#include <xen/grant_table.h>

#include <xen/interface/io/usbif.h>
#include <xen/interface/memory.h>
#include <xen/interface/grant_table.h>

#include <linux/usb/hcd.h>

#define VUSB_INTERFACE_VERSION		3
#define VUSB_INVALID_REQ_ID		((u64)-1)

#define VUSB_PLATFORM_DRIVER_NAME	"xen-vusb"
#define VUSB_HCD_DRIVER_NAME		"vusb-hcd"
#define VUSB_DRIVER_DESC		"OpenXT Virtual USB Host Controller"
#define VUSB_DRIVER_VERSION		"1.0.0"
#define VUSB_POWER_BUDGET		5000 /* mA */

#define GRANTREF_INVALID 		0
#define EVTCHN_INVALID			(-1)
#define USB_RING_SIZE __CONST_RING_SIZE(usbif, PAGE_SIZE)
#define SHADOW_ENTRIES USB_RING_SIZE
#define INDIRECT_PAGES_REQUIRED(p) (((p - 1)/USBIF_MAX_SEGMENTS_PER_IREQUEST) + 1)
#define MAX_INDIRECT_PAGES USBIF_MAX_SEGMENTS_PER_REQUEST
#define MAX_PAGES_FOR_INDIRECT_REQUEST (MAX_INDIRECT_PAGES * USBIF_MAX_SEGMENTS_PER_IREQUEST)
#define MAX_PAGES_FOR_INDIRECT_ISO_REQUEST (MAX_PAGES_FOR_INDIRECT_REQUEST - 1)

#define D_VUSB1 (1 << 0)
#define D_VUSB2 (1 << 1)
#define D_URB1  (1 << 2)
#define D_URB2  (1 << 3)
#define D_STATE (1 << 4)
#define D_PORT1 (1 << 5)
#define D_PORT2 (1 << 6)
#define D_CTRL  (1 << 8)
#define D_MISC  (1 << 9)
#define D_PM    (1 << 10)
#define D_RING1 (1 << 11)
#define D_RING2 (1 << 12)

#define DEBUGMASK (D_STATE | D_PORT1 | D_URB1 | D_PM)

/* #define VUSB_DEBUG */

#ifdef VUSB_DEBUG
#  define dprintk(mask, args...)					\
	do {								\
		if (DEBUGMASK & mask)					\
			printk(KERN_DEBUG "vusb: "args);		\
	} while (0)

#  define dprint_hex_dump(mask, args...)				\
	do {								\
		if (DEBUGMASK & mask)					\
			print_hex_dump(KERN_DEBUG, "vusb: "args);	\
	} while (0)
#else
#  define dprintk(args...) do {} while (0)
#  define dprint_hex_dump(args...) do {} while (0)
#endif

#define eprintk(args...) printk(KERN_ERR "vusb: "args)
#define wprintk(args...) printk(KERN_WARNING "vusb: "args)
#define iprintk(args...) printk(KERN_INFO "vusb: "args)

/* How many ports on the root hub */
#define VUSB_PORTS	USB_MAXCHILDREN

#if ( LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0) )
# define USBFRONT_IRQF 0
#else
# define USBFRONT_IRQF IRQF_SAMPLE_RANDOM
#endif

/* Port are numbered from 1 in linux */
#define vusb_vdev_by_port(v, port) (&((v)->vrh_ports[(port) - 1].vdev))
#define vusb_vhcd_by_vdev(d) ((container_of(d, struct vusb_rh_port, vdev))->vhcd)
#define vusb_vport_by_vdev(d) (container_of(d, struct vusb_rh_port, vdev))
#define vusb_vport_by_port(v, port) (&(v)->vrh_ports[(port) - 1])
#define vusb_check_port(index) \
	(((index) < 1 || (index) > VUSB_PORTS) ? false : true)
#define vusb_dir_to_string(d) (d ? "IN" : "OUT")
#define vusb_start_processing(v) vusb_start_processing_caller(v, (__FUNCTION__))

/* Possible state of an urbp */
enum vusb_urbp_state {
	VUSB_URBP_NEW,
	VUSB_URBP_SENT,
	VUSB_URBP_DONE,
	VUSB_URBP_DROP, /* when an error occurs and unsent */
	VUSB_URBP_CANCEL
};

enum vusb_internal_cmd {
	VUSB_CMD_RESET,
	VUSB_CMD_CYCLE,
	VUSB_CMD_SPEED,
	VUSB_CMD_CANCEL
};

/* URB tracking structure */
struct vusb_urbp {
	struct urb		*urb;
	u64			id;
	enum vusb_urbp_state	state;
	struct list_head	urbp_list;
	int			port;
	usbif_response_t	rsp;
	usbif_iso_packet_info_t	*iso_packet_info;
};

struct vusb_shadow {
	usbif_request_t		req;
	unsigned long		frames[USBIF_MAX_SEGMENTS_PER_REQUEST];
	struct vusb_urbp	*urbp;
	usbif_iso_packet_info_t	*iso_packet_info;
	void			*indirect_reqs;
	u32			indirect_reqs_size;
	unsigned long		*indirect_frames;
	unsigned		in_use:1;
};

/* Virtual USB device on of the RH ports */
struct vusb_device {
	spinlock_t			lock;
	u16				address;
	enum usb_device_speed		speed;
	bool				is_ss;
	bool				rflush;

	/* The Xenbus device associated with this vusb device */
	struct xenbus_device		*xendev;

	/* Xen rings and event channel */
	int				ring_ref;
	struct usbif_front_ring 	ring;
	unsigned int 			evtchn;
	unsigned int 			irq;
	struct gnttab_free_callback 	callback;

	/* Shadow buffers */
	struct vusb_shadow		*shadows;
	u16				*shadow_free_list;
	u16				shadow_free;

	/* This VUSB device's lists of pending URB work */
	struct list_head		pending_list;
	struct list_head		release_list;
	struct list_head		finish_list;

	struct work_struct 		work;
	wait_queue_head_t		wait_queue;
};

/* Virtual USB HCD/RH pieces */
enum vusb_rh_state {
	VUSB_RH_SUSPENDED,
	VUSB_RH_RUNNING
};

enum vusb_hcd_state {
	VUSB_HCD_INACTIVE,
	VUSB_HCD_RUNNING
};

struct vusb_rh_port {
	u32				port;
	u32				port_status;

	/* Pointer back to the virtual HCD core device */
	struct vusb_vhcd		*vhcd;

	u16				device_id;
	struct vusb_device		vdev;

	/* State of device attached to this vRH port */
	unsigned			connecting:1;
	unsigned			present:1;
	unsigned			closing:1;

	/* Current counter for jobs processing for device */
	u32				processing;

	/* Reset gate for port/device resets */
	atomic_t			reset_pending;
	bool				reset_done;

	struct work_struct 		work;
	wait_queue_head_t		wait_queue;
};

struct vusb_vhcd {
	spinlock_t			lock;

	enum vusb_hcd_state		hcd_state;
	enum vusb_rh_state		rh_state;

	struct vusb_rh_port		vrh_ports[VUSB_PORTS];
};

static struct platform_device *vusb_platform_device = NULL;

static bool
vusb_start_processing_caller(struct vusb_rh_port *vport,
		const char *caller);
static void
vusb_stop_processing(struct vusb_rh_port *vport);
static void
vusb_process(struct vusb_device *vdev, struct vusb_urbp *urbp);
static int
vusb_put_internal_request(struct vusb_device *vdev,
		enum vusb_internal_cmd cmd, u64 cancel_id);
static void
vusb_port_work_handler(struct work_struct *work);
static void
vusb_urbp_queue_release(struct vusb_device *vdev, struct vusb_urbp *urbp);

/****************************************************************************/
/* Miscellaneous Routines                                                   */

static inline struct vusb_vhcd*
hcd_to_vhcd(struct usb_hcd *hcd)
{
	return (struct vusb_vhcd *)(hcd->hcd_priv);
}

static inline struct usb_hcd*
vhcd_to_hcd(struct vusb_vhcd *vhcd)
{
	return container_of((void *)vhcd, struct usb_hcd, hcd_priv);
}

static inline struct device*
vusb_dev(struct vusb_vhcd *vhcd)
{
	return vhcd_to_hcd(vhcd)->self.controller;
}

#ifdef VUSB_DEBUG

/* Convert urb pipe type to string */
static const char *
vusb_pipe_to_string(struct urb *urb)
{
	switch (usb_pipetype(urb->pipe)) {
	case PIPE_ISOCHRONOUS:
		return "ISOCHRONOUS";
	case PIPE_CONTROL:
		return "CONTROL";
	case PIPE_INTERRUPT:
		return "INTERRUPT";
	case PIPE_BULK:
		return "BULK";
	default:
		return "Unknown";
	}
}

/* Convert urbp state to string */
static const char *
vusb_state_to_string(const struct vusb_urbp *urbp)
{
	switch (urbp->state) {
	case VUSB_URBP_NEW:
		return "NEW";
	case VUSB_URBP_SENT:
		return "SENT";
	case VUSB_URBP_DONE:
		return "DONE";
	case VUSB_URBP_DROP:
		return "DROP";
	case VUSB_URBP_CANCEL:
		return "CANCEL";
	default:
		return "unknown";
	}
}

#endif /* VUSB_DEBUG */

/****************************************************************************/
/* VUSB HCD & RH                                                            */

static inline u16
vusb_speed_to_port_stat(enum usb_device_speed speed)
{
	switch (speed) {
	case USB_SPEED_HIGH:
		return USB_PORT_STAT_HIGH_SPEED;
	case USB_SPEED_LOW:
		return USB_PORT_STAT_LOW_SPEED;
	case USB_SPEED_FULL:
	default:
		return 0;
	}
}

static inline u16
vusb_pipe_type_to_optype(u16 type)
{
	switch (type) {
	case PIPE_ISOCHRONOUS:
		return USBIF_T_ISOC;
	case PIPE_INTERRUPT:
		return USBIF_T_INT;
	case PIPE_CONTROL:
		return USBIF_T_CNTRL;
	case PIPE_BULK:
		return USBIF_T_BULK;
	default:
		return 0xffff;
	}
}

static struct vusb_rh_port*
vusb_get_vport(struct vusb_vhcd *vhcd, u16 id)
{
	u16 i;
	struct vusb_rh_port *vport;

	for (i = 0; i < VUSB_PORTS; i++) {
		if ((vhcd->vrh_ports[i].connecting)||
			(vhcd->vrh_ports[i].closing))
			continue;
		if (!vhcd->vrh_ports[i].present)
			break;
		if (vhcd->vrh_ports[i].device_id == id) {
			wprintk("Device id 0x%04x already exists on port %d\n",
				id, vhcd->vrh_ports[i].port);
			return NULL;
		}
	}

	if (i >= VUSB_PORTS) {
		wprintk("Attempt to add a device but no free ports on the root hub.\n");
		return NULL;
	}

	vport = &vhcd->vrh_ports[i];
	vport->device_id = id;
	vport->connecting = 1;

	return vport;
}

static void
vusb_put_vport(struct vusb_vhcd *vhcd, struct vusb_rh_port *vport)
{
	/* Port reset after this call. Restore the port values
	 * when no device is attached */
	memset(&vport->vdev, 0, sizeof(struct vusb_device));
	vport->device_id = 0;
	vport->connecting = 0;
	vport->present= 0;
	vport->closing = 0;
	vport->processing = 0;
	atomic_set(&vport->reset_pending, 0);
	vport->reset_done = false;
}

static void
vusb_set_link_state(struct vusb_rh_port *vport)
{
	u32 newstatus, diff;

	newstatus = vport->port_status;
	dprintk(D_STATE, "SLS: Port index %u status 0x%08x\n",
			vport->port, newstatus);

	if (vport->present && !vport->closing) {
		newstatus |= (USB_PORT_STAT_CONNECTION) |
					vusb_speed_to_port_stat(vport->vdev.speed);
	}
	else {
		newstatus &= ~(USB_PORT_STAT_CONNECTION |
					USB_PORT_STAT_LOW_SPEED |
					USB_PORT_STAT_HIGH_SPEED |
					USB_PORT_STAT_ENABLE |
					USB_PORT_STAT_SUSPEND);
	}
	if ((newstatus & USB_PORT_STAT_POWER) == 0) {
		newstatus &= ~(USB_PORT_STAT_CONNECTION |
					USB_PORT_STAT_LOW_SPEED |
					USB_PORT_STAT_HIGH_SPEED |
					USB_PORT_STAT_SUSPEND);
	}
	diff = vport->port_status ^ newstatus;

	if ((newstatus & USB_PORT_STAT_POWER) &&
		(diff & USB_PORT_STAT_CONNECTION)) {
		newstatus |= (USB_PORT_STAT_C_CONNECTION << 16);
		dprintk(D_STATE, "Port %u connection state changed: %08x\n",
				vport->port, newstatus);
	}

	vport->port_status = newstatus;
}

/* SetFeaturePort(PORT_RESET) */
static void
vusb_port_reset(struct vusb_vhcd *vhcd, struct vusb_rh_port *vport)
{
	printk(KERN_DEBUG "vusb: port reset %u 0x%08x",
		   vport->port, vport->port_status);

	vport->port_status |= USB_PORT_STAT_ENABLE | USB_PORT_STAT_POWER;

	/* Test reset gate, only want one reset in flight at a time per
	 * port. If the gate is set, it will return the "unless" value. */
	if (__atomic_add_unless(&vport->reset_pending, 1, 1) == 1)
		return;

	/* Schedule it for the device, can't do it here in the vHCD lock */
	schedule_work(&vport->work);
}

static void
vusb_set_port_feature(struct vusb_vhcd *vhcd, struct vusb_rh_port *vport, u16 val)
{
	switch (val) {
	case USB_PORT_FEAT_INDICATOR:
	case USB_PORT_FEAT_SUSPEND:
		/* Ignored now */
		break;

	case USB_PORT_FEAT_POWER:
		vport->port_status |= USB_PORT_STAT_POWER;
		break;
	case USB_PORT_FEAT_RESET:
		vusb_port_reset(vhcd, vport);
		break;
	case USB_PORT_FEAT_C_CONNECTION:
	case USB_PORT_FEAT_C_RESET:
	case USB_PORT_FEAT_C_ENABLE:
	case USB_PORT_FEAT_C_SUSPEND:
	case USB_PORT_FEAT_C_OVER_CURRENT:
		vport->port_status &= ~(1 << val);
		break;
	default:
		/* No change needed */
		return;
	}
	vusb_set_link_state(vport);
}

static void
vusb_clear_port_feature(struct vusb_rh_port *vport, u16 val)
{
	switch (val) {
	case USB_PORT_FEAT_INDICATOR:
	case USB_PORT_FEAT_SUSPEND:
		/* Ignored now */
		break;
	case USB_PORT_FEAT_ENABLE:
		vport->port_status &= ~USB_PORT_STAT_ENABLE;
		vusb_set_link_state(vport);
		break;
	case USB_PORT_FEAT_POWER:
		vport->port_status &= ~(USB_PORT_STAT_POWER | USB_PORT_STAT_ENABLE);
		vusb_set_link_state(vport);
		break;
	case USB_PORT_FEAT_C_CONNECTION:
	case USB_PORT_FEAT_C_RESET:
	case USB_PORT_FEAT_C_ENABLE:
	case USB_PORT_FEAT_C_SUSPEND:
	case USB_PORT_FEAT_C_OVER_CURRENT:
		dprintk(D_PORT1, "Clear bit %d, old 0x%08x mask 0x%08x new 0x%08x\n",
				val, vport->port_status, ~(1 << val),
				vport->port_status & ~(1 << val));
		vport->port_status &= ~(1 << val);
		break;
	default:
		/* No change needed */
		return;
	}
}

/* Hub descriptor */
static void
vusb_hub_descriptor(struct usb_hub_descriptor *desc)
{
	u16 temp;

	desc->bDescriptorType = 0x29;
	desc->bPwrOn2PwrGood = 10; /* echi 1.0, 2.3.9 says 20ms max */
	desc->bHubContrCurrent = 0;
	desc->bNbrPorts = VUSB_PORTS;

	/* size of DeviceRemovable and PortPwrCtrlMask fields */
	temp = 1 + (VUSB_PORTS / 8);
	desc->bDescLength = 7 + 2 * temp;

	/* bitmaps for DeviceRemovable and PortPwrCtrlMask */

	/* The union was introduced to support USB 3.0 */
	memset(&desc->u.hs.DeviceRemovable[0], 0, temp);
	memset(&desc->u.hs.DeviceRemovable[temp], 0xff, temp);

	/* per-port over current reporting and no power switching */
	temp = 0x00a;
	desc->wHubCharacteristics = cpu_to_le16(temp);
}

static int
vusb_hcd_start(struct usb_hcd *hcd)
{
	struct vusb_vhcd *vhcd = hcd_to_vhcd(hcd);
	int i;

	iprintk("XEN HCD start\n");

	dprintk(D_MISC, ">vusb_start\n");

	/* Initialize root hub ports */
	for (i = 0; i < VUSB_PORTS; i++) {
		memset(&vhcd->vrh_ports[i], 0, sizeof(struct vusb_rh_port));
		vhcd->vrh_ports[i].port = i + 1;
		vhcd->vrh_ports[i].vhcd = vhcd;
		INIT_WORK(&vhcd->vrh_ports[i].work, vusb_port_work_handler);
		init_waitqueue_head(&vhcd->vrh_ports[i].wait_queue);
	}

	/* Enable HCD/RH */
	vhcd->rh_state = VUSB_RH_RUNNING;
	vhcd->hcd_state = VUSB_HCD_RUNNING;

	hcd->power_budget = VUSB_POWER_BUDGET;
	hcd->state = HC_STATE_RUNNING;
	hcd->uses_new_polling = 1;

	dprintk(D_MISC, "<vusb_start 0\n");

	return 0;
}

static void
vusb_hcd_stop(struct usb_hcd *hcd)
{
	struct vusb_vhcd *vhcd;

	iprintk("XEN HCD stop\n");

	dprintk(D_MISC, ">vusb_stop\n");

	vhcd = hcd_to_vhcd(hcd);

	hcd->state = HC_STATE_HALT;
	/* TODO: remove all URBs */
	/* TODO: "cleanly make HCD stop writing memory and doing I/O" */

	dev_info(vusb_dev(vhcd), "stopped\n");
	dprintk(D_MISC, "<vusb_stop\n");
}

static int
vusb_hcd_urb_enqueue(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags)
{
	struct vusb_vhcd *vhcd;
	unsigned long flags;
	struct vusb_urbp *urbp;
	struct vusb_rh_port *vport;
	struct vusb_device *vdev;
	int ret = -ENOMEM;

	dprintk(D_MISC, ">vusb_urb_enqueue\n");

	vhcd = hcd_to_vhcd(hcd);

	if (!urb->transfer_buffer && urb->transfer_buffer_length)
		return -EINVAL;

	if (!vusb_check_port(urb->dev->portnum))
		return -EPIPE;

	urbp = kzalloc(sizeof(*urbp), mem_flags);
	if (!urbp)
		return -ENOMEM;

	urbp->state = VUSB_URBP_NEW;
	/* Port numbered from 1 */
	urbp->port = urb->dev->portnum;
	urbp->urb = urb;
	/* No req ID until shadow is allocated */
	urbp->id = VUSB_INVALID_REQ_ID;

	spin_lock_irqsave(&vhcd->lock, flags);

	vport = vusb_vport_by_port(vhcd, urbp->port);
	if (vhcd->hcd_state == VUSB_HCD_INACTIVE || !vport->present) {
		kfree(urbp);
		spin_unlock_irqrestore(&vhcd->lock, flags);
		eprintk("Enqueue processing called with device/port invalid states\n");
		return -ENXIO;
	}

	if (vport->closing) {
		kfree(urbp);
		spin_unlock_irqrestore(&vhcd->lock, flags);
		return -ESHUTDOWN;
	}

	ret = usb_hcd_link_urb_to_ep(hcd, urb);
	if (ret) {
		kfree(urbp);
		spin_unlock_irqrestore(&vhcd->lock, flags);
		return ret;
	}

	/* Bump the processing counter so it is not nuked out from under us */
	vport->processing++;
	vdev = vusb_vdev_by_port(vhcd, urbp->port);
	spin_unlock_irqrestore(&vhcd->lock, flags);

	vusb_process(vdev, urbp);

	/* Finished processing */
	vusb_stop_processing(vport);

	return 0;
}

static int
vusb_hcd_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
	struct vusb_vhcd *vhcd;
	unsigned long flags;
	int ret;
	bool found = false;
	struct vusb_rh_port *vport;
	struct vusb_device *vdev;
	struct vusb_urbp *urbp;

	dprintk(D_MISC, "*vusb_urb_dequeue\n");

	if (!vusb_check_port(urb->dev->portnum))
		return -EPIPE;

	vhcd = hcd_to_vhcd(hcd);

	spin_lock_irqsave(&vhcd->lock, flags);

	/* Supposed to hold HCD lock when calling this */
	ret = usb_hcd_check_unlink_urb(hcd, urb, status);
	if (ret) {
		spin_unlock_irqrestore(&vhcd->lock, flags);
		return ret;
	}

	urb->status = status;

	/* If it can't be processed, the urbp and urb will be released
	 * in the device teardown code which is where this device is going
	 * (or gone). */
	vport = vusb_vport_by_port(vhcd, urb->dev->portnum);
	if (vhcd->hcd_state == VUSB_HCD_INACTIVE || !vport->present) {
		spin_unlock_irqrestore(&vhcd->lock, flags);
		eprintk("Dequeue processing called with device/port invalid states\n");
		return -ENXIO;
	}

	if (vport->closing) {
		spin_unlock_irqrestore(&vhcd->lock, flags);
		return -ESHUTDOWN;
	}

	/* Bump the processing counter so it is not nuked out from under us */
	vport->processing++;
	vdev = vusb_vdev_by_port(vhcd, urb->dev->portnum);
	spin_unlock_irqrestore(&vhcd->lock, flags);

	spin_lock_irqsave(&vdev->lock, flags);

	/* Need to find the urbp. Note the urbp can be in 4 states:
	 * 1. In the pending queue not sent. In this case we just grab it and
	 *    release it.
	 * 2. In the pending queue sent. In this case we need to flag it as
	 *    cancelled, snipe it with the internal cancel command and clean it
	 *    up in response finish processing.
	 * 3. In the finish queue. Not much can be done but to let it get
	 *    finished and released.
	 * 4. In the release queue. Again just let it get released.
	 * In both 3 and 4, we can just drive response processing to drive the
	 * urbp through to completion. Note there is a window in enqueue where
	 * the new urbp is not yet on the pending list outside the vdev lock.
	 * It seems this would be OK - it seems it is unlikely the core would
	 * call dequeue on the same URB it was currently calling enqueue for. */
	list_for_each_entry(urbp, &vdev->pending_list, urbp_list) {
		if (urbp->urb == urb) {
			found = true;
			break;
		}
	}

	while (found) {
		/* Found it in the pending list, see if it is in state 1 and
		 * and get rid of it right here. */
		if (urbp->state != VUSB_URBP_SENT) {
			vusb_urbp_queue_release(vdev, urbp);
			break;
		}

		/* State 2, this is the hardest one. The urbp cannot be simply
		 * discarded because it has shadow associated with it. It will
		 * have to be flagged as canceled and left for response
		 * processing to handle later. It also has to be shot down in
		 * the backend processing. */
		urbp->state = VUSB_URBP_CANCEL;
		ret = vusb_put_internal_request(vdev, VUSB_CMD_CANCEL, urbp->id);
		if (ret) {
			eprintk("Failed cancel command for URB id: %d, err: %d\n",
				(int)urbp->id, ret);
			/* Go on and do the best we can... */
		}
		break;
	}

	/* For urbp's in states 3 and 4, they will be fishished and released
	 * and their status is what it is at this point. */

	spin_unlock_irqrestore(&vdev->lock, flags);

	/* Drive processing requests and responses */
	vusb_process(vdev, NULL);

	vusb_stop_processing(vport);

	return 0;
}

static int
vusb_hcd_get_frame(struct usb_hcd *hcd)
{
	struct timeval	tv;

	dprintk(D_MISC, "*vusb_get_frame\n");
	/* TODO can we use the internal cmd to do this? */
	do_gettimeofday(&tv);

	return tv.tv_usec / 1000;
}

#define PORT_C_MASK \
	((USB_PORT_STAT_C_CONNECTION \
	| USB_PORT_STAT_C_ENABLE \
	| USB_PORT_STAT_C_SUSPEND \
	| USB_PORT_STAT_C_OVERCURRENT \
	| USB_PORT_STAT_C_RESET) << 16)

static int
vusb_hcd_hub_status(struct usb_hcd *hcd, char *buf)
{
	struct vusb_vhcd *vhcd = hcd_to_vhcd(hcd);
	unsigned long flags;
	int resume = 0;
	int changed = 0;
	u16 length = 0;
	int ret = 0;
	u16 i;

	dprintk(D_MISC, ">vusb_hub_status\n");

	/* TODO FIXME: Not sure it's good */
	if (!HCD_HW_ACCESSIBLE(hcd)) {
		wprintk("Hub is not running %u\n", hcd->state);
		dprintk(D_MISC, ">vusb_hub_status 0\n");
		return 0;
	}

	/* Initialize the status to no-change */
	length = 1 + (VUSB_PORTS / 8);
	for (i = 0; i < length; i++)
		buf[i] = 0;

	spin_lock_irqsave(&vhcd->lock, flags);

	for (i = 0; i < VUSB_PORTS; i++) {
		struct vusb_rh_port *vport = &vhcd->vrh_ports[i];

		/* Check status for each port */
		dprintk(D_PORT2, "check port %u (%08x)\n", vport->port,
				vport->port_status);
		if ((vport->port_status & PORT_C_MASK) != 0) {
			if (i < 7)
				buf[0] |= 1 << (i + 1);
			else if (i < 15)
				buf[1] |= 1 << (i - 7);
			else if (i < 23)
				buf[2] |= 1 << (i - 15);
			else
				buf[3] |= 1 << (i - 23);
			dprintk(D_PORT2, "port %u status 0x%08x has changed\n",
					vport->port, vport->port_status);
			changed = 1;
		}

		if (vport->port_status & USB_PORT_STAT_CONNECTION)
			resume = 1;
	}

	if (resume && vhcd->rh_state == VUSB_RH_SUSPENDED)
		usb_hcd_resume_root_hub(hcd);

	ret = (changed) ? length : 0;

	spin_unlock_irqrestore(&vhcd->lock, flags);
	dprintk(D_MISC, "<vusb_hub_status %d\n", ret);

	return ret;
}

static int
vusb_hcd_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
		u16 wIndex, char *buf, u16 wLength)
{
	struct vusb_vhcd *vhcd;
	int retval = 0;
	unsigned long flags;
	u32 status;

	/* TODO fix param names */
	dprintk(D_CTRL, ">vusb_hub_control %04x %04x %04x\n",
			typeReq, wIndex, wValue);

	if (!HCD_HW_ACCESSIBLE(hcd)) {
		dprintk(D_CTRL, "<vusb_hub_control %d\n", ETIMEDOUT);
		return -ETIMEDOUT;
	}

	vhcd = hcd_to_vhcd(hcd);
	spin_lock_irqsave(&vhcd->lock, flags);

	switch (typeReq) {
	case ClearHubFeature:
		break;
	case ClearPortFeature:
		dprintk(D_CTRL, "ClearPortFeature port %d val: 0x%04x\n",
				wIndex, wValue);
		if (!vusb_check_port(wIndex)) {
			wprintk("ClearPortFeature invalid port %u", wIndex);
        	        retval = -EPIPE;
	                break;
		}
		vusb_clear_port_feature(vusb_vport_by_port(vhcd, wIndex), wValue);
		break;
	case GetHubDescriptor:
		vusb_hub_descriptor((struct usb_hub_descriptor *)buf);
		break;
	case GetHubStatus:
		/* Always local power supply good and no over-current exists. */
		*(__le32 *)buf = cpu_to_le32(0);
		break;
	case GetPortStatus:
		if (!vusb_check_port(wIndex)) {
			wprintk("GetPortStatus invalid port %u", wIndex);
        	        retval = -EPIPE;
	                break;
		}
		status = vusb_vport_by_port(vhcd, wIndex)->port_status;
		dprintk(D_CTRL, "GetPortStatus port %d = 0x%08x\n", wIndex, status);
		((__le16 *) buf)[0] = cpu_to_le16(status);
		((__le16 *) buf)[1] = cpu_to_le16(status >> 16);
		break;
	case SetHubFeature:
		retval = -EPIPE;
		break;
	case SetPortFeature:
		if (!vusb_check_port(wIndex)) {
			wprintk("SetPortFeature invalid port %u", wIndex);
        	        retval = -EPIPE;
	                break;
		}
		dprintk(D_CTRL, "SetPortFeature port %d val: 0x%04x\n", wIndex, wValue);
		vusb_set_port_feature(vhcd, vusb_vport_by_port(vhcd, wIndex), wValue);
		break;

	default:
		dev_dbg(vusb_dev(vhcd),
			"hub control req%04x v%04x i%04x l%d\n",
			typeReq, wValue, wIndex, wLength);

		/* "protocol stall" on error */
		retval = -EPIPE;
	}
	spin_unlock_irqrestore(&vhcd->lock, flags);

	if (wIndex >= 1 && wIndex <= VUSB_PORTS) {
		if ((vusb_vport_by_port(vhcd, wIndex)->port_status & PORT_C_MASK) != 0)
			 usb_hcd_poll_rh_status(hcd);
	}

	dprintk(D_MISC, "<vusb_hub_control %d\n", retval);
	return retval;
}

#ifdef CONFIG_PM
static int
vusb_hcd_bus_suspend(struct usb_hcd *hcd)
{
	struct vusb_vhcd *vhcd = hcd_to_vhcd(hcd);
	unsigned long flags;

	dprintk(D_PM, "Bus suspend\n");

	spin_lock_irqsave(&vhcd->lock, flags);
	vhcd->rh_state = VUSB_RH_SUSPENDED;
	spin_unlock_irqrestore(&vhcd->lock, flags);

	return 0;
}

static int
vusb_hcd_bus_resume(struct usb_hcd *hcd)
{
	struct vusb_vhcd *vhcd = hcd_to_vhcd(hcd);
	int ret = 0;

	dprintk(D_PM, "Bus resume\n");

	spin_lock_irq(&vhcd->lock);
	if (!HCD_HW_ACCESSIBLE(hcd)) {
		ret = -ESHUTDOWN;
	} else {
		vhcd->rh_state = VUSB_RH_RUNNING;
		vhcd->hcd_state = VUSB_HCD_RUNNING;
		hcd->state = HC_STATE_RUNNING;
	}
	spin_unlock_irq(&vhcd->lock);

	return ret;
}
#endif /* CONFIG_PM */

static const struct hc_driver vusb_hcd_driver = {
	.description = VUSB_HCD_DRIVER_NAME,
	.product_desc =	VUSB_DRIVER_DESC,
	.hcd_priv_size = sizeof(struct vusb_vhcd),

	.flags = HCD_USB2,

	/* .reset not used since our HCD is so simple, everything is done in start */
	.start = vusb_hcd_start,
	.stop =	vusb_hcd_stop,

	.urb_enqueue = vusb_hcd_urb_enqueue,
	.urb_dequeue = vusb_hcd_urb_dequeue,

	.get_frame_number = vusb_hcd_get_frame,

	.hub_status_data = vusb_hcd_hub_status,
	.hub_control = vusb_hcd_hub_control,
#ifdef CONFIG_PM
	.bus_suspend = vusb_hcd_bus_suspend,
	.bus_resume = vusb_hcd_bus_resume,
#endif /* CONFIG_PM */
};

/****************************************************************************/
/* Ring Processing                                                          */

static struct vusb_shadow*
vusb_get_shadow(struct vusb_device *vdev)
{
	if (!vdev->shadow_free) {
		printk(KERN_ERR "Requesting shadow when shadow_free == 0!\n");
		return NULL;
	}

	vdev->shadow_free--;

	if (vdev->shadows[vdev->shadow_free_list[vdev->shadow_free]].in_use) {
		printk(KERN_ERR "Requesting shadow at %d which is in use!\n",
			vdev->shadow_free);
		return NULL;
	}

	vdev->shadows[vdev->shadow_free_list[vdev->shadow_free]].in_use = 1;
	vdev->shadows[vdev->shadow_free_list[vdev->shadow_free]].req.nr_segments = 0;
	vdev->shadows[vdev->shadow_free_list[vdev->shadow_free]].req.nr_packets = 0;
	vdev->shadows[vdev->shadow_free_list[vdev->shadow_free]].req.flags = 0;
	vdev->shadows[vdev->shadow_free_list[vdev->shadow_free]].req.length = 0;

	return &vdev->shadows[vdev->shadow_free_list[vdev->shadow_free]];
}

static void
vusb_put_shadow(struct vusb_device *vdev, struct vusb_shadow *shadow)
{
	usbif_indirect_request_t *ireq;
	int i, j;

	if (!shadow->in_use) {
		printk(KERN_ERR "Returning shadow %p that is not in use to list!\n",
			shadow);
		return;
	}

	/* If the iso_packet_info has not been detached for the urbp, take
	 * care of it here. */
	if (shadow->iso_packet_info) {
		kfree(shadow->iso_packet_info);
		shadow->iso_packet_info = NULL;
	}

	/* If a direct data transfer was done, don't try to free any grefs */
	if (shadow->req.flags & USBIF_F_DIRECT_DATA)
		goto no_gref;

	/* N.B. it turns out he readonly param to gnttab_end_foreign_access is,
	 * unused, that is why we don't have to track it and use it here. */
	if (shadow->indirect_reqs) {
		ireq = (usbif_indirect_request_t*)shadow->indirect_reqs;

		for (i = 0; i < shadow->req.nr_segments; i++) {
			for (j = 0; j < ireq[j].nr_segments; j++) {
				gnttab_end_foreign_access(ireq[i].gref[j], 0, 0UL);
			}
		}
		kfree(shadow->indirect_reqs);
		shadow->indirect_reqs = NULL;
	}

	shadow->indirect_reqs_size = 0;

	if (shadow->indirect_frames) {
		kfree(shadow->indirect_frames);
		shadow->indirect_frames = NULL;
	}

	for (i = 0; i < shadow->req.nr_segments; i++)
		gnttab_end_foreign_access(shadow->req.u.gref[i], 0, 0UL);

no_gref:

	memset(&shadow->frames[0], 0,
		(sizeof(unsigned long)*USBIF_MAX_SEGMENTS_PER_REQUEST));
	shadow->urbp = NULL;
	shadow->in_use = 0;

	if (vdev->shadow_free >= SHADOW_ENTRIES) {
		printk(KERN_ERR "Shadow free value too big: %d!\n",
			vdev->shadow_free);
		return;
	}

	vdev->shadow_free_list[vdev->shadow_free] = (u16)shadow->req.id;
	vdev->shadow_free++;
}

static void
vusb_grant_available_callback(void *arg)
{
	struct vusb_device *vdev = (struct vusb_device*)arg;
	schedule_work(&vdev->work);
}

#define BYTE_OFFSET(a) ((u32)((unsigned long)a & (PAGE_SIZE - 1)))
#define SPAN_PAGES(a, s) ((u32)((s >> PAGE_SHIFT) + ((BYTE_OFFSET(a) + BYTE_OFFSET(s) + PAGE_SIZE - 1) >> PAGE_SHIFT)))

static int
vusb_allocate_grefs(struct vusb_device *vdev, struct vusb_shadow *shadow,
		void *addr, u32 length, bool restart)
{
	grant_ref_t gref_head;
	unsigned long mfn;
	u8 *va = (u8*)addr;
	u32 ref, nr_mfns = SPAN_PAGES(addr, length);
	int i, ret;

	dprintk(D_RING2, "Allocate gref for %d mfns\n", (int)nr_mfns);

	/* NOTE: we are following the blkback model where we are not
	 * freeing the pages on gnttab_end_foreign_access so we don't
	 * have to track them. That memory belongs to the USB core
	 * in most cases or is the internal request page. */

	ret = gnttab_alloc_grant_references(nr_mfns, &gref_head);
	if (ret < 0) {
		if (!restart)
			return ret;
		gnttab_request_free_callback(&vdev->callback,
			vusb_grant_available_callback,
			vdev, nr_mfns);
		return -EBUSY;
	}

	for (i = 0; i < nr_mfns; i++, va += PAGE_SIZE) {
		mfn = PFN_DOWN(arbitrary_virt_to_machine(va).maddr);

		ref = gnttab_claim_grant_reference(&gref_head);
		BUG_ON(ref == -ENOSPC);

		shadow->req.u.gref[shadow->req.nr_segments] = ref;

		gnttab_grant_foreign_access_ref(ref,
				vdev->xendev->otherend_id, mfn,
				usb_urb_dir_out(shadow->urbp->urb)); /* OUT is write, so RO */

		shadow->frames[i] = mfn_to_pfn(mfn);
		shadow->req.nr_segments++;
 	}

	gnttab_free_grant_references(gref_head);

	return 0;
}

static int
vusb_allocate_indirect_grefs(struct vusb_device *vdev,
			struct vusb_shadow *shadow,
			void *addr, u32 length, void *iso_addr)
{
	usbif_indirect_request_t *indirect_reqs =
		(usbif_indirect_request_t*)shadow->indirect_reqs;
	unsigned long mfn, iso_mfn;
	u32 nr_mfns = SPAN_PAGES(addr, length);
	grant_ref_t gref_head;
	u8 *va = (u8*)addr;
	u32 nr_total = nr_mfns + (iso_addr ? 1 : 0);
	u32 ref;
	int iso_frame = (iso_addr ? 1 : 0);
	int ret, i = 0, j = 0, k = 0;

	BUG_ON(!indirect_reqs);
	BUG_ON(!shadow->indirect_frames);

	/* This routine cannot be called multiple times for a given shadow
	 * buffer where vusb_allocate_grefs can. */
	shadow->req.nr_segments = 0;

	/* Set up the descriptors for the indirect pages in the request. */
	ret = vusb_allocate_grefs(vdev, shadow, indirect_reqs,
			shadow->indirect_reqs_size, true);
	if (ret)
		return ret; /* may just be EBUSY */

	ret = gnttab_alloc_grant_references(nr_total, &gref_head);
	if (ret < 0) {
		gnttab_request_free_callback(&vdev->callback,
			vusb_grant_available_callback,
			vdev, nr_total);
		/* Clean up what we did above */
		ret = -EBUSY;
		goto cleanup;
	}

	/* Set up the descriptor for the iso packets - it is the first page of
	 * the first indirect page. The first gref of the first page points
	 * to the iso packet descriptor page. */
	if (iso_addr) {
		iso_mfn = PFN_DOWN(arbitrary_virt_to_machine(iso_addr).maddr);

		ref = gnttab_claim_grant_reference(&gref_head);
		BUG_ON(ref == -ENOSPC);

		indirect_reqs[0].gref[0] = ref;

		gnttab_grant_foreign_access_ref(ref,
				vdev->xendev->otherend_id, iso_mfn,
				usb_urb_dir_out(shadow->urbp->urb)); /* OUT is write, so RO */

		shadow->indirect_frames[0] = mfn_to_pfn(iso_mfn);
		indirect_reqs[0].nr_segments++;
		j++;
	}

	for ( ; i < nr_mfns; i++, va += PAGE_SIZE) {
		mfn = PFN_DOWN(arbitrary_virt_to_machine(va).maddr);

		ref = gnttab_claim_grant_reference(&gref_head);
		BUG_ON(ref == -ENOSPC);

		indirect_reqs[j].gref[k] = ref;

		gnttab_grant_foreign_access_ref(ref,
				vdev->xendev->otherend_id, mfn,
				usb_urb_dir_out(shadow->urbp->urb)); /* OUT is write, so RO */

		shadow->indirect_frames[i + iso_frame] = mfn_to_pfn(mfn);
		indirect_reqs[j].nr_segments++;
		if (++k ==  USBIF_MAX_SEGMENTS_PER_IREQUEST) {
			indirect_reqs[++j].nr_segments = 0;
			k = 0;
		}
 	}

	gnttab_free_grant_references(gref_head);

	return 0;

cleanup:
	for (i = 0; i < shadow->req.nr_segments; i++)
		gnttab_end_foreign_access(shadow->req.u.gref[i], 0, 0UL);

	memset(&shadow->frames[0], 0,
		(sizeof(unsigned long)*USBIF_MAX_SEGMENTS_PER_REQUEST));
	shadow->req.nr_segments = 0;

	return ret;
}

static void
vusb_flush_ring(struct vusb_device *vdev)
{
	int notify;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&vdev->ring, notify);

	/* TODO see OXT-311. The backend is not properly checking for more work
	 * to do so it currently unconditionally notifying. This means the
	 * front end must do this for now. This explains a bunch of hangs and
	 * timeouts.
	 */
	/*if (notify)*/
		notify_remote_via_irq(vdev->irq);
}

static void
vusb_put_ring(struct vusb_device *vdev, struct vusb_shadow *shadow)
{
	usbif_request_t *req;

	/* If we have a shadow allocation, we know there is space on the ring */
	req = RING_GET_REQUEST(&vdev->ring, vdev->ring.req_prod_pvt);
	memcpy(req, &shadow->req, sizeof(usbif_request_t));

	vdev->ring.req_prod_pvt++;

	vusb_flush_ring(vdev);
}

static int
vusb_put_internal_request(struct vusb_device *vdev,
		enum vusb_internal_cmd cmd, u64 cancel_id)
{
	struct vusb_shadow *shadow;

	/* NOTE USBIF_T_ABORT_PIPE is not currently supported */

	/* Internal request can use the last entry */
	if (vdev->shadow_free == 0)
		return -ENOMEM;

	shadow = vusb_get_shadow(vdev);
	BUG_ON(!shadow);

	shadow->urbp = NULL;
	shadow->req.endpoint = 0;
	shadow->req.length = 0;
	shadow->req.offset = 0;
	shadow->req.nr_segments = 0;
	shadow->req.setup = 0L;
	shadow->req.flags = 0;

	if (cmd == VUSB_CMD_RESET || cmd == VUSB_CMD_CYCLE) {
		/* Resets/cycles are easy - no response data. Always use the
		 * reset type even though the backed relies on the flags. */
		shadow->req.type = USBIF_T_RESET;
		shadow->req.flags = ((cmd == VUSB_CMD_CYCLE) ?
			USBIF_F_CYCLE_PORT : USBIF_F_RESET);
	}
	else if (cmd == VUSB_CMD_SPEED) {
		/* Speed requests use the data field in the response */
		shadow->req.endpoint = 0 | USB_DIR_IN;
		shadow->req.type = USBIF_T_GET_SPEED;
	}
	else if (cmd == VUSB_CMD_CANCEL) {
		/* Cancel requests have to set the request ID */
		shadow->req.type = USBIF_T_CANCEL;
		shadow->req.flags = USBIF_F_DIRECT_DATA;
		*((u64*)(&shadow->req.u.data[0])) = cancel_id;
	}
	else
		return -EINVAL;

	vusb_put_ring(vdev, shadow);

	return 0;
}

static int
vusb_put_urb(struct vusb_device *vdev, struct vusb_urbp *urbp)
{
	struct vusb_shadow *shadow;
	struct urb *urb = urbp->urb;
	u32 nr_mfns = 0, nr_ind_pages;
	int ret = 0;

	BUG_ON(!urb);

	/* Leave room for resets on ring */
	if (vdev->shadow_free <= 1)
		return -EAGAIN;

	shadow = vusb_get_shadow(vdev);
	BUG_ON(!shadow);

	if (!(urb->transfer_flags & URB_SHORT_NOT_OK) && usb_urb_dir_in(urb))
		shadow->req.flags |= USBIF_F_SHORTOK;

	/* Set the urbp and req ID to the shadow value */
	shadow->urbp = urbp;
	urbp->id = shadow->req.id;

	/* Is there any data to transfer, e.g. a control transaction may
	 * just be the setup packet. */
	if (urb->transfer_buffer_length > 0)
		nr_mfns = SPAN_PAGES(urb->transfer_buffer,
				urb->transfer_buffer_length);

	if (nr_mfns > USBIF_MAX_SEGMENTS_PER_REQUEST) {
		/* Need indirect support here, only used with bulk transfers */
		if (!usb_pipebulk(urb->pipe)) {
			eprintk("%p(%s) too many pages for non-bulk transfer: %d\n",
				vdev, vdev->xendev->nodename, nr_mfns);
			ret = -E2BIG;
			goto err;
		}

		if (nr_mfns > MAX_PAGES_FOR_INDIRECT_REQUEST) {
			eprintk("%p(%s) too many pages for any transfer: %d\n",
				vdev, vdev->xendev->nodename, nr_mfns);
			ret = -E2BIG;
			goto err;
		}

		nr_ind_pages = INDIRECT_PAGES_REQUIRED(nr_mfns);
		shadow->indirect_reqs_size = nr_ind_pages*PAGE_SIZE;
		shadow->indirect_reqs =
			kmalloc(nr_ind_pages*PAGE_SIZE,
				GFP_ATOMIC);
		shadow->indirect_frames =
			kmalloc(nr_ind_pages*USBIF_MAX_SEGMENTS_PER_IREQUEST,
				GFP_ATOMIC);
		if (!shadow->indirect_reqs || !shadow->indirect_frames) {
			eprintk("%s out of memory\n", __FUNCTION__);
			ret = -ENOMEM;
			goto err;
		}

		ret = vusb_allocate_indirect_grefs(vdev, shadow,
						urb->transfer_buffer,
						urb->transfer_buffer_length,
						NULL);
		if (ret) {
			eprintk("%s failed to alloc indirect grefs\n", __FUNCTION__);
			/* Have to free this here to prevent vusb_put_shadow
			 * from trying to return grefs */
			kfree(shadow->indirect_reqs);
			shadow->indirect_reqs = NULL;
			goto err;
		}
		shadow->req.flags |= USBIF_F_INDIRECT;

	}
	else if (nr_mfns > 0) {
		ret = vusb_allocate_grefs(vdev, shadow,
					urb->transfer_buffer,
					urb->transfer_buffer_length,
					true);
		if (ret) {
			if (ret != -EBUSY)
				eprintk("%s failed to alloc grefs\n",
					__FUNCTION__);
			goto err;
		}
	}

	/* Setup the request for the ring */
	shadow->req.type = vusb_pipe_type_to_optype(usb_pipetype(urb->pipe));
	shadow->req.endpoint = usb_pipeendpoint(urb->pipe);
	shadow->req.endpoint |= usb_urb_dir_in(urb) ? USB_DIR_IN : 0;
	shadow->req.offset = BYTE_OFFSET(urb->transfer_buffer);
	shadow->req.length = urb->transfer_buffer_length;
	shadow->req.nr_packets = 0;
	shadow->req.startframe = 0;
	if (usb_pipecontrol(urb->pipe) && urb->setup_packet)
		memcpy(&shadow->req.setup, urb->setup_packet, 8);
	else
		shadow->req.setup = 0L;

	vusb_put_ring(vdev, shadow);

	return 0;
err:
	/* This will clean up any stuffs allocated above */
	vusb_put_shadow(vdev, shadow);
	return ret;
}

static int
vusb_put_isochronous_urb(struct vusb_device *vdev, struct vusb_urbp *urbp)
{
	struct vusb_shadow *shadow;
	struct urb *urb = urbp->urb;
	usbif_iso_packet_info_t *iso_packets;
	u32 nr_mfns = 0, nr_ind_pages;
	u16 seg_length;
	int ret = 0, i;

	BUG_ON(!urb);

	/* Leave room for resets on ring */
	if (vdev->shadow_free <= 1)
		return -EAGAIN;

	shadow = vusb_get_shadow(vdev);
	BUG_ON(!shadow);

	if (!(urb->transfer_flags & URB_SHORT_NOT_OK) && usb_urb_dir_in(urb))
		shadow->req.flags |= USBIF_F_SHORTOK;

	if (urb->transfer_flags & URB_ISO_ASAP)
		shadow->req.flags |= USBIF_F_ASAP;

	/* Set the urbp and req ID to the shadow value */
	shadow->urbp = urbp;
	urbp->id = shadow->req.id;

	iso_packets = (usbif_iso_packet_info_t*)kzalloc(PAGE_SIZE, GFP_ATOMIC);
	if (!iso_packets) {
		ret = -ENOMEM;
		goto err;
	}

	seg_length = (u16)urb->transfer_buffer_length/urb->number_of_packets;
	for (i = 0; i < urb->number_of_packets; i++) {
		iso_packets[i].offset = urb->iso_frame_desc[i].offset;
		iso_packets[i].length = seg_length;
	}

	shadow->iso_packet_info = iso_packets;

	nr_mfns = SPAN_PAGES(urb->transfer_buffer, urb->transfer_buffer_length);
	if (nr_mfns == 0) {
		eprintk("ISO URB urbp: %p with no data buffers\n", urbp);
		ret = -EINVAL;
		goto err;
	}

	if (nr_mfns > USBIF_MAX_ISO_SEGMENTS) {
		if (nr_mfns > MAX_PAGES_FOR_INDIRECT_ISO_REQUEST) {
			eprintk("%p(%s) too many pages for ISO transfer: %d\n",
				vdev, vdev->xendev->nodename, nr_mfns);
			ret = -E2BIG;
			goto err;
		}

		/* +1 for the ISO packet page */
		nr_ind_pages = INDIRECT_PAGES_REQUIRED(nr_mfns + 1);
		shadow->indirect_reqs_size = nr_ind_pages*PAGE_SIZE;
		shadow->indirect_reqs =
			kmalloc(nr_ind_pages*PAGE_SIZE,
				GFP_ATOMIC);
		shadow->indirect_frames =
			kmalloc(nr_ind_pages*USBIF_MAX_SEGMENTS_PER_IREQUEST,
				GFP_ATOMIC);
		if (!shadow->indirect_reqs || !shadow->indirect_frames) {
			eprintk("%s out of memory\n", __FUNCTION__);
			ret = -ENOMEM;
			goto err;
		}

		ret = vusb_allocate_indirect_grefs(vdev, shadow,
						urb->transfer_buffer,
						urb->transfer_buffer_length,
						iso_packets);
		if (ret) {
			eprintk("%s failed to alloc ISO indirect grefs\n", __FUNCTION__);
			/* Have to free this here to prevent vusb_put_shadow
			 * from trying to return grefs */
			kfree(shadow->indirect_reqs);
			shadow->indirect_reqs = NULL;
			goto err;
		}
		shadow->req.flags |= USBIF_F_INDIRECT;
	}
	else {
		/* Setup ISO packet page */
		ret = vusb_allocate_grefs(vdev, shadow,
					iso_packets,
					PAGE_SIZE,
					true);
		if (ret) {
			if (ret != -EBUSY)
				eprintk("%s failed to alloc ISO packet grefs\n",
					__FUNCTION__);
			eprintk("%s failed to alloc grefs\n", __FUNCTION__);
			goto err;
		}

		/* The rest are for the data segments */
		ret = vusb_allocate_grefs(vdev, shadow,
					urb->transfer_buffer,
					urb->transfer_buffer_length,
					true);
		if (ret) {
			if (ret != -EBUSY)
				eprintk("%s failed to alloc grefs\n",
					__FUNCTION__);
			goto err;
		}
	}

	/* Setup the request for the ring */
	shadow->req.type = USBIF_T_ISOC;
	shadow->req.endpoint = usb_pipeendpoint(urb->pipe);
	shadow->req.endpoint |= usb_urb_dir_in(urb) ? USB_DIR_IN : 0;
	shadow->req.offset = BYTE_OFFSET(urb->transfer_buffer);
	shadow->req.length = urb->transfer_buffer_length;
	shadow->req.nr_packets = urb->number_of_packets;
	shadow->req.startframe = urb->start_frame;
	shadow->req.setup = 0L;

	vusb_put_ring(vdev, shadow);

	return 0;
err:
	/* This will clean up any stuffs allocated above */
	vusb_put_shadow(vdev, shadow);
	return ret;
}

/****************************************************************************/
/* URB Processing                                                           */

#ifdef VUSB_DEBUG
/* Dump URBp */
static inline void
vusb_urbp_dump(struct vusb_urbp *urbp)
{
	struct urb *urb = urbp->urb;
	unsigned int type;

	type = usb_pipetype(urb->pipe);

	iprintk("URB urbp: %p state: %s status: %d pipe: %s(%u)\n",
		urbp, vusb_state_to_string(urbp),
		urb->status, vusb_pipe_to_string(urb), type);
	iprintk("device: %u endpoint: %u in: %u\n",
		usb_pipedevice(urb->pipe), usb_pipeendpoint(urb->pipe),
		usb_urb_dir_in(urb));
}
#endif /* VUSB_DEBUG */

static void
vusb_urbp_release(struct vusb_vhcd *vhcd, struct vusb_urbp *urbp)
{
	struct urb *urb = urbp->urb;

	/* Notify USB stack that the URB is finished and release it. This
	 * has to be done outside the all locks. */

#ifdef VUSB_DEBUG
	if (urb->status)
		vusb_urbp_dump(urbp);
#endif

	dprintk(D_URB2, "Giveback URB urpb: %p status %d length %u\n",
		urbp, urb->status, urb->actual_length);
	if (urbp->iso_packet_info)
		kfree(urbp->iso_packet_info);
	kfree(urbp);
	usb_hcd_unlink_urb_from_ep(vhcd_to_hcd(vhcd), urb);
	usb_hcd_giveback_urb(vhcd_to_hcd(vhcd), urb, urb->status);
}

static void
vusb_urbp_queue_release(struct vusb_device *vdev, struct vusb_urbp *urbp)
{
	/* Remove from the active urbp list and place it on the release list.
	 * Called from the urb processing routines holding the vdev lock. */
	list_del(&urbp->urbp_list);

	list_add_tail(&urbp->urbp_list, &vdev->release_list);

	schedule_work(&vdev->work);
}

/* Convert status to errno */
static int
vusb_status_to_errno(u32 status)
{
	switch (status) {
	case USBIF_RSP_OKAY:
		return 0;
	case USBIF_RSP_EOPNOTSUPP:
		return -ENOENT;
	case USBIF_RSP_USB_CANCELED:
		return -ECANCELED;
	case USBIF_RSP_USB_PENDING:
		return -EINPROGRESS;
	case USBIF_RSP_USB_PROTO:
		return -EPROTO;
	case USBIF_RSP_USB_CRC:
		return -EILSEQ;
	case USBIF_RSP_USB_TIMEOUT:
		return -ETIME;
	case USBIF_RSP_USB_STALLED:
		return -EPIPE;
	case USBIF_RSP_USB_INBUFF:
		return -ECOMM;
	case USBIF_RSP_USB_OUTBUFF:
		return -ENOSR;
	case USBIF_RSP_USB_OVERFLOW:
		return -EOVERFLOW;
	case USBIF_RSP_USB_SHORTPKT:
		return -EREMOTEIO;
	case USBIF_RSP_USB_DEVRMVD:
		return -ENODEV;
	case USBIF_RSP_USB_PARTIAL:
		return -EXDEV;
	case USBIF_RSP_USB_INVALID:
		return -EINVAL;
	case USBIF_RSP_USB_RESET:
		return -ECONNRESET;
	case USBIF_RSP_USB_SHUTDOWN:
		return -ESHUTDOWN;
	case USBIF_RSP_ERROR:
	case USBIF_RSP_USB_UNKNOWN:
	default:
		return -EIO;
	}
}

static void
vusb_urb_common_finish(struct vusb_device *vdev, struct vusb_urbp *urbp,
			bool in)
{
	struct urb *urb = urbp->urb;

	/* If the URB was canceled and shot down in the backend then
	 * just use the error code set in dequeue and don't bother
	 * setting values. */
	if (urbp->state == VUSB_URBP_CANCEL)
		return;

	urb->status = vusb_status_to_errno(urbp->rsp.status);
	if (unlikely(urb->status)) {
		wprintk("Failed %s URB urbp: %p urb: %p status: %d\n",
			vusb_dir_to_string(in), urbp, urb, urb->status);
		return;
	}

	dprintk(D_URB2, "%s URB completed status %d len %u\n",
		vusb_dir_to_string(in), urb->status, urbp->rsp.actual_length);

	/* Sanity check on len, should be less or equal to
	 * the length of the transfer buffer */
	if (unlikely(in && urbp->rsp.actual_length >
		urb->transfer_buffer_length)) {
		wprintk("IN URB too large (expect %u got %u)\n",
			urb->transfer_buffer_length,
			urbp->rsp.actual_length);
		urb->status = -EIO;
		return;
	}

	/* Set to what the backend said we sent or received */
	urb->actual_length = urbp->rsp.actual_length;
}

static void
vusb_urb_control_finish(struct vusb_device *vdev, struct vusb_urbp *urbp)
{
	struct urb *urb = urbp->urb;
	struct usb_ctrlrequest *ctrl
		= (struct usb_ctrlrequest *)urb->setup_packet;
	u8 *buf = (u8*)urb->transfer_buffer;
	bool in;

	/* This is fun. If a USB 3 device is in a USB 3 port, we get a USB 3
	 * device descriptor. Since we are a USB 2 HCD, things get unhappy
	 * above us. So this code will make the descriptor look more
	 * USB 2ish by fixing the bcdUSB  and bMaxPacketSize0
	 */
	if (vdev->is_ss && ctrl->bRequest == USB_REQ_GET_DESCRIPTOR &&
		(ctrl->wValue & 0xff00) == 0x0100 &&
		urbp->rsp.actual_length >= 0x12 &&
		buf[1] == 0x01 && buf[3] == 0x03) {
		iprintk("Modifying USB 3 device descriptor to be USB 2\n");
		buf[2] = 0x10;
		buf[3] = 0x02;
		buf[7] = 0x40;
	}

	/* Get direction of control request and do common finish */
	in = ((ctrl->bRequestType & USB_DIR_IN) != 0) ? true : false;
	vusb_urb_common_finish(vdev, urbp, in);
}

static void
vusb_urb_isochronous_finish(struct vusb_device *vdev, struct vusb_urbp *urbp,
				bool in)
{
	struct urb *urb = urbp->urb;
	struct usb_iso_packet_descriptor *iso_desc = &urb->iso_frame_desc[0];
	u32 total_length = 0, packet_length;
	int i;

	BUG_ON(!urbp->iso_packet_info);

	/* Same for ISO URBs, clear everything, set the status and release */
	if (urbp->state == VUSB_URBP_CANCEL) {
		urb->status = -ECANCELED;
		goto iso_err;
	}

	urb->status = vusb_status_to_errno(urbp->rsp.status);

	/* Did the entire ISO request fail? */
	if (urb->status)
		goto iso_err;

	/* Reset packet error count */
	urb->error_count = 0;

	for (i = 0; i < urb->number_of_packets; i++) {
		packet_length = urbp->iso_packet_info[i].length;

		/* Sanity check on packet length */
		if (packet_length > iso_desc[i].length) {
			wprintk("ISO packet %d too much data\n", i);
			goto iso_io;
		}

		iso_desc[i].actual_length = packet_length;
		iso_desc[i].status =
			vusb_status_to_errno(urbp->iso_packet_info[i].status);
		iso_desc[i].offset = urbp->iso_packet_info[i].offset;

		/* Do sanity check each time on effective data length */
		if ((in) && (urb->transfer_buffer_length <
				(total_length + packet_length))) {
			wprintk("ISO response %d to much data - "
				"expected %u got %u\n",
				i, total_length + packet_length,
				urb->transfer_buffer_length);
				goto iso_err;
		}

		if (!iso_desc[i].status)
			total_length += packet_length;
		else
			urb->error_count++;
	}

	/* Check for new start frame */
	if (urb->transfer_flags & URB_ISO_ASAP)
		urb->start_frame = urbp->rsp.data;

	urb->actual_length = total_length;
	dprintk(D_URB2, "ISO response urbp: %s total: %u errors: %d\n",
		urbp, total_length, urb->error_count);

	return;

iso_io:
	urb->status = -EIO;
iso_err:
	for (i = 0; i < urb->number_of_packets; i++) {
		urb->iso_frame_desc[i].actual_length = 0;
		urb->iso_frame_desc[i].status = urb->status;
	}
	urb->actual_length = 0;
}

static void
vusb_urb_finish(struct vusb_device *vdev, struct vusb_urbp *urbp)
{
	struct vusb_shadow *shadow;
	struct urb *urb = urbp->urb;
	int type = usb_pipetype(urb->pipe);
	bool in;

	shadow = &vdev->shadows[urbp->rsp.id];

		in = usb_urb_dir_in(urbp->urb) ? true : false;

	switch (type) {
	case PIPE_CONTROL:
		vusb_urb_control_finish(vdev, urbp);
		break;
	case PIPE_ISOCHRONOUS:
		vusb_urb_isochronous_finish(vdev, urbp, in);
		break;
	case PIPE_INTERRUPT:
	case PIPE_BULK:
		vusb_urb_common_finish(vdev, urbp, in);
		break;
	default:
		eprintk("Unknown pipe type %u\n", type);
	}

	/* Done with this shadow entry, give it back */
	vusb_put_shadow(vdev, shadow);

	/* No matter what, move this urbp to the release list */
	urbp->state = VUSB_URBP_DONE;
	vusb_urbp_queue_release(vdev, urbp);
}

static void
vusb_send(struct vusb_device *vdev, struct vusb_urbp *urbp, int type)
{
	int ret = (type != PIPE_ISOCHRONOUS) ?
		vusb_put_urb(vdev, urbp) :
		vusb_put_isochronous_urb(vdev, urbp);
	switch (ret) {
	case 0:
		urbp->state = VUSB_URBP_SENT;
		break;
	case -EAGAIN:
		schedule_work(&vdev->work);
	case -EBUSY:
		/* grant callback restarts work */
		break;
	default:
		urbp->state = VUSB_URBP_DROP;
		urbp->urb->status = ret;
	}
}

static void
vusb_send_control_urb(struct vusb_device *vdev, struct vusb_urbp *urbp)
{
	struct urb *urb = urbp->urb;
	const struct usb_ctrlrequest *ctrl;
	u16 ctrl_tr, ctrl_value;

	/* Convenient aliases on setup packet*/
	ctrl = (struct usb_ctrlrequest *)urb->setup_packet;
	ctrl_tr = (ctrl->bRequestType << 8) | ctrl->bRequest;
	ctrl_value = le16_to_cpu(ctrl->wValue);

	dprintk(D_URB2, "Send Control URB dev: %u in: %u cmd: 0x%x 0x%02x\n",
		usb_pipedevice(urb->pipe), ((ctrl->bRequestType & USB_DIR_IN) != 0),
		ctrl->bRequest, ctrl->bRequestType);
	dprintk(D_URB2, "SETUP packet, tb_len=%d\n",
		urb->transfer_buffer_length);
	dprint_hex_dump(D_URB2, "SET: ",
		DUMP_PREFIX_OFFSET, 16, 1, ctrl, 8, true);

	/* The only special case it a set address request. We can't actually
	 * let the guest do this in the backend - it would cause mayhem */
	if (ctrl_tr == (DeviceOutRequest | USB_REQ_SET_ADDRESS)) {
		vdev->address = ctrl_value;
		dprintk(D_URB1, "SET ADDRESS %u\n", vdev->address);
		urb->status = 0;
		urbp->state = VUSB_URBP_DONE;
		return;
	}

	vusb_send(vdev, urbp, PIPE_CONTROL);
}

static void
vusb_send_urb(struct vusb_device *vdev, struct vusb_urbp *urbp)
{
	struct urb *urb = urbp->urb;
	unsigned int type = usb_pipetype(urb->pipe);

	dprintk(D_URB2, "Send URB urbp: %p state: %s pipe: %s(t:%u e:%u d:%u)\n",
		urbp, vusb_state_to_string(urbp),
		vusb_pipe_to_string(urb), type, usb_pipeendpoint(urb->pipe),
		usb_urb_dir_in(urb));

	if (urbp->state == VUSB_URBP_NEW) {
		switch (type) {
		case PIPE_CONTROL:
			vusb_send_control_urb(vdev, urbp);
			break;
		case PIPE_ISOCHRONOUS:
		case PIPE_INTERRUPT:
		case PIPE_BULK:
			vusb_send(vdev, urbp, type);
			break;
		default:
			wprintk("Unknown urb type %x\n", type);
			urbp->state = VUSB_URBP_DROP;
			urb->status = -ENODEV;
		}
	}

	/* This will pick up canceled urbp's from dequeue too */
	if (urbp->state == VUSB_URBP_DONE ||
		urbp->state == VUSB_URBP_DROP) {
		/* Remove URB */
		dprintk(D_URB1, "URB immediate %s\n",
			vusb_state_to_string(urbp));
		vusb_urbp_queue_release(vdev, urbp);
	}
}

/****************************************************************************/
/* VUSB Port                                                                */

static bool
vusb_start_processing_caller(struct vusb_rh_port *vport, const char *caller)
{
	struct vusb_vhcd *vhcd = vport->vhcd;
	unsigned long flags;

	spin_lock_irqsave(&vhcd->lock, flags);

	if (vhcd->hcd_state == VUSB_HCD_INACTIVE || !vport->present) {
		spin_unlock_irqrestore(&vhcd->lock, flags);
		eprintk("%s called start processing - device %p "
			"invalid state - vhcd: %d vport: %d\n",
			caller, &vport->vdev, vhcd->hcd_state, vport->present);
		return false;
	}

	if (vport->closing) {
		/* Normal, shutdown of this device pending */
		spin_unlock_irqrestore(&vhcd->lock, flags);
		return false;
	}

	vport->processing++;
	spin_unlock_irqrestore(&vhcd->lock, flags);

	return true;
}

static void
vusb_stop_processing(struct vusb_rh_port *vport)
{
	struct vusb_vhcd *vhcd = vport->vhcd;
	unsigned long flags;

	spin_lock_irqsave(&vhcd->lock, flags);
	vport->processing--;
	spin_unlock_irqrestore(&vhcd->lock, flags);
}

static void
vusb_wait_stop_processing(struct vusb_rh_port *vport)
{
	struct vusb_vhcd *vhcd = vport->vhcd;
	unsigned long flags;

again:
	spin_lock_irqsave(&vhcd->lock, flags);

	if (vport->processing > 0) {
		spin_unlock_irqrestore(&vhcd->lock, flags);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
		goto again;
	}

	spin_unlock_irqrestore(&vhcd->lock, flags);
}

static void
vusb_process_reset(struct vusb_rh_port *vport)
{
	struct vusb_vhcd *vhcd = vport->vhcd;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&vport->vdev.lock, flags);
	ret = vusb_put_internal_request(&vport->vdev, VUSB_CMD_RESET, 0);
	spin_unlock_irqrestore(&vport->vdev.lock, flags);

	if (ret) {
		eprintk("Failed to send reset for device %p on port %d - ret: %d\n",
			&vport->vdev, vport->port, ret);
		return;
	}

	/* Wait for the reset with no lock */
	wait_event_interruptible(vport->wait_queue, (vport->reset_done));

	iprintk("Reset complete for vdev: %p on port: %d\n",
		&vport->vdev, vport->port);

	/* Reset the reset gate */
	vport->reset_done = false;
	atomic_set(&vport->reset_pending, 0);

	spin_lock_irqsave(&vhcd->lock, flags);
	/* Signal reset completion */
	vport->port_status |= (USB_PORT_STAT_C_RESET << 16);

	vusb_set_link_state(vport);
	spin_unlock_irqrestore(&vhcd->lock, flags);

	/* Update RH outside of critical section */
	usb_hcd_poll_rh_status(vhcd_to_hcd(vhcd));
}

static void
vusb_port_work_handler(struct work_struct *work)
{
	struct vusb_rh_port *vport = container_of(work, struct vusb_rh_port, work);

	if (!vusb_start_processing(vport))
		return;

	/* Process port/device reset in port work */
	vusb_process_reset(vport);

	vusb_stop_processing(vport);
}

/****************************************************************************/
/* VUSB Devices                                                             */

static void
vusb_process(struct vusb_device *vdev, struct vusb_urbp *urbp)
{
	struct vusb_vhcd *vhcd = vusb_vhcd_by_vdev(vdev);
	struct vusb_urbp *pos;
	struct vusb_urbp *next;
	struct list_head tmp;
	unsigned long flags;

	INIT_LIST_HEAD(&tmp);

	spin_lock_irqsave(&vdev->lock, flags);

	/* Always drive any response processing since this could make room for
	 * requests. */
	list_for_each_entry_safe(pos, next, &vdev->finish_list, urbp_list) {
		vusb_urb_finish(vdev, pos);
	}

	/* New URB, queue it at the back */
	if (urbp)
		list_add_tail(&urbp->urbp_list, &vdev->pending_list);

	/* Drive request processing */
	list_for_each_entry_safe(pos, next, &vdev->pending_list, urbp_list) {
		/* Work scheduled if 1 or more URBs cannot be sent */
		vusb_send_urb(vdev, pos);
	}

	/* Copy off any urbps on the release list that need releasing */
	list_splice_init(&vdev->release_list, &tmp);

	spin_unlock_irqrestore(&vdev->lock, flags);

	/* Clean them up outside the lock */
	list_for_each_entry_safe(pos, next, &tmp, urbp_list) {
		vusb_urbp_release(vhcd, pos);
	}
}

static void
vusb_device_work_handler(struct work_struct *work)
{
	struct vusb_device *vdev = container_of(work, struct vusb_device, work);
	struct vusb_rh_port *vport = vusb_vport_by_vdev(vdev);

	if (!vusb_start_processing(vport))
		return;

	/* Start request processing again */
	vusb_process(vdev, NULL);

	vusb_stop_processing(vport);
}

static irqreturn_t
vusb_interrupt(int irq, void *dev_id)
{
	struct vusb_device *vdev = (struct vusb_device*)dev_id;
	struct vusb_rh_port *vport = vusb_vport_by_vdev(vdev);
	struct vusb_shadow *shadow;
	usbif_response_t *rsp;
	RING_IDX i, rp;
	unsigned long flags;
	bool processing = false;
	int more;

	/* Shutting down or not ready? */
	processing = vusb_start_processing(vport);

	spin_lock_irqsave(&vdev->lock, flags);

	/* If not processing, the ring code is allowed to run to
	 * do a final ring flush.
	 */
	if (!processing && !vdev->rflush) {
		spin_unlock_irqrestore(&vdev->lock, flags);
		return IRQ_HANDLED;
	}

again:
	rp = vdev->ring.sring->rsp_prod;
	rmb(); /* Ensure we see queued responses up to 'rp'. */

	for (i = vdev->ring.rsp_cons; i != rp; i++) {
		rsp = RING_GET_RESPONSE(&vdev->ring, i);

		/* Find the shadow block that goes with this req/rsp */
		shadow = &vdev->shadows[rsp->id];
		BUG_ON(!shadow->in_use);

		/* Processing internal command right here, no urbp's to queue anyway */
		if (shadow->req.type > USBIF_T_INT) {
			if (shadow->req.type == USBIF_T_GET_SPEED) {
				vdev->speed = rsp->data;
				wake_up_interruptible(&vdev->wait_queue);
			}
			else if (shadow->req.type == USBIF_T_RESET) {
				/* clear reset, wake waiter */
				vport->reset_done = true;
				wake_up_interruptible(&vport->wait_queue);
			}

			/* USBIF_T_CANCEL no waiters, no data*/

			/* Return the shadow which does almost nothing in this case */
			vusb_put_shadow(vdev, shadow);
			continue;
		}

		/* Make a copy of the response (it is small) and queue for bh.
		 * Not going to free the shadow here - that could be a lot of work;
		 * dropping it on the BH to take care of */
		memcpy(&shadow->urbp->rsp, rsp, sizeof(usbif_response_t));

		/* Detach ISO packet info from the shadow for the urbp */
		shadow->urbp->iso_packet_info = shadow->iso_packet_info;
		shadow->iso_packet_info = NULL;

		/* Remove the urbp from the pending list and attach to the
		 * finish list */
		list_del(&shadow->urbp->urbp_list);
		list_add_tail(&shadow->urbp->urbp_list, &vdev->finish_list);
	}

	vdev->ring.rsp_cons = i;

	if (i != vdev->ring.req_prod_pvt) {
		more = 0;
		RING_FINAL_CHECK_FOR_RESPONSES(&vdev->ring, more);
		if (more)
			goto again;
	} else
		vdev->ring.sring->rsp_event = i + 1;

	/* Is this a final ring flush? */
	if (unlikely(vdev->rflush)) {
		vdev->rflush = false;
		wake_up_interruptible(&vdev->wait_queue);
	}

	if (likely(processing))
		schedule_work(&vdev->work);

	spin_unlock_irqrestore(&vdev->lock, flags);

	if (likely(processing))
		vusb_stop_processing(vport);

	return IRQ_HANDLED;
}

static void
vusb_usbif_halt(struct vusb_device *vdev)
{
	struct vusb_rh_port *vport = vusb_vport_by_vdev(vdev);
	unsigned long flags;

	spin_lock_irqsave(&vdev->lock, flags);
	vdev->rflush = true;
	spin_unlock_irqrestore(&vdev->lock, flags);

	/* Disconnect gref free callback so it schedules no more work */
	gnttab_cancel_free_callback(&vdev->callback);

	/* Shutdown all work. Must be done with no locks held. */
	flush_work_sync(&vport->work);
	flush_work_sync(&vdev->work);

	/* Eat up anything left on the ring */
	while (wait_event_interruptible(vdev->wait_queue,
		vdev->ring.req_prod_pvt == vdev->ring.rsp_cons
		&& !RING_HAS_UNCONSUMED_RESPONSES(&vdev->ring))
		== -ERESTARTSYS)
		;

	/* Wait for all processing to stop now */
	vusb_wait_stop_processing(vport);
}

static void
vusb_usbif_free(struct vusb_device *vdev)
{
	struct xenbus_device *dev = vdev->xendev;

	/* Free resources associated with old device channel. */
	if (vdev->ring_ref != GRANTREF_INVALID) {
		/* This frees the page too */
		gnttab_end_foreign_access(vdev->ring_ref, 0,
					(unsigned long)vdev->ring.sring);
		vdev->ring_ref = GRANTREF_INVALID;
		vdev->ring.sring = NULL;
	}

	if (vdev->irq)
		unbind_from_irqhandler(vdev->irq, vdev);
	vdev->irq = 0;

	if (vdev->evtchn != EVTCHN_INVALID)
		xenbus_free_evtchn(dev, vdev->evtchn);
	vdev->evtchn = EVTCHN_INVALID;

	if (vdev->shadows) {
		kfree(vdev->shadows);
		vdev->shadows = NULL;
	}

	if (vdev->shadow_free_list) {
		kfree(vdev->shadow_free_list);
		vdev->shadow_free_list = NULL;
	}

	/* Unstrap vdev */
	dev_set_drvdata(&vdev->xendev->dev, NULL);
}

static int
vusb_usbif_recover(struct vusb_device *vdev)
{
	/* TODO recovery of rings and grants */
	return 0;
}

static int
vusb_setup_usbfront(struct vusb_device *vdev)
{
	struct xenbus_device *dev = vdev->xendev;
	struct usbif_sring *sring;
	int err, i;

	vdev->ring_ref = GRANTREF_INVALID;
	vdev->evtchn = EVTCHN_INVALID;
	vdev->irq = 0;

	sring = (struct usbif_sring *)__get_free_page(GFP_NOIO | __GFP_HIGH);
	if (!sring) {
		xenbus_dev_fatal(dev, -ENOMEM, "allocating shared ring");
		return -ENOMEM;
	}
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&vdev->ring, sring, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(vdev->ring.sring));
	if (err < 0) {
		free_page((unsigned long)sring);
		vdev->ring.sring = NULL;
		goto fail;
	}
	vdev->ring_ref = err;

	err = xenbus_alloc_evtchn(dev, &vdev->evtchn);
	if (err) {
		vdev->evtchn = EVTCHN_INVALID;
		goto fail;
	}

	err = bind_evtchn_to_irqhandler(vdev->evtchn, vusb_interrupt,
					USBFRONT_IRQF, "usbif", vdev);
	if (err <= 0) {
		eprintk("bind_evtchn_to_irqhandler failed device %p ret: %d\n",
			vdev, err);
		goto fail;
	}
	vdev->irq = err;

	/* Allocate the shadow buffers */
	vdev->shadows = kzalloc(sizeof(struct vusb_shadow)*SHADOW_ENTRIES,
				GFP_KERNEL);
	if (!vdev->shadows) {
		xenbus_dev_fatal(dev, err, "allocate shadows failed");
		err = -ENOMEM;
		goto fail;
	}

	vdev->shadow_free_list = kzalloc(sizeof(u16)*SHADOW_ENTRIES,
				GFP_KERNEL);
	if (!vdev->shadow_free_list) {
		xenbus_dev_fatal(dev, err, "allocate shadow list failed");
		err = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < SHADOW_ENTRIES; i++) {
		vdev->shadows[i].req.id = i;
		vdev->shadows[i].in_use = 1;
		vusb_put_shadow(vdev, &vdev->shadows[i]);
	}

	return 0;
fail:
	vusb_usbif_free(vdev);
	return err;
}

/* Common code used when first setting up, and when resuming. */
static int
vusb_talk_to_usbback(struct vusb_device *vdev)
{
	struct xenbus_device *dev = vdev->xendev;
	const char *message = NULL;
	struct xenbus_transaction xbt;
	int err;

	/* Create shared ring, alloc event channel. */
	err = vusb_setup_usbfront(vdev);
	if (err)
		goto out;

again:
	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		goto free_usbif;
	}

	err = xenbus_printf(xbt, dev->nodename,
	 		"ring-ref", "%u", vdev->ring_ref);
	if (err) {
		message = "writing ring-ref";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename,
			"event-channel", "%u", vdev->evtchn);
	if (err) {
		message = "writing event-channel";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "version", "%d",
			VUSB_INTERFACE_VERSION);
	if (err) {
		message = "writing protocol";
		goto abort_transaction;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto free_usbif;
	}

	/* Started out in the initialising state, go to initialised */
	xenbus_switch_state(dev, XenbusStateInitialised);

	return 0;

 abort_transaction:
	xenbus_transaction_end(xbt, 1);
	if (message)
		xenbus_dev_fatal(dev, err, "%s", message);
 free_usbif:
	vusb_usbif_free(vdev);
 out:
	return err;
}

static int
vusb_create_device(struct vusb_vhcd *vhcd, struct xenbus_device *dev, u16 id)
{
	int ret = 0;
	struct vusb_rh_port *vport;
	struct vusb_device *vdev;
	unsigned long flags;

	/* Find a port we can use. */
	spin_lock_irqsave(&vhcd->lock, flags);
	vport = vusb_get_vport(vhcd, id);
	spin_unlock_irqrestore(&vhcd->lock, flags);
	if (!vport)
		return -ENODEV;

	vdev = vusb_vdev_by_port(vhcd, vport->port);

	spin_lock_init(&vdev->lock);
	INIT_LIST_HEAD(&vdev->pending_list);
	INIT_LIST_HEAD(&vdev->release_list);
	INIT_LIST_HEAD(&vdev->finish_list);
	INIT_WORK(&vdev->work, vusb_device_work_handler);
	init_waitqueue_head(&vdev->wait_queue);

	/* Strap our vUSB device onto the Xen device context, etc. */
	dev_set_drvdata(&dev->dev, vdev);
	vdev->xendev = dev;

	/* Setup the rings, event channel and xenstore. Internal failures cleanup
	 * the usbif bits. Wipe the new VUSB dev and bail out. */
	ret = vusb_talk_to_usbback(vdev);
	if (ret) {
		eprintk("Failed to initialize the device - id: %d\n", id);
		dev_set_drvdata(&dev->dev, NULL);
		spin_lock_irqsave(&vhcd->lock, flags);
		vusb_put_vport(vhcd, container_of(vdev, struct vusb_rh_port, vdev));
		spin_unlock_irqrestore(&vhcd->lock, flags);
	}

	return ret;
}

static int
vusb_start_device(struct vusb_device *vdev)
{
	struct vusb_vhcd *vhcd = vusb_vhcd_by_vdev(vdev);
	struct vusb_rh_port *vport = vusb_vport_by_vdev(vdev);
	unsigned long flags;
	int ret = 0;

	/* TODO need a reset in here? The WFE gets that info from the registry */

	/* Take the VHCD lock to change the state flags */
	spin_lock_irqsave(&vhcd->lock, flags);
	vport->present = 1;
	vport->connecting = 0;
	spin_unlock_irqrestore(&vhcd->lock, flags);

	/* The rest are vdev operations with the device lock */
	spin_lock_irqsave(&vdev->lock, flags);

	vdev->speed = (unsigned int)-1;

	ret = vusb_put_internal_request(vdev, VUSB_CMD_SPEED, 0);
	if (ret) {
		spin_unlock_irqrestore(&vdev->lock, flags);
		eprintk("Failed to get device %p speed - ret: %d\n", vdev, ret);
		return ret;
	}
	spin_unlock_irqrestore(&vdev->lock, flags);

	/* Wait for a response with no lock */
	wait_event_interruptible(vdev->wait_queue, (vdev->speed != (unsigned int)-1));

	/* vdev->speed should be set, sanity check the speed */
	switch (vdev->speed) {
	case USB_SPEED_LOW:
		iprintk("Speed set to USB_SPEED_LOW for device %p", vdev);
		break;
	case USB_SPEED_FULL:
		iprintk("Speed set to USB_SPEED_FULL for device %p", vdev);
		break;
	case USB_SPEED_HIGH:
		iprintk("Speed set to USB_SPEED_HIGH for device %p", vdev);
		break;
	case USB_SPEED_SUPER:
		iprintk("Speed set to USB_SPEED_HIGH for "
			"USB_SPEED_SUPER device %p", vdev);
		vdev->speed = USB_SPEED_HIGH;
		vdev->is_ss = true;
		break;
	default:
		wprintk("Warning, setting default USB_SPEED_HIGH"
			" for device %p - original value: %d",
			vdev, vdev->speed);
		vdev->speed = USB_SPEED_HIGH;
	}

	/* Root hub port state is owned by the VHCD so use its lock */
	spin_lock_irqsave(&vhcd->lock, flags);

	vport->port_status |= vusb_speed_to_port_stat(vdev->speed)
					 | USB_PORT_STAT_CONNECTION
					 | USB_PORT_STAT_C_CONNECTION << 16;

	vusb_set_link_state(vport);

	dprintk(D_PORT1, "new status: 0x%08x speed: 0x%04x\n",
			vport->port_status, vdev->speed);

	spin_unlock_irqrestore(&vhcd->lock, flags);

	/* Update RH, this will find this port in the connected state */
	usb_hcd_poll_rh_status(vhcd_to_hcd(vhcd));

	return 0;
}

static void
vusb_destroy_device(struct vusb_device *vdev)
{
	struct vusb_vhcd *vhcd = vusb_vhcd_by_vdev(vdev);
	struct vusb_rh_port *vport = vusb_vport_by_vdev(vdev);
	struct vusb_urbp *pos;
	struct vusb_urbp *next;
	unsigned long flags;
	bool update_rh = false;

	spin_lock_irqsave(&vhcd->lock, flags);

	/* First test if it is already closing or not there,
	 * if not, set closing */
	if (vport->closing || !vport->present) {
		spin_unlock_irqrestore(&vhcd->lock, flags);
		return;
	}

	vport->closing = 1; /* Going away now... */

	/* Final vHCD operations on port - shut it down */
	vusb_set_link_state(vport);

	if (vhcd->hcd_state != VUSB_HCD_INACTIVE)
		update_rh = true;

	spin_unlock_irqrestore(&vhcd->lock, flags);
	
	/* Update root hub status, device gone, port empty */
	if (update_rh)
		usb_hcd_poll_rh_status(vhcd_to_hcd(vhcd));

	dprintk(D_PORT1, "Remove device from port %u\n", vdev->port);

	/* Main halt call, shut everything down */
	vusb_usbif_halt(vdev);

	/* NOTE removed lock here - should be OK since all processing
	 * is halted now.
	 */

	/* Process any last completed URBs left before tearing up the
	 * usbif bits.
	 */
	list_for_each_entry_safe(pos, next, &vdev->finish_list, urbp_list) {
		vusb_urb_finish(vdev, pos);
	}

	/* Main cleanup call, everyting is torn down in here */
	vusb_usbif_free(vdev);

	/* Release all the ready to release and pending URBs - this
	 * has to be done outside a lock. */
	list_for_each_entry_safe(pos, next, &vdev->release_list, urbp_list) {
		vusb_urbp_release(vhcd, pos);
	}

	/* Return any pending URBs, can't process them now... */
	list_for_each_entry_safe(pos, next, &vdev->pending_list, urbp_list) {
		pos->urb->status = -ESHUTDOWN;
		vusb_urbp_release(vhcd, pos);
	}

	/* Finally return the vport for reuse */
	spin_lock_irqsave(&vhcd->lock, flags);
	vusb_put_vport(vhcd, vport);
	spin_unlock_irqrestore(&vhcd->lock, flags);
}

/****************************************************************************/
/* VUSB Xen Devices & Driver                                                */

static int
vusb_usbfront_probe(struct xenbus_device *dev, const struct xenbus_device_id *id)
{
	struct vusb_vhcd *vhcd = hcd_to_vhcd(platform_get_drvdata(vusb_platform_device));
	int vid, err;

	/* Make device ids out of the virtual-device value from xenstore */
	err = xenbus_scanf(XBT_NIL, dev->nodename, "virtual-device", "%i", &vid);
	if (err != 1) {
		eprintk("Failed to read virtual-device value\n");
		return err;
	}

	iprintk("Creating new VUSB device - virtual-device: %i devicetype: %s\n",
		vid, id->devicetype);

	return vusb_create_device(vhcd, dev, (u16)vid);
}

static void
vusb_usbback_changed(struct xenbus_device *dev, enum xenbus_state backend_state)
{
	struct vusb_device *vdev = dev_get_drvdata(&dev->dev);

	/* Callback received when the backend's state changes. */
	dev_dbg(&dev->dev, "%s\n", xenbus_strstate(backend_state));
	dev_dbg(&dev->dev, "Mine: %s\n", xenbus_strstate(dev->state));

	switch (backend_state) {
	case XenbusStateUnknown:
		/* if the backend vanishes from xenstore, close frontend */
		if (!xenbus_exists(XBT_NIL, dev->otherend, "")) {
			/* Gone is gone, don't care about our state since we do not reconnect
			 * devices. Just destroy the device. */
			printk(KERN_INFO "backend vanished, closing frontend\n");
			xenbus_switch_state(dev, XenbusStateClosed);
			if (vdev)
				vusb_destroy_device(vdev);
		}
		break;
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
		break;
	case XenbusStateConnected:
		if (vusb_start_device(vdev)) {
			printk(KERN_ERR "failed to start frontend, aborting!\n");
			xenbus_switch_state(dev, XenbusStateClosed);
			vusb_destroy_device(vdev);
		}
		break;
	case XenbusStateInitWait:
		if (dev->state != XenbusStateInitialising && dev->state != XenbusStateClosed)
			break;
		/* Frontend drives the backend from InitWait to Connected */
		xenbus_switch_state(dev, XenbusStateConnected);
		break;
	case XenbusStateClosing:
		xenbus_frontend_closed(dev);
	case XenbusStateClosed:
		break;
	}
}

static int
vusb_xenusb_remove(struct xenbus_device *dev)
{
	struct vusb_device *vdev = dev_get_drvdata(&dev->dev);

	iprintk("xen_usbif remove %s\n", dev->nodename);

	vusb_destroy_device(vdev);

	return 0;
}

static int
vusb_usbfront_suspend(struct xenbus_device *dev)
{
	struct vusb_device *vdev = dev_get_drvdata(&dev->dev);

	iprintk("xen_usbif: pm freeze event received, detaching usbfront\n");

	/* When suspending, just halt processing, flush the ring and teardown
	 * the usbif. Leave the URB lists, vdev and vport as is.
	 */
	vusb_usbif_halt(vdev);
	vusb_usbif_free(vdev);

	return 0;
}

static int
vusb_usbfront_resume(struct xenbus_device *dev)
{
	struct vusb_device *vdev = dev_get_drvdata(&dev->dev);
	int err = 0;

	iprintk("xen_usbif: pm restore event received, unregister usb device\n");

	err = vusb_talk_to_usbback(vdev);
	if (!err)
		err = vusb_usbif_recover(vdev);

	return err;
}

static struct xenbus_device_id vusb_usbfront_ids[] = {
	{ "vusb" },
	{ "" }
};

static struct xenbus_driver vusb_usbfront_driver = {
	.name = "xc-vusb",
	.owner = THIS_MODULE,
	.ids = vusb_usbfront_ids,
	.probe = vusb_usbfront_probe,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))
	.remove = vusb_xenusb_remove,
#else
	.remove = __devexit_p(vusb_xenusb_remove),
#endif

	.suspend = vusb_usbfront_suspend,
	.resume = vusb_usbfront_resume,

	.otherend_changed = vusb_usbback_changed,
};

/****************************************************************************/
/* VUSB Platform Device & Driver                                            */

static void
vusb_platform_sanity_disable(struct vusb_vhcd *vhcd)
{
	unsigned long flags;
	u16 i = 0;

	iprintk("Disable vHCD with sanity check.\n");

	spin_lock_irqsave(&vhcd->lock, flags);

	/* Check for any vUSB devices - lotsa trouble if there are any */
	for (i = 0; i < VUSB_PORTS; i++) {
		if (!vhcd->vrh_ports[i].present)
			continue;

		/* Active vUSB device, now in a world of pain */
		eprintk("Danger! Shutting down while"
			" xenbus device at %d is present!!\n", i);
	}

	/* Shut down the vHCD */
	vhcd->hcd_state = VUSB_HCD_INACTIVE;

	spin_unlock_irqrestore(&vhcd->lock, flags);
}

/* Platform probe */
static int
vusb_platform_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd;
	int ret;
	struct vusb_vhcd *vhcd;

	if (usb_disabled())
		return -ENODEV;

	dprintk(D_MISC, ">vusb_hcd_probe\n");
	dev_info(&pdev->dev, "%s, driver " VUSB_DRIVER_VERSION "\n", VUSB_DRIVER_DESC);

	hcd = usb_create_hcd(&vusb_hcd_driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	/* Indicate the USB stack that both High and Full speed are supported */
	hcd->has_tt = 1;

	vhcd = hcd_to_vhcd(hcd);

	spin_lock_init(&vhcd->lock);
	vhcd->hcd_state = VUSB_HCD_INACTIVE;

	ret = usb_add_hcd(hcd, 0, 0);
	if (ret != 0)
		goto err_add;

	/* vHCD is up, now initialize this device for xenbus */
	ret = xenbus_register_frontend(&vusb_usbfront_driver);
	if (ret)
		goto err_xen;

	iprintk("xen_usbif initialized\n");

	dprintk(D_MISC, "<vusb_hcd_probe %d\n", ret);

	return 0;

err_xen:
	hcd->irq = -1;

	usb_remove_hcd(hcd);

err_add:
	usb_put_hcd(hcd);

	eprintk("%s failure - ret: %d\n", __FUNCTION__, ret);

	return ret;
}

/* Platform remove */
static int
vusb_platform_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct vusb_vhcd *vhcd = hcd_to_vhcd(hcd);

	/* Sanity check the state of the platform. Unloading this module
	 * should only be done for debugging and development purposes. */
	vusb_platform_sanity_disable(vhcd);

	/* Unregister from xenbus first */
	xenbus_unregister_driver(&vusb_usbfront_driver);

	/* A warning will result: "IRQ 0 already free". It seems the Linux
	 * kernel doesn't set hcd->irq to -1 when IRQ is not enabled for a USB
	 * driver. So we put an hack for this before usb_remove_hcd(). */
	hcd->irq = -1;

	usb_remove_hcd(hcd);

	usb_put_hcd(hcd);

	return 0;
}

#ifdef CONFIG_PM
static int
vusb_platform_freeze(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd;
	struct vusb_vhcd *vhcd;
	unsigned long flags;

	iprintk("HCD freeze\n");

	hcd = platform_get_drvdata(pdev);
	vhcd = hcd_to_vhcd(hcd);
	spin_lock_irqsave(&vhcd->lock, flags);

	dprintk(D_PM, "root hub state %u\n", vhcd->rh_state);

	if (vhcd->rh_state == VUSB_RH_RUNNING) {
		wprintk("Root hub isn't suspended!\n");
		vhcd->hcd_state = VUSB_HCD_INACTIVE;
		return -EBUSY;
	}

	clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	spin_unlock_irqrestore(&vhcd->lock, flags);

	return 0;
}

static int
vusb_platform_restore(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd;
	unsigned long flags;
	struct vusb_vhcd *vhcd;

	iprintk("HCD restore\n");

	hcd = platform_get_drvdata(pdev);
	vhcd = hcd_to_vhcd(hcd);

	spin_lock_irqsave(&vhcd->lock, flags);
	set_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	/* TODO used to be vusb_init_hcd which was wrong - what needs to happen here */
	spin_unlock_irqrestore(&vhcd->lock, flags);

	return 0;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM
static const struct dev_pm_ops vusb_platform_pm = {
	.freeze = vusb_platform_freeze,
	.restore = vusb_platform_restore,
	.thaw = vusb_platform_restore,
};
#endif /* CONFIG_PM */

static struct platform_driver vusb_platform_driver = {
	.probe = vusb_platform_probe,
	.remove = vusb_platform_remove,
	.driver = {
		.name = VUSB_PLATFORM_DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &vusb_platform_pm,
#endif /* CONFIG_PM */
	},
};

/****************************************************************************/
/* Module Init & Cleanup                                                    */

static bool module_ref_counted = false;

static ssize_t vusb_enable_unload(struct device_driver *drv, const char *buf,
				size_t count)
{
	/* In general we don't want this module to ever be unloaded since
	 * it is highly unsafe when there are active xenbus devices running
	 * in this module. This sysfs attribute allows this module to be
	 * unloaded for development and debugging work */
	if (module_ref_counted) {
		module_put(THIS_MODULE);
		module_ref_counted = false;
	} 

        return count;
}

static DRIVER_ATTR(enable_unload, S_IWUSR, NULL, vusb_enable_unload);

static void
vusb_cleanup(void)
{
	iprintk("clean up\n");
	if (vusb_platform_device) {
		driver_remove_file(&vusb_platform_driver.driver,
				&driver_attr_enable_unload);
		platform_device_unregister(vusb_platform_device);
		platform_driver_unregister(&vusb_platform_driver);
	}
}

static int __init
vusb_init(void)
{
	int ret;

	iprintk("OpenXT USB host controller\n");

	if (usb_disabled()) {
		wprintk("USB is disabled\n");
		return -ENODEV;
	}

	ret = platform_driver_register(&vusb_platform_driver);
	if (ret < 0) {
		eprintk("Unable to register the platform\n");
		goto fail_platform_driver;
	}

	vusb_platform_device =
		platform_device_alloc(VUSB_PLATFORM_DRIVER_NAME, -1);
	if (!vusb_platform_device) {
		eprintk("Unable to allocate platform device\n");
		ret = -ENOMEM;
		goto fail_platform_device1;
	}

	ret = platform_device_add(vusb_platform_device);
	if (ret < 0) {
		eprintk("Unable to add the platform\n");
		goto fail_platform_device2;
	}

	ret = driver_create_file(&vusb_platform_driver.driver,
				&driver_attr_enable_unload);
	if (ret < 0) {
		eprintk("Unable to add driver attr\n");
		goto fail_platform_device3;
	}

	if (!try_module_get(THIS_MODULE)) {
		eprintk("Failed to get module ref count\n");
		ret = -ENODEV;
		goto fail_driver_create_file;
	}
	module_ref_counted = true;

	return 0;

fail_driver_create_file:
	driver_remove_file(&vusb_platform_driver.driver,
			&driver_attr_enable_unload);
fail_platform_device3:
        platform_device_del(vusb_platform_device);
fail_platform_device2:
	platform_device_put(vusb_platform_device);
fail_platform_device1:
	platform_driver_unregister(&vusb_platform_driver);
fail_platform_driver:
	return ret;
}

module_init(vusb_init);
module_exit(vusb_cleanup);

MODULE_DESCRIPTION("Xen virtual USB frontend");
MODULE_LICENSE ("GPL");
