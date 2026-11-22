# @file
# stuart settings for the MacBookPro2026 platform (J804 (MacBook Pro 14-inch, M6) Windows and M1 profiles).
#
# Everything except the platform name lives in Platform/PlatformBuildCommon.py,
# so this package gets the same override generation (PlatformPreBuild), the
# override proof and the clean-submodule check (PlatformPostBuild) as every
# other platform. The profile is chosen with BLD_*_ defines on the stuart
# command line (see Tools/build-j804-*.sh), not with a Features.py device, so
# DEVICE stays None.
#
# Copyright (c) Microsoft Corporation.
# Copyright (c) 2026 Aurora Silicon
# SPDX-License-Identifier: BSD-2-Clause-Patent
##
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PlatformBuildCommon import ApplePlatformBuilder, ApplePlatformSettings, stuart_main

from edk2toolext.environment.uefi_build import UefiBuilder
from edk2toolext.invocables.edk2_platform_build import BuildSettingsManager
from edk2toolext.invocables.edk2_pr_eval import PrEvalSettingsManager
from edk2toolext.invocables.edk2_setup import SetupSettingsManager
from edk2toolext.invocables.edk2_update import UpdateSettingsManager

PLATFORM = "MacBookPro2026"
DEVICE = None


class SettingsManager(ApplePlatformSettings, UpdateSettingsManager, SetupSettingsManager,
                      PrEvalSettingsManager):
    PLATFORM = PLATFORM


class PlatformBuilder(ApplePlatformBuilder, UefiBuilder, BuildSettingsManager):
    PLATFORM = PLATFORM
    DEVICE = DEVICE

    def __init__(self):
        UefiBuilder.__init__(self)


if __name__ == "__main__":
    stuart_main(__file__)
