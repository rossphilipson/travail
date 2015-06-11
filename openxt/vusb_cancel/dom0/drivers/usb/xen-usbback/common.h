/* 
 * Copyright (c) Citrix Systems Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef __USBIF__BACKEND__COMMON_H__
#define __USBIF__BACKEND__COMMON_H__

#include <linux/version.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/pgalloc.h>
#include <xen/evtchn.h>
#include <asm/hypervisor.h>
#include <xen/vusb.h>
#include <xen/grant_table.h>
#include <xen/xenbus.h>


#define DPRINTK(_f, _a...)			\
	pr_debug("(file=%s, line=%d) " _f,	\
		 __FILE__ , __LINE__ , ## _a )

#undef VUSB_MANAGE_INTERFACES
#define USBBK_TIMEOUT (15 * HZ)
#define USBBCK_NRPACKS 1024
#define DUMP_URB_SZ 32
#undef DEBUG_CHECKS
#define USBBCK_VERSION 3
#define USBBCK_MAX_URB_SZ (10 * 1024 * 1024)

static inline int usbif_request_type(usbif_request_t *req)
{
	return (req->type);
}

static inline int usbif_request_dir_in(usbif_request_t *req)
{
	return ((req->endpoint & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN);
}

static inline int usbif_request_endpoint_num(usbif_request_t *req)
{
	return (req->endpoint & USB_ENDPOINT_NUMBER_MASK);
}

static inline int usbif_request_offset(usbif_request_t *req)
{
	return (req->offset);
}

static inline int usbif_request_shortok(usbif_request_t *req)
{
	return (req->flags & USBIF_F_SHORTOK);
}

static inline int usbif_request_reset(usbif_request_t *req)
{
	return ((req->flags & USBIF_F_RESET) || (req->type == USBIF_T_RESET));
}

static inline int usbif_request_cycle_port(usbif_request_t *req)
{
	return (req->flags & USBIF_F_CYCLE_PORT);
}

static inline int usbif_request_abort_pipe(usbif_request_t *req)
{
	return (req->type == USBIF_T_ABORT_PIPE);
}

static inline int usbif_request_get_frame(usbif_request_t *req)
{
	return (req->type == USBIF_T_GET_FRAME);
}

static inline int usbif_request_get_speed(usbif_request_t *req)
{
	return (req->type == USBIF_T_GET_SPEED);
}

static inline int usbif_request_cancel(usbif_request_t *req)
{
	return (req->type == USBIF_T_CANCEL);
}

static inline int usbif_request_type_valid(usbif_request_t *req)
{
	return (req->type <= USBIF_T_MAX);
}

static inline int usbif_request_asap(usbif_request_t *req)
{
	return (req->flags & USBIF_F_ASAP);
}

static inline int usbif_request_indirect(usbif_request_t *req)
{
	return (req->flags & USBIF_F_INDIRECT);
}

static inline int usbif_request_timeout(usbif_request_t *req)
{
	return ((usbif_request_type(req) == USBIF_T_CNTRL) ||
		!usbif_request_dir_in(req));
}

struct usbif_st;

struct vusb {
	/* what the domain refers to this vusb as */
	usbif_vdev_t           handle;
	unsigned               bus;
	unsigned               device;
	struct usb_device      *usbdev;
	struct usb_anchor      anchor;
	int                    initted;
	int                    active;
	int                    canceling_requests;
	/* maximum sgs supported by HCD this device is attached to */
	unsigned               max_sgs;
	int                    hcd_speed;
	/* device is allowed to suspend */
	int                    autosuspend : 1;
	/* copy unaligned transfers? */
	int                    copy_unaligned : 1;
	struct kref            kref;
};
#define KREF_TO_VUSB(d) container_of(d, struct vusb, kref)

struct backend_info;

typedef struct usbif_stats_st {
	int st_oo_req;
	int st_in_req;
	int st_out_req;

	int st_error;
	int st_reset;

	int st_in_bandwidth;
	int st_out_bandwidth;

	int st_cntrl_req;
	int st_isoc_req;
	int st_bulk_req;
	int st_int_req;
	int st_ind_req;
} usbif_stats_t;

typedef struct usbif_st {
	/* Unique identifier for this interface. */
	domid_t           domid;
	unsigned int      handle;
	/* Physical parameters of the comms window. */
	unsigned int      irq;
	/* Comms information. */
	enum usbif_protocol usb_protocol;
	usbif_back_rings_t usb_rings;
	void *usb_ring_addr;
	/* The VUSB attached to this interface. */
	struct vusb        vusb;
	/* Back pointer to the backend_info. */
	struct backend_info *be;
	/* Private fields. */
	spinlock_t       usb_ring_lock;
	atomic_t         refcnt;

	wait_queue_head_t   wq;
	struct task_struct  *xenusbd;
	unsigned int        waiting_reqs;

        /* statistics */
	unsigned long       st_print;
	usbif_stats_t       stats;

	wait_queue_head_t waiting_to_free;
	
} usbif_t;

static inline struct usbif_st *usbif_from_vusb(struct vusb *vusb)
{
	return container_of(vusb, struct usbif_st, vusb);
}

struct backend_info
{
	struct xenbus_device *dev;
	usbif_t *usbif;
	
	struct xenbus_watch backend_watch;
	struct xenbus_watch autosuspend_watch;
	unsigned bus;
	unsigned device;
};

typedef struct {
	struct list_head       free_list;
	struct page            *page;
	grant_handle_t         grant_handle;
} pending_segment_t;

/*
 * Each outstanding request that we've passed to the lower device layers has a 
 * 'pending_req' allocated to it. When the associated URB completes, the specified domain has a 
 * response queued for it, with the saved 'id' passed back.
 */
typedef struct {
	usbif_t                  *usbif;
	u64                      id;
    	int                      type;
	int                      direction_in;
	uint16_t                 offset;
	int                      nr_pages;
	int                      nr_packets;
	struct list_head         submitted_list;
	struct list_head         free_list;
	struct list_head         to_free_list;
	struct urb               *urb;
#ifdef USBBK_TIMEOUT
	struct timer_list        timer;
#endif
	pending_segment_t        *pending_segment[USBIF_MAX_SEGMENTS_PER_REQUEST];
#ifdef INDIRECT_SEGMENTS
	pending_segment_t        **pending_indirect_segment;
	int                      pending_indirect_segments;
	usbif_indirect_request_t *indirect_req[USBIF_MAX_SEGMENTS_PER_REQUEST];
#endif
} pending_req_t;

static inline int is_indirect(pending_req_t *req)
{
	return (req->pending_indirect_segments > 0);
}

static inline unsigned long vaddr_base(pending_req_t *req, int seg)
{
        unsigned long pfn = page_to_pfn(req->pending_segment[seg]->page);
        return (unsigned long)pfn_to_kaddr(pfn);
}

static inline unsigned long vaddr(pending_req_t *req, int seg)
{
#ifdef INDIRECT_SEGMENTS
	struct page *page = is_indirect(req) ?
		req->pending_indirect_segment[seg]->page :
		req->pending_segment[seg]->page;
        unsigned long pfn = page_to_pfn(page);
        return (unsigned long)pfn_to_kaddr(pfn);
#else
	return vaddr_base(req, seg);
#endif
}

static inline int data_pages(pending_req_t *req)
{
	return (is_indirect(req) ?
                req->pending_indirect_segments :
                req->nr_pages);
}


usbif_t *usbif_alloc(domid_t domid);
void usbif_kill_xenusbd(usbif_t *usbif);
void usbif_disconnect(usbif_t *usbif, struct xenbus_device *dev);
void usbif_free(usbif_t *usbif);
int usbif_map(usbif_t *usbif, unsigned long shared_page, unsigned int evtchn);

int get_usb_status(int status);

#define usbif_get(_b) (atomic_inc(&(_b)->refcnt))
#define usbif_put(_b)					\
	do {						\
		if (atomic_dec_and_test(&(_b)->refcnt))	\
			wake_up(&(_b)->waiting_to_free);\
	} while (0)

static inline int vusb_connected(struct vusb *vusb)
{
	return (vusb->usbdev != NULL);
}
int vusb_init(void);
int vusb_create(usbif_t *usbif, usbif_vdev_t vdevice, unsigned bus,
	       unsigned device);
void vusb_free(struct vusb *vusb);
int vusb_setup_urb(struct vusb *vusb, usbif_request_t *req, struct urb *urb);
static inline int vusb_canceling_requests(struct vusb *vusb)
{
	return (vusb->canceling_requests == 1);
}
int vusb_reset_device(struct vusb *vusb);
void vusb_flush(struct vusb *vusb);
int vusb_flush_endpoint(struct vusb *vusb, usbif_request_t *req);
int vusb_get_speed(struct vusb *vusb);
void vusb_free_coherent(struct vusb *vusb, struct urb *urb);
void *vusb_alloc_coherent(struct vusb *vusb, size_t size, dma_addr_t *dma);
void vusb_cycle_port(struct vusb *vusb);

/* vusb power management methods */
void vusb_pm_autosuspend_control(struct vusb *vusb, int enable);

void usbif_interface_init(void);

void usbif_xenbus_init(void);

irqreturn_t usbif_be_int(int irq, void *dev_id);
int usbif_schedule(void *arg);

int usbback_barrier(struct xenbus_transaction xbt,
		    struct backend_info *be, int state);
int usbback_suspend(usbif_t *usbif, int suspended);

unsigned int usbback_debug_lvl(void);

#define LOG_LVL_ERROR  0
#define LOG_LVL_INFO   1
#define LOG_LVL_DEBUG  2
#define LOG_LVL_DUMP   3

#define debug_print(l, _f, _a...) \
	do { if (usbback_debug_lvl() >= l) printk( _f, ## _a ); } while(0)

static inline unsigned int buffer_pages(unsigned int length)
{
	return ((length + PAGE_SIZE - 1) >> PAGE_SHIFT);
}

/* buffer handling routines */
int copy_out(pending_req_t *pending_req);
void copy_in(pending_req_t *pending_req);

void dump(uint8_t *buffer, int len);

#endif /* __USBIF__BACKEND__COMMON_H__ */
