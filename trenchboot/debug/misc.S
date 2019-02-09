/* Use the TXT error register to show a value in sl_stub.S */
	movl	%esi, (TXT_PRIV_CONFIG_REGS_BASE + TXTCR_ERRORCODE)
	movl	(TXT_PRIV_CONFIG_REGS_BASE + TXTCR_E2STS), %eax
	movl	$1, (TXT_PRIV_CONFIG_REGS_BASE + TXTCR_CMD_UNLOCK_MEM_CONFIG)
	movl	(TXT_PRIV_CONFIG_REGS_BASE + TXTCR_E2STS), %eax
	movl	$1, (TXT_PRIV_CONFIG_REGS_BASE + TXTCR_CMD_RESET)

	jmp	debug_put_chars
debug_back:

