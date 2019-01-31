#include <tpm.h>
#include <tpmbuff.h>

#include "crb.h"
#include "tpm_common.h"

#define TPM_LOC_STATE		0x0000
#define TPM_LOC_CTRL		0x0008
#define TPM_LOC_STS		0x000C
#define TPM_CRB_INTF_ID		0x0030
#define TPM_CRB_CTRL_EXT	0x0038
#define TPM_CRB_CTRL_REQ	0x0040
#define TPM_CRB_CTRL_STS	0x0044
#define TPM_CRB_CTRL_CANCEL	0x0048
#define TPM_CRB_CTRL_START	0x004C
#define TPM_CRB_INT_ENABLE	0x0050
#define TPM_CRB_INT_STS		0x0054
#define TPM_CRB_CTRL_CMD_SIZE	0x0058
#define TPM_CRB_CTRL_CMD_LADDR	0x005C
#define TPM_CRB_CTRL_CMD_HADDR	0x0060
#define TPM_CRB_CTRL_RSP_SIZE	0x0064
#define TPM_CRB_CTRL_RSP_ADDR	0x0068
#define TPM_CRB_DATA_BUFFER	0x0080

#define REGISTER(l,r)		(((l) << 12) | r)

static u16 locality;

struct tpm_loc_state {
	union {
		u8 val;
		struct {
			u8 tpm_established:1;
			u8 loc_assigned:1;
			u8 active_locality:3;
			u8 _reserved:2;
			u8 tpm_reg_valid_sts:1;
		};
	};
} __attribute__ ((packed));

struct tpm_loc_ctrl {
	union {
		u32 val;
		struct {
			u32 request_access:1;
			u32 relinquish:1;
			u32 seize:1;
			u32 reset_establishment_bit:1;
			u32 _reserved:28;
		};
	};
} __attribute__ ((packed));

struct tpm_loc_sts {
	union {
		u32 val;
		struct {
			u32 granted:1;
			u32 beenSeized:1;
			u32 _reserved:30;
		};
	};
} __attribute__ ((packed));

struct tpm_crb_ctrl_req {
	union {
		u32 val;
		struct {
			u32 cmd_ready:1;
			u32 go_idle:1;
			u32 _reserved:30;
		};
	};
} __attribute__ ((packed));

struct tpm_crb_ctrl_sts {
	union {
		u32 val;
		struct {
			u32 tpm_sts:1;
			u32 tpm_idle:1;
			u32 _reserved:30;
		};
	};
} __attribute__ ((packed));

struct tpm_crb_intf_id_ext {
	union {
		u32 val;
		struct {
			u32 vid:16;
			u32 did:16;
		};
	};
} __attribute__ ((packed));

/* Durations derived from Table 15 of the PTP but is purely an artifact of this
 * implementation */

/* TPM Duration A: 20ms */
static void duration_a(void)
{
	udelay(20);
}

/* TPM Duration B: 750ms */
static void duration_b(void)
{
	udelay(750);
}

/* TPM Duration C: 1000ms */
static void duration_c(void)
{
	udelay(1000);
}

/* Timeouts defined in Table 16 of the PTP */

/* TPM Timeout A: 750ms */
static void timeout_a(void)
{
	udelay(750);
}

/* TPM Timeout B: 2000ms */
static void timeout_b(void)
{
	udelay(2000);
}

/* TPM Timeout C: 200ms */
static void timeout_c(void)
{
	udelay(200);
}

/* TPM Timeout D: 30ms */
static void timeout_d(void)
{
	udelay(30);
}

static u8 is_idle(void)
{
	struct tpm_crb_ctrl_sts ctl_sts;

	ctl_sts.val = tpm_read32(REGISTER(locality,TPM_CRB_CTRL_STS));
	if (ctl_sts.tpm_idle == 1) {
		return 1;
	}

	return 0;
}

static u8 is_ready(void)
{
	struct tpm_crb_ctrl_sts ctl_sts;

	ctl_sts.val = tpm_read32(REGISTER(locality,TPM_CRB_CTRL_STS));
	if (ctl_sts.tpm_idle == 1) {
		return 0;
	}

	return 1;
}

static u8 is_cmd_exec(void)
{
	u32 ctrl_start;

	ctrl_start = tpm_read32(REGISTER(locality, TPM_CRB_CTRL_START));
	if (ctrl_start == 1) {
		return 1;
	}

	return 0;
}

static u8 cmd_ready(void)
{
	struct tpm_crb_ctrl_req ctl_req;

	if (is_idle()) {
		ctl_req.cmd_ready = 1;
		tpm_write32(REGISTER(locality,TPM_CRB_CTRL_REQ), ctl_req.val);
		timeout_c();

		if (is_idle())
			return -1;
	}

	return 0;
}

static void go_idle(void)
{
	struct tpm_crb_ctrl_req ctl_req;

	if (is_idle())
		return;

	ctl_req.go_idle = 1;
	tpm_write32(REGISTER(locality,TPM_CRB_CTRL_REQ), ctl_req.val);

	/* pause to give tpm time to complete the request */
	timeout_c();

	return;
}

void crb_relinquish_locality(u16 l)
{
	struct tpm_loc_ctrl loc_ctrl;

	loc_ctrl.relinquish = 1;

	tpm_write32(REGISTER(l, TPM_LOC_CTRL), loc_ctrl.val);
}

u8 crb_request_locality(u8 l)
{
	struct tpm_loc_state loc_state;
	struct tpm_loc_ctrl loc_ctrl;
	struct tpm_loc_sts loc_sts;

	/* TPM_LOC_STATE is aliased across all localities */
	loc_state.val = tpm_read8(REGISTER(0, TPM_LOC_STATE));

	if (loc_state.loc_assigned == 1) {
		if (loc_state.active_locality == l) {
			locality = l;
                        return locality;
                }

		crb_relinquish_locality(loc_state.loc_assigned);
	}

	loc_ctrl.request_access = 1;
	tpm_write32(REGISTER(l, TPM_LOC_CTRL), loc_ctrl.val);

	loc_sts.val = tpm_read32(REGISTER(l, TPM_LOC_STS));
	if (loc_sts.granted != 1)
		return NO_LOCALITY;

	locality = l;
	return locality;
}


u8 crb_init(struct tpm *t)
{
	u8 i;
	struct tpm_crb_intf_id_ext id;

	for (i=0; i<=MAX_LOCALITY; i++)
		crb_relinquish_locality(i);

	if (crb_request_locality(0) == NO_LOCALITY)
		return 0;

	id.val = tpm_read32(REGISTER(0,TPM_CRB_INTF_ID+4));
	t->vendor = ((id.vid & 0x00FF) << 8) | ((id.vid & 0xFF00) >> 8);
	if ((t->vendor & 0xFFFF) == 0xFFFF)
		return 0;

	/* have the tpm invalidate the buffer if left in completion state */
	go_idle();
	/* now move to ready state */
	cmd_ready();

	return 1;
}

/* assumes cancel will succeed */
static void cancel_send(void)
{
	if (is_cmd_exec()) {
		tpm_write32(REGISTER(locality, TPM_CRB_CTRL_CANCEL), 1);
		timeout_b();

		tpm_write32(REGISTER(locality, TPM_CRB_CTRL_CANCEL), 0);
	}
}

size_t crb_send(struct tpmbuff *buf)
{
	u32 ctrl_start = 1;
	u8 count = 0;

	if (is_idle())
		return 0;

	tpm_write32(REGISTER(locality, TPM_CRB_CTRL_START), ctrl_start);

	/* most command sequences this code is interested with operates with
	 * 20/750 duration/timeout schedule
	 * */
	duration_a();
	ctrl_start = tpm_read32(REGISTER(locality, TPM_CRB_CTRL_START));
	if (ctrl_start != 0) {
		timeout_a();
		ctrl_start = tpm_read32(REGISTER(locality, TPM_CRB_CTRL_START));
		if (ctrl_start != 0) {
			cancel_send();
			/* minimum response is header with cancel ord */
			return sizeof(struct tpm_header);
		}
	}

	return buf->len;
}

size_t crb_recv(struct tpmbuff *buf)
{
	/* noop, currently send waits until execution is complete*/
	return 0;
}
