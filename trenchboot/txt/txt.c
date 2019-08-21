/*
 * Intel TXT implementation file.
 *
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 */

void txt_smx_parameters(struct smx_parameters *params)
{
	u32 index = 0, eax, ebx, ecx, param_type;

	memset(params, 0, sizeof(struct smx_supported_versions));
	params->max_acm_size = SMX_DEFAULT_MAX_ACM_SIZE;
	params->acm_memory_types = SMX_DEFAULT_ACM_MEMORY_TYPE;
	params->senter_controls = SMX_DEFAULT_SENTER_CONTROLS;

	do {
		txt_getsec_parameters(index, &eax, &ebx, &ecx);
		param_type = eax & SMX_PARAMETER_TYPE_MASK;

		switch (param_type) {
		case SMX_PARAMETER_NULL:
			break; /* this means done */
		case SMX_PARAMETER_ACM_VERSIONS:
			if (params->version_count == SMX_PARAMETER_MAX_VERSIONS) {
				/* TODO log warning about too many versions */
				break;
			}
			params->versions[params->version_count].mask = ebx;
			params->versions[params->version_count++].version = ecx;
			break;
		case SMX_PARAMETER_MAX_ACM_SIZE:
			params->max_acm_size =
					SMX_GET_MAX_ACM_SIZE(eax);
			break;
		case SMX_PARAMETER_ACM_MEMORY_TYPES:
			params->acm_memory_types =
					SMX_GET_ACM_MEMORY_TYPES(eax);
			break;
		case SMX_PARAMETER_SENTER_CONTROLS:
			params->senter_controls =
					SMX_GET_SENTER_CONTROLS(eax);
			break;
		case SMX_PARAMETER_TXT_EXTENSIONS:
			params->txt_feature_ext_flags =
					SMX_GET_TXT_EXT_FEATURES(eax);
			break;
		default:
			/* TODO log warning about unknown param */
			param_type = SMX_PARAMETER_NULL;
		}
	} while (param_type != SMX_PARAMETER_NULL);

	/* If no ACM versions were found, set the default one */
	if (!params->version_count) {
		params->versions[0].mask = SMX_DEFAULT_VERSION_MASK;
		params->versions[0].version = SMX_DEFAULT_VERSION;
		params->version_count++;
	}
}

/* Page directory and table entries only need Present set */
#define MAKE_PT_ENTRY(addr)  (((u64)(unsigned long)(addr) & PAGE_MASK) | 0x01)

/*
 * The MLE page tables have to be below the MLE and have no special regions in
 * between them and the MLE (this is a bit of an unwritten rule).
 * 20 pages are carved out of memory below the MLE. That leave 18 page table
 * pages that can cover up to 36M .
 * can only contain 4k pages
 */
void *setup_mle_pagetables(u32 mle_start, u32 mle_size,
			   void *ptab_base, u32 ptab_size)
{
	u32 mle_off, pd_off;
	void *pg_dir_ptr_tab, *pg_dir, *pg_tab;
	u64 *pte, *pde;

	memset(ptab_base, 0, ptab_size);

	pg_dir_ptr_tab = ptab_base;
	pg_dir         = pg_dir_ptr_tab + PAGE_SIZE;
	pg_tab         = pg_dir + PAGE_SIZE;

	/* Only use first entry in page dir ptr table */
	*(u64 *)pg_dir_ptr_tab = MAKE_PT_ENTRY(pg_dir);

	/* Start with first entry in page dir */
	*(u64 *)pg_dir = MAKE_PT_ENTRY(pg_tab);

	pte = pg_tab;
	pde = pg_dir;
	mle_off = 0;
	pd_off = 0;

	do {
		*pte = MAKE_PT_ENTRY(mle_start + mle_off);

		pte++;
		mle_off += PAGE_SIZE;

		pd_off++;
		if ( !(pd_off % 512) ) {
			/* Break if we don't need any additional page entries */
			if (mle_off >= mle_size)
				break;

			pde++;
			*pde = MAKE_PT_ENTRY(pte);
		}
	} while ( mle_off < mle_size );

	return ptab_base;
}
