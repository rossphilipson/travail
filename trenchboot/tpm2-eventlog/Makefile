PROG=tpm2_eventlog
SRCS=tpm2_eventlog.c tpm2_eventlog_yaml.c log.c tpm2_tool_output.c tpm2_alg_util.c tpm2_openssl.c files.c
SRCS+=tpm2_util.c tpm2_errata.c pcr.c tpm2_attr_util.c
LIBS=-lcrypto -luuid
CFLAGS += -Wall -O2 -D_LINUX -Wstrict-prototypes

all: $(PROG)

$(PROG) : $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) $(LIBS) -o $(PROG)

CLEANFILES= $(PROG)

clean:
	rm -f $(CLEANFILES) $(patsubst %.c,%.o, $(SRCS))
