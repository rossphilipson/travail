/******************************************************************************
 * usbif.h
 * 
 * Unified usb-device I/O interface for Xen guest OSes.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Derived from blkif.h
 *
 * Copyright (c) 2003-2004, Keir Fraser
 * Copyright (c) 2008, Virtual Computer Inc.
 */

#ifndef __XEN_PUBLIC_IO_USBIF_H__
#define __XEN_PUBLIC_IO_USBIF_H__

#include "ring.h"
#include "../grant_table.h"

/*
 * Front->back notifications: When enqueuing a new request, sending a
 * notification can be made conditional on req_event (i.e., the generic
 * hold-off mechanism provided by the ring macros). Backends must set
 * req_event appropriately (e.g., using RING_FINAL_CHECK_FOR_REQUESTS()).
 * 
 * Back->front notifications: When enqueuing a new response, sending a
 * notification can be made conditional on rsp_event (i.e., the generic
 * hold-off mechanism provided by the ring macros). Frontends must set
 * rsp_event appropriately (e.g., using RING_FINAL_CHECK_FOR_RESPONSES()).
 */

#ifndef usbif_vdev_t
#define usbif_vdev_t   uint16_t
#endif

/* URB operations */
#define USBIF_T_CNTRL           0
#define USBIF_T_ISOC            1
#define USBIF_T_BULK            2
#define USBIF_T_INT             3

/* non URB operations */
#define USBIF_T_RESET		4
#define USBIF_T_ABORT_PIPE	5
#define USBIF_T_GET_FRAME	6
#define USBIF_T_GET_SPEED	7
#define USBIF_T_CANCEL		8

#define USBIF_T_MAX		(USBIF_T_CANCEL)

#define USBIF_F_SHORTOK		0x01
#define USBIF_F_RESET		0x02
#define USBIF_F_ASAP		0x04 // start ISO request on next available frame
#define USBIF_F_INDIRECT	0x08 // this request contains indirect segments 
#define USBIF_F_CYCLE_PORT	0x10 // force re-enumeration of this device
#define USBIF_F_DIRECT_DATA	0x20 // request contains data directly inline

/*
 * Maximum scatter/gather segments per request.
 * This is carefully chosen so that sizeof(usbif_ring_t) <= PAGE_SIZE.
 * NB.
 */
#define USBIF_MAX_SEGMENTS_PER_REQUEST 17
#define USBIF_MAX_ISO_SEGMENTS         (USBIF_MAX_SEGMENTS_PER_REQUEST - 1)

typedef uint32_t usbif_request_len_t;

struct usbif_request {
    uint64_t            id;           /* private guest value, echoed in resp */
    uint64_t            setup;
    uint8_t             type;         /* USBIF_T_??? */
    uint8_t             endpoint;
    uint16_t            offset;
    usbif_request_len_t length;
    uint8_t             nr_segments;  /* number of segments */
    uint8_t             flags;
    uint16_t            nr_packets;   /* number of ISO packets */
    uint32_t            startframe;
    union {
        grant_ref_t     gref[USBIF_MAX_SEGMENTS_PER_REQUEST];
        uint8_t         data[sizeof(grant_ref_t)*USBIF_MAX_SEGMENTS_PER_REQUEST];
    } u;
    uint32_t            pad;
};
typedef struct usbif_request usbif_request_t;

#define INDIRECT_SEGMENTS

#ifdef INDIRECT_SEGMENTS
#define USBIF_MAX_SEGMENTS_PER_IREQUEST 1023
struct usbif_indirect_request {
    uint32_t		nr_segments;
    grant_ref_t         gref[USBIF_MAX_SEGMENTS_PER_IREQUEST];
};
typedef struct usbif_indirect_request usbif_indirect_request_t;
#endif

struct usbif_iso_packet_info {
    usbif_request_len_t offset;
    uint16_t            length;
    uint16_t            status;
};
typedef struct usbif_iso_packet_info usbif_iso_packet_info_t;

struct usbif_response {
    uint64_t            id;              /* copied from request */
    usbif_request_len_t actual_length;
    uint32_t            data;
    int16_t             status;          /* USBIF_RSP_???       */
    uint32_t            pad;
};
typedef struct usbif_response usbif_response_t;

/*
 * STATUS RETURN CODES.
 */
 /* USB errors */
#define USBIF_RSP_USB_ERROR	-10

#define USBIF_USB_CANCELED	-1
#define USBIF_RSP_USB_CANCELED	(USBIF_RSP_USB_ERROR + USBIF_USB_CANCELED)
#define USBIF_USB_PENDING	-2
#define USBIF_RSP_USB_PENDING	(USBIF_RSP_USB_ERROR + USBIF_USB_PENDING)
#define USBIF_USB_PROTO		-3
#define USBIF_RSP_USB_PROTO	(USBIF_RSP_USB_ERROR + USBIF_USB_PROTO)
#define USBIF_USB_CRC		-4
#define USBIF_RSP_USB_CRC	(USBIF_RSP_USB_ERROR + USBIF_USB_CRC)
#define USBIF_USB_TIMEOUT	-5
#define USBIF_RSP_USB_TIMEOUT	(USBIF_RSP_USB_ERROR + USBIF_USB_TIMEOUT)
#define USBIF_USB_STALLED	-6
#define USBIF_RSP_USB_STALLED	(USBIF_RSP_USB_ERROR + USBIF_USB_STALLED)
#define USBIF_USB_INBUFF	-7
#define USBIF_RSP_USB_INBUFF	(USBIF_RSP_USB_ERROR + USBIF_USB_INBUFF)
#define USBIF_USB_OUTBUFF	-8
#define USBIF_RSP_USB_OUTBUFF	(USBIF_RSP_USB_ERROR + USBIF_USB_OUTBUFF)
#define USBIF_USB_OVERFLOW	-9
#define USBIF_RSP_USB_OVERFLOW	(USBIF_RSP_USB_ERROR + USBIF_USB_OVERFLOW)
#define USBIF_USB_SHORTPKT	-10
#define USBIF_RSP_USB_SHORTPKT	(USBIF_RSP_USB_ERROR + USBIF_USB_SHORTPKT)
#define USBIF_USB_DEVRMVD	-11
#define USBIF_RSP_USB_DEVRMVD	(USBIF_RSP_USB_ERROR + USBIF_USB_DEVRMVD)
#define USBIF_USB_PARTIAL	-12
#define USBIF_RSP_USB_PARTIAL	(USBIF_RSP_USB_ERROR + USBIF_USB_PARTIAL)
#define USBIF_USB_INVALID	-13
#define USBIF_RSP_USB_INVALID	(USBIF_RSP_USB_ERROR + USBIF_USB_INVALID)
#define USBIF_USB_RESET		-14
#define USBIF_RSP_USB_RESET	(USBIF_RSP_USB_ERROR + USBIF_USB_RESET)
#define USBIF_USB_SHUTDOWN	-15
#define USBIF_RSP_USB_SHUTDOWN	(USBIF_RSP_USB_ERROR + USBIF_USB_SHUTDOWN)
#define USBIF_USB_UNKNOWN	-16
#define USBIF_RSP_USB_UNKNOWN	(USBIF_RSP_USB_ERROR + USBIF_USB_UNKNOWN)

 /* Operation not supported. */
#define USBIF_RSP_EOPNOTSUPP  -2
 /* Operation failed for some unspecified reason (-EIO). */
#define USBIF_RSP_ERROR       -1
 /* Operation completed successfully. */
#define USBIF_RSP_OKAY         0

#define USBIF_S_LOW		1
#define USBIF_S_FULL		2
#define USBIF_S_HIGH		3

/*
 * Generate usbif ring structures and types.
 */

DEFINE_RING_TYPES(usbif, struct usbif_request, struct usbif_response);

#endif /* __XEN_PUBLIC_IO_USBIF_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
