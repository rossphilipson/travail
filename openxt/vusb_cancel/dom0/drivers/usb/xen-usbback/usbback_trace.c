/******************************************************************************
 * 
 * Back-end of the driver for virtual block devices. This portion of the
 * driver exports a 'unified' block-device interface that can be accessed
 * by any operating system that implements a compatible front end. A 
 * reference front-end implementation can be found in:
 *  arch/xen/drivers/blkif/frontend
 * 
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Copyright (c) 2005, Christopher Clark
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

/* derived from xen/blkback/blkback.c */

#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/list.h>
#include <linux/stacktrace.h>
#include <linux/scatterlist.h>
#include <xen/events.h>
#include <asm/hypervisor.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>

#include "common.h"

static int usbif_reqs = 64;
module_param_named(reqs, usbif_reqs, int, 0);
MODULE_PARM_DESC(reqs, "Number of usbback requests to allocate");

/* Run-time switchable: /sys/module/usbback/parameters/ */
static unsigned int log_stats = 0;
static unsigned int debug_lvl = 0;
module_param(log_stats, int, 0644);
module_param(debug_lvl, int, 0644);

unsigned int usbback_debug_lvl(void)
{
	return (debug_lvl);
}
static pending_req_t *pending_reqs;
static struct list_head pending_free;
static DEFINE_SPINLOCK(pending_free_lock);
static DECLARE_WAIT_QUEUE_HEAD(pending_free_wq);

#define USBBACK_INVALID_HANDLE (~0)

static struct page **pending_pages;
static pending_segment_t *pending_segments; 
static struct list_head pending_segments_free;
static int pending_segments_free_cnt;

static DEFINE_SPINLOCK(pending_to_free_lock);
static struct list_head pending_to_free;
static void async_free_reqs(unsigned long);
static DECLARE_TASKLET(async_free_reqs_task, async_free_reqs, 0);

static int do_usb_io_op(usbif_t *usbif);
static void dispatch_usb_io(usbif_t *usbif,
				 usbif_request_t *req,
				 pending_req_t *pending_req);
static void make_response(usbif_t *usbif, u64 id, int actual_length,
	int startframe, int status);

/******************************************************************
 * misc small helpers
 */
static int populate_req(pending_req_t *req)
{
	unsigned long flags;
	int index;

#ifdef INDIRECT_SEGMENTS
	req->pending_indirect_segments = 0;
#endif

	spin_lock_irqsave(&pending_free_lock, flags);
	if (req->nr_pages > pending_segments_free_cnt) {
		spin_unlock_irqrestore(&pending_free_lock, flags);
		debug_print(LOG_LVL_ERROR,
			"%s not enough segs (%d) need (%d)\n",
			__FUNCTION__, pending_segments_free_cnt,
			req->nr_pages);
		return -1;
	}
	
	for (index=0; index<req->nr_pages; index++) {
		pending_segment_t *segment =
			list_entry(pending_segments_free.next,
				pending_segment_t, free_list);
		BUG_ON(!segment);
		list_del(&segment->free_list);
		pending_segments_free_cnt--;

#ifdef DEBUG_CHECKS
		debug_print(LOG_LVL_DEBUG,
			"%s %p seg %d (%d) page %p\n",
			__FUNCTION__, req, index,
			pending_segments_free_cnt, segment->page);
#endif
		req->pending_segment[index] = segment;
	}
	spin_unlock_irqrestore(&pending_free_lock, flags);

	return 0;
}

#ifdef INDIRECT_SEGMENTS
static int populate_indirect(pending_req_t *req, int segs)
{
	unsigned long flags;
	int index;

	spin_lock_irqsave(&pending_free_lock, flags);
        if (segs > pending_segments_free_cnt) {
                spin_unlock_irqrestore(&pending_free_lock, flags);
		debug_print(LOG_LVL_ERROR,
			"%s not enough segs (%d) need (%d)\n",
			__FUNCTION__, pending_segments_free_cnt,
			req->nr_pages);
                return -1;
        }

        for (index=0; index<segs; index++) {
                pending_segment_t *segment =
                        list_entry(pending_segments_free.next, pending_segment_t, free_list);
		BUG_ON(!segment);
                list_del(&segment->free_list);
                pending_segments_free_cnt--;

#ifdef DEBUG_CHECKS
                debug_print(LOG_LVL_DEBUG,
                        "%s req %p seg %d (%d) page %p\n",
                        __FUNCTION__, req, index, pending_segments_free_cnt,
                        segment->page);
#endif
                req->pending_indirect_segment[index] = segment;
        }

	req->pending_indirect_segments = segs;

        spin_unlock_irqrestore(&pending_free_lock, flags);

        return 0;
}
#endif

static pending_req_t* alloc_req(void)
{
	pending_req_t *req = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pending_free_lock, flags);
	if (!list_empty(&pending_free)) {
		req = list_entry(pending_free.next, pending_req_t, free_list);
		list_del(&req->free_list);
	}
	spin_unlock_irqrestore(&pending_free_lock, flags);

	debug_print(LOG_LVL_DEBUG, "%s req %p\n", __FUNCTION__, req);

	return req;
}

#ifdef INDIRECT_SEGMENTS
static void free_indirect_segments(pending_req_t *req)
{
        int index;

	assert_spin_locked(&pending_free_lock);
      
	for (index=0; index<req->pending_indirect_segments; index++) {
                pending_segment_t *segment = req->pending_indirect_segment[index];

		BUG_ON(!segment);
                list_add(&segment->free_list, &pending_segments_free);
                pending_segments_free_cnt++;

#ifdef DEBUG_CHECKS
                debug_print(LOG_LVL_DEBUG, "%s req %p seg %d (%d) page %p\n",
                            __FUNCTION__, req, index, pending_segments_free_cnt,
                            segment->page);
#endif
        }

	kfree(req->pending_indirect_segment);
	req->pending_indirect_segment = NULL;
	req->pending_indirect_segments = 0;
}
#endif

static void free_segments(pending_req_t *req)
{
        int index;

	assert_spin_locked(&pending_free_lock);

#ifdef INDIRECT_SEGMENTS
	if (is_indirect(req))
		free_indirect_segments(req);
#endif

        for (index=0; index<req->nr_pages; index++) {
		pending_segment_t *segment = req->pending_segment[index];

		BUG_ON(!segment);
		req->pending_segment[index] = NULL;

		list_add(&segment->free_list, &pending_segments_free);
		pending_segments_free_cnt++;

#ifdef DEBUG_CHECKS
		debug_print(LOG_LVL_DEBUG, "%s req %p seg %d (%d) page %p\n",
			__FUNCTION__, req, index, pending_segments_free_cnt,
			segment->page);
#endif
        }

	req->nr_pages = 0;
}

static void free_req(pending_req_t *req)
{
	unsigned long flags;
	int was_empty;
	struct urb *urb = req->urb;

	req->urb = NULL;
	if (urb) {
		if (urb->transfer_buffer_length) {
			struct vusb *vusb = &req->usbif->vusb;
                	vusb_free_coherent(vusb, urb);
		}
		if (urb->setup_packet) {
			kfree(urb->setup_packet);
			urb->setup_packet = NULL;
		}
		if (urb->sg) {
			kfree(urb->sg);
			urb->sg = NULL;
		}
	}
	req->usbif = NULL;

	debug_print(LOG_LVL_DEBUG, "%s %p\n", __FUNCTION__, req);

	spin_lock_irqsave(&pending_free_lock, flags);
	free_segments(req);
	was_empty = list_empty(&pending_free);
	list_add(&req->free_list, &pending_free);
	spin_unlock_irqrestore(&pending_free_lock, flags);
	if (was_empty)
		wake_up(&pending_free_wq);
}

static void async_free_reqs(unsigned long data)
{
	struct list_head tmp;
	pending_req_t *req;
	struct urb *urb;
	unsigned long flags;

	INIT_LIST_HEAD(&tmp);

	/* Copy to temp list */
	spin_lock_irqsave(&pending_to_free_lock, flags);
	list_splice_init(&pending_to_free, &tmp);
	spin_unlock_irqrestore(&pending_to_free_lock, flags);

	/* Run actual free outside of interrupt context */
	while (!list_empty(&tmp)) {
		req = list_entry(tmp.next, pending_req_t, to_free_list);
		list_del(&req->to_free_list);

		/* Stash the urb and call the real free_req routine */
		urb = req->urb;
		free_req(req);

		/*
		 * The urb had its ref count bumped to keep it alive before being queued for
		 * cleanup in this bottom half routine. Dropping that ref here will likely
		 * cleanup and release the urb.
	 	 */
		if (urb)
			usb_put_urb(urb);
	}
}

#ifdef INDIRECT_SEGMENTS
static void fast_flush_area_indirect(pending_req_t *req)
{
        struct gnttab_unmap_grant_ref *unmap;
        unsigned int i, invcount = 0;
        grant_handle_t *handle;
	pending_segment_t *indirect_seg;
        int ret;

	debug_print(LOG_LVL_DEBUG, "%s Flushing %d indirect segs!\n",
                                __FUNCTION__, req->pending_indirect_segments);

	unmap = kmalloc(sizeof(struct gnttab_unmap_grant_ref) *
			req->pending_indirect_segments, GFP_ATOMIC);
	if (!unmap) {
		debug_print(LOG_LVL_ERROR, "%s kmalloc failed for 0x%x bytes!\n",
				__FUNCTION__, sizeof(struct gnttab_unmap_grant_ref) *
                        req->pending_indirect_segments);
		return;
	}

       	for (i = 0; i < req->pending_indirect_segments; i++) {
		indirect_seg = req->pending_indirect_segment[i];
		BUG_ON(!indirect_seg);

		handle = &indirect_seg->grant_handle;
               	if (*handle == USBBACK_INVALID_HANDLE)
                       	continue;
               	gnttab_set_unmap_op(&unmap[i], vaddr(req, i),
			GNTMAP_host_map, *handle);
               	*handle = USBBACK_INVALID_HANDLE;
               	invcount++;
       	}
        ret = HYPERVISOR_grant_table_op(
       	        GNTTABOP_unmap_grant_ref, unmap, invcount);
      	BUG_ON(ret);

	kfree(unmap);
}
#endif

static void fast_flush_area(pending_req_t *req)
{
	struct gnttab_unmap_grant_ref unmap[USBIF_MAX_SEGMENTS_PER_REQUEST];
	unsigned int i, invcount = 0;
	grant_handle_t *handle;
	int ret;

#ifdef INDIRECT_SEGMENTS
	if (is_indirect(req))
		fast_flush_area_indirect(req);
#endif

	debug_print(LOG_LVL_DEBUG, "%s Flushing %d segs!\n",
                                __FUNCTION__, req->nr_pages);

	for (i = 0; i < req->nr_pages; i++) {
		handle = &req->pending_segment[i]->grant_handle;
		if (*handle == USBBACK_INVALID_HANDLE)
			continue;
		gnttab_set_unmap_op(&unmap[i], vaddr_base(req, i), GNTMAP_host_map,
				    *handle);
		*handle = USBBACK_INVALID_HANDLE;
		invcount++;
	}

	ret = HYPERVISOR_grant_table_op(
		GNTTABOP_unmap_grant_ref, unmap, invcount);
	BUG_ON(ret);
}

/*
 * This is our special version of usb_kill_anchored_urbs. Our routine
 * is a bit like that one except it is used to snipe a single URB.
 */
static void cancel_urb(struct usb_anchor *anchor, u64 cancel_id)
{
	struct urb *victim;
	bool found = false;

	printk("***RJP*** %s v3 looking for shit to kill!\n", __FUNCTION__);

	spin_lock_irq(&anchor->lock);
	list_for_each_entry(victim, &anchor->urb_list, anchor_list) {
		if (((pending_req_t*)victim->context)->id == cancel_id) {
			printk("***RJP*** %s v3 found shit to kill!\n", __FUNCTION__);
			usb_get_urb(victim);
			found = true;
			break;
		}
	}
	spin_unlock_irq(&anchor->lock);

	if (!found)
		return;

	/*
	 * Now there is an extra ref of the URB. After killing it, drop the ref
         * count. The docs say the URB cannot be deleted within the kill call.
         * The ref count will prevent the async cleanup part of the completion
         * routines from doing this.
         */
	usb_kill_urb(victim);
	usb_put_urb(victim);

	printk("***RJP*** %s v3 killed shit!\n", __FUNCTION__);
}

/******************************************************************
 * SCHEDULER FUNCTIONS
 */

static void print_stats(usbif_t *usbif)
{
	usbif_stats_t *stats = &usbif->stats;

	printk("%s: oo %3d  |  in %4d (%6d)  |  out %4d (%6d) | cntrl %4d | "
		"isoc %4d | bulk %4d | int %4d | ind %4d | err %4d | rst %4d\n",
		current->comm, stats->st_oo_req, stats->st_in_req,
		stats->st_in_bandwidth, stats->st_out_req,
		stats->st_out_bandwidth, stats->st_cntrl_req,
		stats->st_isoc_req, stats->st_bulk_req, stats->st_int_req,
		stats->st_ind_req, stats->st_error, stats->st_reset);
	usbif->st_print = jiffies + msecs_to_jiffies(10 * 1000);
	memset(&usbif->stats, 0, sizeof(usbif_stats_t));
}

int usbif_schedule(void *arg)
{
	usbif_t *usbif = arg;

	usbif_get(usbif);

	debug_print(LOG_LVL_INFO, "%s: started\n", current->comm);

	while (!kthread_should_stop()) {
		if (try_to_freeze())
			continue;

		wait_event_interruptible(
			usbif->wq,
			usbif->waiting_reqs || kthread_should_stop());
		wait_event_interruptible(
			pending_free_wq,
			!list_empty(&pending_free) || kthread_should_stop());

		if (!kthread_should_stop()) {
			usbif->waiting_reqs = 0;
			smp_mb(); /* clear flag *before* checking for work */

			if (do_usb_io_op(usbif))
				usbif->waiting_reqs = 1;
		
			if (log_stats && time_after(jiffies, usbif->st_print))
				print_stats(usbif);
		}
	}

	/* cancel any outstanding URBs */
	vusb_flush(&usbif->vusb);

	if (log_stats)
		print_stats(usbif);

	debug_print(LOG_LVL_INFO, "%s: exiting\n", current->comm);

	usbif_put(usbif);

	return 0;
}

static char *get_usb_statmsg(int status)
{
	static char unkmsg[28];

	switch (status) {
	case 0:
		return "success";
	case -ENOENT:
		return "unlinked (sync)";
	case -EINPROGRESS:
		return "pending";
	case -EPROTO:
		return "bit stuffing error, timeout, or unknown USB error";
	case -EILSEQ:
		return "CRC mismatch, timeout, or unknown USB error";
	case -ETIME:
		return "timed out";
	case -EPIPE:
		return "endpoint stalled";
	case -ECOMM:
		return "IN buffer overrun";
	case -ENOSR:
		return "OUT buffer underrun";
	case -EOVERFLOW:
		return "too much data";
	case -EREMOTEIO:
		return "short packet detected";
	case -ENODEV:
	case -EHOSTUNREACH:
		return "device removed";
	case -EXDEV:
		return "partial isochronous transfer";
	case -EINVAL:
		return "invalid argument";
	case -ECONNRESET:
		return "unlinked (async)";
	case -ESHUTDOWN:
		return "device shut down";
	default:
		snprintf(unkmsg, sizeof(unkmsg), "unknown status %d", status);
		return unkmsg;
	}
}

int get_usb_status(int status)
{
	switch (status) {
	case 0:
		/* success */
		return USBIF_RSP_OKAY;
	case -ENOENT:
		/* unlinked (sync) */
		return USBIF_RSP_USB_CANCELED;
	case -EINPROGRESS:
		/* pending */
		return USBIF_RSP_USB_PENDING;
	case -EPROTO:
		/* bit stuffing error, timeout, or unknown USB error */
		return USBIF_RSP_USB_PROTO;
	case -EILSEQ:
		/* CRC mismatch, timeout, or unknown USB error */
		return USBIF_RSP_USB_CRC;
	case -ETIME:
		/* timed out */
		return USBIF_RSP_USB_TIMEOUT;
	case -EPIPE:
		/* endpoint stall */
		return USBIF_RSP_USB_STALLED;
	case -ECOMM:
		/* IN buffer overrun */
		return USBIF_RSP_USB_INBUFF;
	case -ENOSR:
		/* OUT buffer underrun */
		return USBIF_RSP_USB_OUTBUFF;
	case -EOVERFLOW:
		/* too much data */
		return USBIF_RSP_USB_OVERFLOW;
	case -EREMOTEIO:
		/* short packet detected */
		return USBIF_RSP_USB_SHORTPKT;
	case -ENODEV:
		/* device removed */
		return USBIF_RSP_USB_DEVRMVD;
	case -EXDEV:
		/* partial isochronous transfer */
		return USBIF_RSP_USB_PARTIAL;
	case -EMSGSIZE:
	case -EINVAL:
		/* invalid argument */
		return USBIF_RSP_USB_INVALID;
	case -ECONNRESET:
		/* unlinked (async) */
		return USBIF_RSP_USB_RESET;
	case -ESHUTDOWN:
		/* device shut down */
		return USBIF_RSP_USB_SHUTDOWN;
	default:
		return USBIF_RSP_USB_UNKNOWN;
	}
}

/*
 * Handle timeouts
 */
#ifdef USBBK_TIMEOUT
static void timeout_usb_io_op(unsigned long data)
{
	struct urb *urb = (struct urb *)data;

	debug_print(LOG_LVL_DEBUG, "%s: urb %p\n", __FUNCTION__, urb);

	usb_unlink_urb(urb);
}

static void set_timeout(pending_req_t *pending_req)
{
	init_timer(&pending_req->timer);
	pending_req->timer.function = timeout_usb_io_op;
	pending_req->timer.data = (unsigned long) pending_req->urb;
	pending_req->timer.expires = jiffies + USBBK_TIMEOUT;
	add_timer(&pending_req->timer);
}

static void cancel_timeout(pending_req_t *pending_req)
{
	if (timer_pending(&pending_req->timer))
		del_timer(&pending_req->timer);
}
#endif

/*
 * COMPLETION CALLBACK 
 */
static void end_usb_io_op(struct urb *urb)
{
	pending_req_t *pending_req = (pending_req_t *)urb->context;
	usbif_t *usbif = pending_req->usbif;
	int status = vusb_canceling_requests(&usbif->vusb) ?
		-ECONNRESET : urb->status;
	unsigned long flags;

	debug_print(LOG_LVL_INFO, "end id %llu len %d status %d %s\n",
		pending_req->id, urb->actual_length, status,
		get_usb_statmsg(status));

#ifdef USBBK_TIMEOUT
	cancel_timeout(pending_req);
#endif

	/*
	 * Don't need to unanchor, usb_hcd_giveback_urb has already done it
	 * before calling this completion routine.
	 */
	if ((urb->status != -ENODEV) &&		/* device removed */
		(urb->status != -ESHUTDOWN) &&	/* device disabled */
		(urb->status != -EPROTO)) { /* timeout or unknown USB error */
		copy_in(pending_req);

		if (pending_req->direction_in)
			usbif->stats.st_in_bandwidth +=
				urb->transfer_buffer_length;
		else
			usbif->stats.st_out_bandwidth +=
				urb->transfer_buffer_length;
	}

	fast_flush_area(pending_req);
	make_response(usbif, pending_req->id, urb->actual_length,
		urb->start_frame, get_usb_status(status));
	usbif_put(pending_req->usbif);

	/*
	 * Schedule async free as it causes an oops on 32bit kernel doing dma frees in
	 * this completion handler with irqs disabled (the WARN_ON(irqs_disabled())
	 * in dma_free_attrs).  We have to bump the ref count on the urb since it will
	 * be released after this completion routine returns. See the code in
	 * hcd.c:usb_hcd_giveback_urb() that call the completion callback.
	 */
	urb = usb_get_urb(urb);
	spin_lock_irqsave(&pending_to_free_lock, flags);
	list_add_tail(&pending_req->to_free_list, &pending_to_free);
	spin_unlock_irqrestore(&pending_to_free_lock, flags);
	tasklet_schedule(&async_free_reqs_task);
}

/******************************************************************************
 * NOTIFICATION FROM GUEST OS.
 */

static void usbif_notify_work(usbif_t *usbif)
{
	usbif->waiting_reqs = 1;
	wake_up(&usbif->wq);
}

irqreturn_t usbif_be_int(int irq, void *dev_id)
{
	usbif_notify_work(dev_id);
	return IRQ_HANDLED;
}


/******************************************************************
 * DOWNWARD CALLS -- These interface with the usb-device layer proper.
 */

static int do_usb_io_op(usbif_t *usbif)
{
	usbif_back_rings_t *usb_rings = &usbif->usb_rings;
	usbif_request_t req;
	pending_req_t *pending_req;
	RING_IDX rc, rp;
	int more_to_do = 0;

	rc = usb_rings->common.req_cons;
	rp = usb_rings->common.sring->req_prod;
	rmb(); /* Ensure we see queued requests up to 'rp'. */

	while ((rc != rp) && !kthread_should_stop()) {

		if (RING_REQUEST_CONS_OVERFLOW(&usb_rings->common, rc))
			break;

		pending_req = alloc_req();
		if (NULL == pending_req) {
			usbif->stats.st_oo_req++;
			more_to_do = 1;
			break;
		}

		switch (usbif->usb_protocol) {
		case USBIF_PROTOCOL_NATIVE:
			memcpy(&req, RING_GET_REQUEST(&usb_rings->native, rc), sizeof(req));
			break;
		case USBIF_PROTOCOL_X86_32:
			usbif_get_x86_32_req(&req, RING_GET_REQUEST(&usb_rings->x86_32, rc));
			break;
		case USBIF_PROTOCOL_X86_64:
			usbif_get_x86_64_req(&req, RING_GET_REQUEST(&usb_rings->x86_64, rc));
			break;
		default:
			BUG();
		}
		usb_rings->common.req_cons = ++rc; /* before make_response() */

		if (!usbif_request_type_valid(&req)) {
			debug_print(LOG_LVL_ERROR, "%s: type %d not valid\n",
				__FUNCTION__, usbif_request_type(&req));
			make_response(usbif, req.id, 0, 0, USBIF_RSP_ERROR);
			free_req(pending_req);
		} else if (usbif_request_reset(&req)) {
			int ret = vusb_reset_device(&usbif->vusb)
					? USBIF_RSP_ERROR : USBIF_RSP_OKAY;

			make_response(usbif, req.id, 0, 0, ret);
			free_req(pending_req);
		} else if (usbif_request_cycle_port(&req)) {
			vusb_cycle_port(&usbif->vusb);

			make_response(usbif, req.id, 0, 0, USBIF_RSP_OKAY);
			free_req(pending_req);
		} else if (usbif_request_abort_pipe(&req)) {
			int ret = vusb_flush_endpoint(&usbif->vusb, &req)
					? USBIF_RSP_ERROR : USBIF_RSP_OKAY;

			make_response(usbif, req.id, 0, 0, ret);
			free_req(pending_req);
		} else if (usbif_request_get_frame(&req)) {
			int frame = usb_get_current_frame_number(usbif->vusb.usbdev);

			if (frame >= 0)
				make_response(usbif, req.id, 0, frame, 0);
			else
				make_response(usbif, req.id, 0, 0, USBIF_RSP_ERROR);
			free_req(pending_req);
		} else if (usbif_request_get_speed(&req)) {
			make_response(usbif, req.id, 0,
				vusb_get_speed(&usbif->vusb), 0);
			free_req(pending_req);
		} else if (usbif_request_cancel(&req)) {
			cancel_urb(&usbif->vusb.anchor, *((u64*)(&req.u.data[0])));

			make_response(usbif, req.id, 0, 0, USBIF_RSP_OKAY);
			free_req(pending_req);
		} else
			dispatch_usb_io(usbif, &req, pending_req);
	}

	return more_to_do;
}

static struct urb * setup_urb(pending_req_t *pending_req, int length, int* err)
{
	struct vusb *vusb = &pending_req->usbif->vusb;
	struct urb *urb = usb_alloc_urb(pending_req->nr_packets, GFP_KERNEL);
	*err=0;

	pending_req->urb = urb;
	if (urb == NULL) {
		*err=1;
		return (NULL);
	}

	/* struct urb is pre zeroed, only init to non zero values */
	urb->context	            = pending_req;
	urb->complete	            = end_usb_io_op;
	urb->number_of_packets      = pending_req->nr_packets;

	if (length > 0) {
		int pages = buffer_pages(pending_req->offset + length);

		/*
		 * 1. Linux currently only supports scatter gather for bulk
		 *    transfers.
		 * 2. Some controllers can't handle unaligned multipage
		 *    DMA transfers.
		 */
		if ((vusb->max_sgs > 0) && (pages <= vusb->max_sgs) &&
			((pages == 1) || (!vusb->copy_unaligned || !pending_req->offset)) &&
			(pending_req->type == USBIF_T_BULK)) {
			urb->sg = kzalloc(pages * sizeof(struct scatterlist),
				GFP_KERNEL);
			if (urb->sg == NULL) {
				*err=2;
				return (NULL);
			}
		} else {
			urb->transfer_buffer = vusb_alloc_coherent(vusb,
				length, &urb->transfer_dma);
			if (urb->transfer_buffer == NULL) {
				*err=3;
				return (NULL);
			}

			urb->transfer_flags = URB_NO_TRANSFER_DMA_MAP;
		}
		urb->transfer_buffer_length = length;
	}

	if (unlikely(pending_req->type == USBIF_T_CNTRL)) {
		urb->setup_packet =
			kmalloc(sizeof(struct usb_ctrlrequest), GFP_KERNEL);
		if (urb->setup_packet == NULL) {
			*err=4;
			return (NULL);
		}
	}

	return (urb);
}

static int map_request(pending_req_t *pending_req, int offset, domid_t domid,
			grant_ref_t *gref, unsigned int nseg, int readonly,
			int indirect)
{
	struct gnttab_map_grant_ref *map;
	int i, ret;

	BUG_ON(nseg > USBIF_MAX_SEGMENTS_PER_IREQUEST);

	map = kmalloc(sizeof(struct gnttab_map_grant_ref) * nseg, GFP_KERNEL);
	if (!map) {
		debug_print(LOG_LVL_ERROR, "%s: req %p offset %d nseg %d indirect %d\n",
			__FUNCTION__, pending_req, offset, nseg, indirect);
		return (-1);
	}

	for (i = 0; i < nseg; i++) {
		uint32_t flags;
#ifdef INDIRECT_SEGMENTS
		int page_nr = i + offset;
		unsigned long virtual_address = indirect ? vaddr(pending_req, page_nr)
				: vaddr_base(pending_req, page_nr);
#else
		unsigned long virtual_address = vaddr_base(pending_req, i);
#endif

		if (readonly)
			flags = (GNTMAP_host_map|GNTMAP_readonly);
		else
			flags = GNTMAP_host_map;
		gnttab_set_map_op(&map[i], virtual_address, flags, gref[i], domid);
#ifdef DEBUG_CHECKS
		debug_print(LOG_LVL_DEBUG, "%s: %d of %d gref %x flags %x vaddr %lx\n",
			__FUNCTION__, i, nseg, gref[i], flags, virtual_address);
#endif
	}

	ret = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref, map, nseg);
	BUG_ON(ret);

	for (i = 0; i < nseg; i++) {
#ifdef INDIRECT_SEGMENTS
		int page_nr = i + offset;
		unsigned long virtual_address = indirect ? vaddr(pending_req, page_nr)
				: vaddr_base(pending_req, page_nr);
#else
		unsigned long virtual_address = vaddr_base(pending_req, i);
#endif

		if (unlikely(map[i].status != 0)) {
			debug_print(LOG_LVL_ERROR,
				"invalid buffer -- could not remap it\n");
			map[i].handle = USBBACK_INVALID_HANDLE;
			ret |= 1;
		}

#ifdef INDIRECT_SEGMENTS
		if (indirect)
			pending_req->pending_indirect_segment[page_nr]->grant_handle = map[i].handle;
		else
#endif
			pending_req->pending_segment[i]->grant_handle = map[i].handle;

		if (ret)
			continue;

		set_phys_to_machine(__pa(virtual_address) >> PAGE_SHIFT,
				FOREIGN_FRAME(map[i].dev_bus_addr >> PAGE_SHIFT));
	}

	kfree(map);

	return (ret);
}

#ifdef INDIRECT_SEGMENTS
static int setup_indirect(usbif_t *usbif, pending_req_t *pending_req, int segs, int in)
{
	unsigned int indirect_req, mapped_segs = 0;

	debug_print(LOG_LVL_DEBUG, "%s req %p segs %d\n", __FUNCTION__, pending_req, segs);

	usbif->stats.st_ind_req++;

	pending_req->pending_indirect_segment = kmalloc(sizeof(pending_segment_t *) * segs, GFP_KERNEL);
	if (!pending_req->pending_indirect_segment) {
		debug_print(LOG_LVL_ERROR, "kmalloc indirect segments failed!\n");
		return (-1);
	}

	if (populate_indirect(pending_req, segs)) {
		kfree(pending_req->pending_indirect_segment);
		pending_req->pending_indirect_segment = NULL;
		debug_print(LOG_LVL_ERROR, "populate indirect failed!\n");
                return (-1);
	}

	for (indirect_req=0; indirect_req<pending_req->nr_pages; indirect_req++) {
		usbif_indirect_request_t *indirect =
			(usbif_indirect_request_t *)vaddr_base(pending_req, indirect_req);

		debug_print(LOG_LVL_DEBUG, "%s req %p indirect %d : %p segs %d\n",
			__FUNCTION__, pending_req, indirect_req, indirect,
			indirect->nr_segments);
#if (DUMP_URB_SZ > 0)
	        if (usbback_debug_lvl() >= LOG_LVL_DUMP)
        	        dump((uint8_t *)indirect, PAGE_SIZE);
#endif

		pending_req->indirect_req[indirect_req] = indirect;

		if ((indirect->nr_segments == 0) ||
			(indirect->nr_segments > USBIF_MAX_SEGMENTS_PER_IREQUEST)) {
			debug_print(LOG_LVL_ERROR, "req bad indirect segs!\n");
			return (-1);
		}

		if (map_request(pending_req, mapped_segs,
				usbif->domid, indirect->gref,
                       		indirect->nr_segments, in, 1)) {
			debug_print(LOG_LVL_ERROR, "indirect map failed!\n");
                	return (-1);
		}

		mapped_segs += indirect->nr_segments;
	}

	BUG_ON(mapped_segs != segs);

	return (0);
}
#endif

static void dispatch_usb_io(usbif_t *usbif, usbif_request_t *req,
				 pending_req_t *pending_req)
{
	struct urb *urb;
	int ret = -EINVAL;
	int type = usbif_request_type(req);
	int indirect = usbif_request_indirect(req);
	int err;

	debug_print(LOG_LVL_INFO, "start %d id %llu %s type %d end %d"
			" len %d off %d segs %d flags %x pr %p\n",
			usbif->vusb.handle, req->id,
			usbif_request_dir_in(req) ? "IN" : "OUT",
			type, usbif_request_endpoint_num(req), req->length,
			usbif_request_offset(req), req->nr_segments,
			(int)req->flags, pending_req);

	pending_req->usbif        = usbif;
	pending_req->id           = req->id;
	pending_req->type         = req->type;
	pending_req->direction_in = usbif_request_dir_in(req);
	pending_req->offset       = usbif_request_offset(req);
	pending_req->nr_pages     = req->nr_segments;
	pending_req->nr_packets   = req->nr_packets;
	pending_req->urb          = NULL;

	if (unlikely(req->length > USBBCK_MAX_URB_SZ)) {
		debug_print(LOG_LVL_ERROR,
			"Bad req size %d (%d)\n",
			req->length, USBBCK_MAX_URB_SZ);
		goto fail_response;
	}

	if (unlikely(pending_req->nr_packets > USBBCK_NRPACKS)) {
		debug_print(LOG_LVL_ERROR,
			"Bad number of packets in request (%d : %d)\n",
			req->nr_packets, USBBCK_NRPACKS);
		goto fail_response;
	}

	if (populate_req(pending_req) < 0) {
		debug_print(LOG_LVL_ERROR, "Failed populate req\n");
		goto fail_response;
        }

	urb = setup_urb(pending_req, req->length, &err);
	if (unlikely(urb == NULL)) {
		if (printk_ratelimit())
			debug_print(LOG_LVL_ERROR, "Failed urb alloc, reason = %d\n", err);
		goto fail_response;
	}

	if (req->length > 0) {
		/* Check that number of segments is sane. */
		int pages = buffer_pages(pending_req->offset + req->length);

		/* ISO requests have one addition page for the desciptors */
		if (type == USBIF_T_ISOC)
			pages++;
		if (unlikely(req->nr_segments == 0) || 
			unlikely(req->nr_segments
				> USBIF_MAX_SEGMENTS_PER_REQUEST) ||
			(unlikely(pages != req->nr_segments) && !indirect)) {
			debug_print(LOG_LVL_ERROR,
				"Bad number of segments in request (%d : %d)\n",
				req->nr_segments, pages);
			goto fail_response;
		}

		if ((err = map_request(pending_req, 0, usbif->domid, req->u.gref,
				       req->nr_segments,
				       !usbif_request_dir_in(req) || indirect,
				       0))) {
			debug_print(LOG_LVL_ERROR,
				    "map_request failed, err=%d\n",err);
			goto fail_flush;
		}

		if (indirect) {
#ifdef INDIRECT_SEGMENTS
			if ((err = setup_indirect(usbif, pending_req, pages,
						  !usbif_request_dir_in(req))) < 0) {
				debug_print(LOG_LVL_ERROR,
					    "setup_indirect failed, err=%d\n",err);
				goto fail_flush;
			}
#else
			debug_print(LOG_LVL_ERROR,
				    "indirect specified but not compiled in\n");
			goto fail_flush;
#endif
		}

		ret = copy_out(pending_req);
		if (unlikely(ret < 0)) {
			debug_print(LOG_LVL_ERROR,
				"copy iso failed urb %p, ret %d\n", urb, ret);
			goto fail_flush;
		}
	}

	ret = vusb_setup_urb(&usbif->vusb, req, urb);
	if (unlikely(ret < 0)) {
		debug_print(LOG_LVL_ERROR,
			"setup failed for urb %p, ret %d\n", urb, ret);
		goto fail_flush;
	} else if (unlikely(ret > 0)) {
		debug_print(LOG_LVL_INFO,
			"control success for urb %p\n", urb);
		ret = 0;
		goto early_success;
	}

	usbif_get(usbif);

#ifdef USBBK_TIMEOUT
	if (usbif_request_timeout(req)) {
		debug_print(LOG_LVL_DEBUG,
			"Timeout set for urb for %llu\n", req->id);
		set_timeout(pending_req);
	}
#endif

	usb_anchor_urb(urb, &usbif->vusb.anchor);

	ret = usb_submit_urb(urb, GFP_KERNEL);
	if (unlikely(ret < 0)) {
		debug_print(LOG_LVL_ERROR,
			"submit failed for urb %p, ret %d\n", urb, ret);
		usb_unanchor_urb(urb);
#ifdef USBBK_TIMEOUT
		cancel_timeout(pending_req);
#endif
		usbif_put(usbif);
		goto fail_flush;
	}

	/* release our urb reference from the alloc, the core now owns it */
	usb_free_urb(urb);

	debug_print(LOG_LVL_INFO, "%s: Submitted urb for %llu\n",
			__FUNCTION__, req->id);

	return;

 early_success:
 fail_flush:
	fast_flush_area(pending_req);
 fail_response:
	make_response(usbif, req->id, 0, 0, get_usb_status(ret));
	urb = pending_req->urb;
	free_req(pending_req);
	usb_free_urb(urb);
} 



/******************************************************************
 * MISCELLANEOUS SETUP / TEARDOWN / DEBUGGING
 */


static void make_response(usbif_t *usbif, u64 id, int actual_length,
	int data, int status)
{
	usbif_response_t  resp;
	unsigned long     flags;
	usbif_back_rings_t *usb_rings = &usbif->usb_rings;
	int more_to_do = 0;
	int notify;

	debug_print(LOG_LVL_INFO,
		"%s: id %llu len %d data %d status %d\n",
		__FUNCTION__, id, actual_length, data, status);

	if (status)
		usbif->stats.st_error++;

	resp.id            = id;
	resp.actual_length = actual_length;
	resp.data          = data;
	resp.status        = status;

	spin_lock_irqsave(&usbif->usb_ring_lock, flags);
	/* Place on the response ring for the relevant domain. */
	switch (usbif->usb_protocol) {
	case USBIF_PROTOCOL_NATIVE:
		memcpy(RING_GET_RESPONSE(&usb_rings->native, usb_rings->native.rsp_prod_pvt),
		       &resp, sizeof(resp));
		break;
	case USBIF_PROTOCOL_X86_32:
		memcpy(RING_GET_RESPONSE(&usb_rings->x86_32, usb_rings->x86_32.rsp_prod_pvt),
		       &resp, sizeof(resp));
		break;
	case USBIF_PROTOCOL_X86_64:
		memcpy(RING_GET_RESPONSE(&usb_rings->x86_64, usb_rings->x86_64.rsp_prod_pvt),
		       &resp, sizeof(resp));
		break;
	default:
		BUG();
	}
	usb_rings->common.rsp_prod_pvt++;
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&usb_rings->common, notify);
	if (usb_rings->common.rsp_prod_pvt == usb_rings->common.req_cons) {
		/*
		 * Tail check for pending requests. Allows frontend to avoid
		 * notifications if requests are already in flight (lower
		 * overheads and promotes batching).
		 */
		RING_FINAL_CHECK_FOR_REQUESTS(&usb_rings->common, more_to_do);

	} else if (RING_HAS_UNCONSUMED_REQUESTS(&usb_rings->common)) {
		more_to_do = 1;
	}

	spin_unlock_irqrestore(&usbif->usb_ring_lock, flags);

	if (more_to_do)
		usbif_notify_work(usbif);

	/* always notify, there seems to be a bug in the Xen ring code */
//	if (notify)
		notify_remote_via_irq(usbif->irq);
}

static int __init usbif_init(void)
{
	int i, mmap_pages;

	if (!xen_pv_domain())
		return -ENODEV;

	if (vusb_init())
		return -EINVAL;

	mmap_pages = usbif_reqs * USBIF_MAX_SEGMENTS_PER_REQUEST;

	pending_reqs          = kzalloc(sizeof(pending_reqs[0]) *
					usbif_reqs, GFP_KERNEL);
	pending_pages         = vzalloc(sizeof(pending_pages[0]) * mmap_pages);

	pending_segments      = kzalloc(sizeof(pending_segments[0]) *
					mmap_pages, GFP_KERNEL);

	if (!pending_reqs || !pending_pages || !pending_segments)
		goto out_of_memory;

	memset(pending_segments, 0, sizeof(pending_segments));
	INIT_LIST_HEAD(&pending_segments_free);

	for (i = 0; i < mmap_pages; i++) {
		pending_segments[i].grant_handle = USBBACK_INVALID_HANDLE;
		pending_pages[i] = alloc_page(GFP_KERNEL | __GFP_HIGHMEM);
		if (pending_pages[i] == NULL)
			goto out_of_memory;
		pending_segments[i].page = pending_pages[i];
		list_add_tail(&pending_segments[i].free_list, &pending_segments_free);
	}
	pending_segments_free_cnt = mmap_pages;

	usbif_interface_init();

	memset(pending_reqs, 0, sizeof(pending_reqs));
	INIT_LIST_HEAD(&pending_free);

	for (i = 0; i < usbif_reqs; i++) {
		init_timer(&pending_reqs[i].timer);
		list_add_tail(&pending_reqs[i].free_list, &pending_free);
	}

	INIT_LIST_HEAD(&pending_to_free);

	usbif_xenbus_init();

	printk("USB backend driver intialized!\n");

	return 0;

 out_of_memory:
	kfree(pending_reqs);
	kfree(pending_segments);
	for (i = 0; i < mmap_pages; i++) {
		if (pending_pages[i])
			__free_page(pending_pages[i]);
	}
	vfree(pending_pages);
	printk("%s: out of memory\n", __FUNCTION__);
	return -ENOMEM;
}

module_init(usbif_init);

MODULE_LICENSE("Dual BSD/GPL");
