/* Trace out MLE header before SENTER txt.c */
    {
        uint8_t *ptr = (uint8_t*)(g_il_kernel_setup.protected_mode_base +
                       g_il_kernel_setup.boot_params->slaunch_info.sl_mle_hdr);
        int i;
        uint64_t e2sts;

        printk(TBOOT_INFO"MLE header (at least I hope so)\n");
        for ( i = 0; i < 0x34; i++ ) {
            if (!(i % 16) && i != 0)
                printk(TBOOT_INFO"\n");
            printk(TBOOT_INFO"%2.2x ", ptr[i]);
        }
        printk(TBOOT_INFO"\n");

        e2sts = read_pub_config_reg(TXTCR_E2STS);
        printk(TBOOT_INFO"**^^$$E2STS: 0x%llx\n", e2sts);
    }

/* Setup earlyprintk in sl_main() */
extern char *vidmem;
extern int vidport;
extern int lines, cols;

void sl_main(u8 *bootparams)
{
	struct sha1_state sctx = {0};
	u8 sha1_hash[SHA1_DIGEST_SIZE];
	u32 cmdline_len;
	u64 cmdline_addr;
	struct tpm *tpm;
	int ret;

	boot_params = bootparams;

	sanitize_boot_params(boot_params);

	if (boot_params->screen_info.orig_video_mode == 7) {
		vidmem = (char *) 0xb0000;
		vidport = 0x3b4;
	} else {
		vidmem = (char *) 0xb8000;
		vidport = 0x3d4;
	}

	lines = boot_params->screen_info.orig_video_lines;
	cols = boot_params->screen_info.orig_video_cols;

	/* Move here from extract_kernel() */
	console_init();
	error_putstr("***RJP*** test string and hex:\n");
	error_puthex(0xffaa2211);

	...
}
