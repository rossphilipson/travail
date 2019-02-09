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

