/** SPDX-License-Identifier: BSD-2-Clause-Patent */
// Create volatile phase indicators before UefiRuntimeLib's TPL_NOTIFY
// ExitBootServices callback makes them read-only. The public phase policy,
// failure handling and missed-phase callbacks remain unchanged. This event
// runs once even if a stale memory-map key requires an ExitBootServices retry;
// phase locks remain monotonic across that retry.
#define gEfiEventExitBootServicesGuid gEfiEventBeforeExitBootServicesGuid
#include "../../../../../Common/MU/MsCorePkg/MuVarPolicyFoundationDxe/MuVarPolicyFoundationDxe.c"
#undef gEfiEventExitBootServicesGuid
