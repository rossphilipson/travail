// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The definitions in this header are extracted from:
 *  - Trusted Computing Group's "TPM Main Specification", Parts 1-3.
 *  - Trusted Computing Group's TPM 2.0 Library Specification Parts 1&2.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <asm/tpm.h>

#include "early_tpm.h"

static u8 locality = TPM_NO_LOCALITY;

