/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The definitions in this header are extracted from the Trusted Computing
 * Group's "TPM Main Specification", Parts 1-3.
 */

#ifndef _ASM_X86_TPM_H
#define _ASM_X86_TPM_H

#include <linux/types.h>

#define TPM_HASH_ALG_SHA1    (u16)(0x0004)
#define TPM_HASH_ALG_SHA256  (u16)(0x000B)
#define TPM_HASH_ALG_SHA384  (u16)(0x000C)
#define TPM_HASH_ALG_SHA512  (u16)(0x000D)
#define TPM_HASH_ALG_SM3_256 (u16)(0x0012)

