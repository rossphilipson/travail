#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define __packed __attribute__((packed))

#define SHA1_DIGEST_SIZE        20
#define SHA256_DIGEST_SIZE      32
#define SHA512_DIGEST_SIZE      64

#define TPM_HASH_ALG_SHA1    (uint16_t)(0x0004)
#define TPM_HASH_ALG_SHA256  (uint16_t)(0x000B)
#define TPM_HASH_ALG_SHA384  (uint16_t)(0x000C)
#define TPM_HASH_ALG_SHA512  (uint16_t)(0x000D)

/*
 * Secure Launch event log entry type. The TXT specification defines the
 * base event value as 0x400 for DRTM values.
 */
#define TXT_EVTYPE_BASE		0x400
#define TXT_EVTYPE_SLAUNCH	(TXT_EVTYPE_BASE + 0x102)

/*
 * TPM event log structures defined in both the TXT specification and
 * the TCG documentation.
 */
#define TPM12_EVTLOG_SIGNATURE "TXT Event Container"

struct tpm12_event_log_header {
	char		signature[20];
	char		reserved[12];
	uint8_t		container_ver_major;
	uint8_t		container_ver_minor;
	uint8_t		pcr_event_ver_major;
	uint8_t		pcr_event_ver_minor;
	uint32_t	container_size;
	uint32_t	pcr_events_offset;
	uint32_t	next_event_offset;
	/* PCREvents[] */
} __packed;

struct tpm12_pcr_event {
	uint32_t	pcr_index;
	uint32_t	type;
	uint8_t		digest[20];
	uint32_t	size;
	/* Data[] */
} __packed;

#define TPM20_EVTLOG_SIGNATURE "Spec ID Event03"

struct tpm20_ha {
	uint16_t	algorithm_id;
	/* digest[AlgorithmID_DIGEST_SIZE] */
} __packed;

struct tpm20_digest_values {
	uint32_t	count;
	/* TPMT_HA digests[count] */
} __packed;

struct tpm20_pcr_event_head {
	uint32_t	pcr_index;
	uint32_t	event_type;
} __packed;

/* Variable size array of hashes in the tpm20_digest_values structure */

struct tpm20_pcr_event_tail {
	uint32_t	event_size;
	/* Event[EventSize]; */
} __packed;

#define SL_MAX_EVENT_DATA	64
#define SL_TPM12_LOG_SIZE	(sizeof(struct tpm12_pcr_event) + \
				SL_MAX_EVENT_DATA)
#define SL_TPM20_LOG_SIZE	(sizeof(struct tpm20_ha) + \
				SHA512_DIGEST_SIZE + \
				sizeof(struct tpm20_digest_values) + \
				sizeof(struct tpm20_pcr_event_head) + \
				sizeof(struct tpm20_pcr_event_tail) + \
				SL_MAX_EVENT_DATA)

static int fd;

static inline int tpm_log_event(uint32_t event_size, void *event)
{
	ssize_t ret;

	ret = write(fd, event, event_size);
	if (ret == -1) {
		printf("Failed to write event to log\n");
		return -1;
	}

	return 0;
}

static void sl_tpm12_log_event(uint32_t pcr, uint8_t *digest,
			       const uint8_t *event_data, uint32_t event_size)
{
	struct tpm12_pcr_event *pcr_event;
	uint32_t total_size;
	uint8_t log_buf[SL_TPM12_LOG_SIZE];

	memset(log_buf, 0, SL_TPM12_LOG_SIZE);
	pcr_event = (struct tpm12_pcr_event *)log_buf;
	pcr_event->pcr_index = pcr;
	pcr_event->type = TXT_EVTYPE_SLAUNCH;
	memcpy(&pcr_event->digest[0], digest, SHA1_DIGEST_SIZE);
	pcr_event->size = event_size;
	memcpy((uint8_t *)pcr_event + sizeof(struct tpm12_pcr_event),
	       event_data, event_size);

	total_size = sizeof(struct tpm12_pcr_event) + event_size;

	if (tpm_log_event(total_size, pcr_event))
		printf("Failed to write TPM 1.2 event\n");
}

static void sl_tpm20_log_event(uint32_t pcr, uint8_t *digest, uint16_t algo,
			       const uint8_t *event_data, uint32_t event_size)
{
	struct tpm20_pcr_event_head *head;
	struct tpm20_digest_values *dvs;
	struct tpm20_ha *ha;
	struct tpm20_pcr_event_tail *tail;
	uint8_t *dptr;
	uint32_t total_size;
	uint8_t log_buf[SL_TPM20_LOG_SIZE];

	memset(log_buf, 0, SL_TPM20_LOG_SIZE);
	head = (struct tpm20_pcr_event_head *)log_buf;
	head->pcr_index = pcr;
	head->event_type = TXT_EVTYPE_SLAUNCH;
	dvs = (struct tpm20_digest_values *)
		((uint8_t *)head + sizeof(struct tpm20_pcr_event_head));
	dvs->count = 1;
	ha = (struct tpm20_ha *)
		((uint8_t *)dvs + sizeof(struct tpm20_digest_values));
	ha->algorithm_id = algo;
	dptr = (uint8_t *)ha + sizeof(struct tpm20_ha);

	switch (algo) {
	case TPM_HASH_ALG_SHA512:
		memcpy(dptr, digest, SHA512_DIGEST_SIZE);
		tail = (struct tpm20_pcr_event_tail *)
			(dptr + SHA512_DIGEST_SIZE);
		break;
	case TPM_HASH_ALG_SHA256:
		memcpy(dptr, digest, SHA256_DIGEST_SIZE);
		tail = (struct tpm20_pcr_event_tail *)
			(dptr + SHA256_DIGEST_SIZE);
		break;
	case TPM_HASH_ALG_SHA1:
	default:
		memcpy(dptr, digest, SHA1_DIGEST_SIZE);
		tail = (struct tpm20_pcr_event_tail *)
			(dptr + SHA1_DIGEST_SIZE);
	};

	tail->event_size = event_size;
	memcpy((uint8_t *)tail + sizeof(struct tpm20_pcr_event_tail),
	       event_data, event_size);

	total_size = (uint32_t)((uint8_t *)tail - (uint8_t *)head) +
		sizeof(struct tpm20_pcr_event_tail) + event_size;

	if (tpm_log_event(total_size, &log_buf[0]))
		printf("Failed to write TPM 1.2 event\n");
}

void log_event(int is_tpm20)
{
	uint8_t digest[20];

	fd = open("/sys/kernel/security/slaunch/eventlog", O_WRONLY);
	if (fd == -1) {
		printf("Failed to open slaunch/eventlog node\n");
		return;
	}

	if (is_tpm20)
		sl_tpm20_log_event(23, &digest[0], TPM_HASH_ALG_SHA1,
				   "Test event 20", strlen("Test event 20"));
	else
		sl_tpm12_log_event(23, &digest[0],
				   "Test event 12", strlen("Test event 12"));

	close(fd);
}

void usage(void)
{
	printf("Usage: tpmwrevt <-1|-2>\n");
}

int main(int argc, char *argv[])
{
	int c;

	if (argc <= 1) {
		usage();
		return 0;
	}

	for ( ; ; ) {
		c = getopt(argc, argv, "12");
		if (c == -1)
			break;
		switch ( c ) {
		case '1':
			printf("TPM 1.2 log event\n");
			log_event(0);
			break;
		case '2':
			printf("TPM 2.0 log event\n");
			log_event(1);
			break;
		case 'h':
		case '?':
		default:
			usage();
		}
	}

	return 0;
}
