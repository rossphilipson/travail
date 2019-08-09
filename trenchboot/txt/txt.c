/*
 * Intel TXT implementation file.
 *
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 */

void txt_smx_parameters (struct smx_parameters *params)
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
