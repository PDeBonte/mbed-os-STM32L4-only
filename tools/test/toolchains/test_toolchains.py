#!/usr/bin/env python
# Copyright (c) 2019 Arm Limited and Contributors. All rights reserved.
#
# SPDX-License-Identifier: Apache-2.0

"""Test the arm toolchain."""

import os
import sys
from unittest import TestCase

import mock


ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..")
)
sys.path.insert(0, ROOT)

from tools.toolchains.arm import ARM_STD, ARM_MICRO, ARMC6
from tools.toolchains.gcc import GCC_ARM
from tools.toolchains.iar import IAR
from tools.toolchains.mbed_toolchain import UNSUPPORTED_C_LIB_EXECPTION_STRING
from tools.utils import NotSupportedException

class TestArmToolchain(TestCase):
    """Test Arm classes."""

    def test_arm_minimal_printf(self):
        """Test that linker flags are correctly added to an instance of ARM."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.printf_lib = "minimal-printf"
        mock_target.default_lib = "std"
        mock_target.supported_c_libs = {"arm": ["std"]}
        mock_target.supported_toolchains = ["ARM", "uARM", "ARMC5"]

        arm_std_obj = ARM_STD(mock_target)
        arm_micro_obj = ARM_MICRO(mock_target)
        arm_c6_obj = ARMC6(mock_target)

        self.assertIn("-DMBED_MINIMAL_PRINTF", arm_std_obj.flags["common"])
        self.assertIn("-DMBED_MINIMAL_PRINTF", arm_micro_obj.flags["common"])
        self.assertIn("-DMBED_MINIMAL_PRINTF", arm_c6_obj.flags["common"])

    def test_arm_default_lib(self):
        """Test that linker flags are correctly added to an instance of ARM."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.supported_c_libs = {"arm": ["small"]}
        mock_target.default_lib = "sMALL"
        mock_target.default_toolchain = "ARM"
        mock_target.supported_toolchains = ["ARM", "uARM", "ARMC5", "ARMC6"]
        arm_std_obj = ARM_STD(mock_target)
        arm_micro_obj = ARM_MICRO(mock_target)

        mock_target.default_toolchain = "ARMC6"
        arm_c6_obj = ARMC6(mock_target)

        self.assertIn("-D__MICROLIB", arm_std_obj.flags["common"])
        self.assertIn("-D__MICROLIB", arm_micro_obj.flags["common"])
        self.assertIn("-D__MICROLIB", arm_c6_obj.flags["common"])

        self.assertIn("--library_type=microlib", arm_std_obj.flags["ld"])
        self.assertIn("--library_type=microlib", arm_micro_obj.flags["ld"])
        self.assertIn("--library_type=microlib", arm_c6_obj.flags["ld"])

        self.assertIn("-Wl,--library_type=microlib", arm_c6_obj.flags["cxx"])
        self.assertIn("--library_type=microlib", arm_c6_obj.flags["asm"])

    def test_arm_default_lib_std_exception(self):
        """Test that exception raised when default_lib is std but supported_c_libs parameter arm is not suppoted std lib."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.supported_toolchains = ["ARM", "uARM", "ARMC5"]
        mock_target.default_toolchain = "ARM"
        mock_target.default_lib = "std"
        mock_target.supported_c_libs = {"arm": ["small"]}
        with self.assertRaisesRegexp(NotSupportedException, UNSUPPORTED_C_LIB_EXECPTION_STRING.format(mock_target.default_lib)):
            ARM_STD(mock_target)
        with self.assertRaisesRegexp(NotSupportedException, UNSUPPORTED_C_LIB_EXECPTION_STRING.format(mock_target.default_lib)):
            ARMC6(mock_target)


    def test_arm_default_lib_small_exception(self):
        """Test that exception raised when default_lib is small but supported_c_libs parameter arm is not suppoted small lib."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.default_lib = "small"
        mock_target.supported_c_libs = {"arm": ["std"]}
        mock_target.default_toolchain = "ARM"
        mock_target.supported_toolchains = ["ARM", "uARM", "ARMC5"]
        with self.assertRaisesRegexp(NotSupportedException, UNSUPPORTED_C_LIB_EXECPTION_STRING.format(mock_target.default_lib)):
            ARM_STD(mock_target)
        mock_target.default_toolchain = "ARMC6"
        with self.assertRaisesRegexp(NotSupportedException, UNSUPPORTED_C_LIB_EXECPTION_STRING.format(mock_target.default_lib)):
            ARMC6(mock_target)

class TestGccToolchain(TestCase):
    """Test the GCC class."""

    def test_gcc_minimal_printf(self):
        """Test that linker flags are correctly added to an instance of GCC_ARM."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.printf_lib = "minimal-printf"
        mock_target.supported_toolchains = ["GCC_ARM"]
        mock_target.default_lib = "std"
        mock_target.supported_c_libs = {"gcc_arm": ["std"]}
        mock_target.is_TrustZone_secure_target = False

        gcc_obj = GCC_ARM(mock_target)

        self.assertIn("-DMBED_MINIMAL_PRINTF", gcc_obj.flags["common"])

        minimal_printf_wraps = [
            "-Wl,--wrap,printf",
            "-Wl,--wrap,sprintf",
            "-Wl,--wrap,snprintf",
            "-Wl,--wrap,vprintf",
            "-Wl,--wrap,vsprintf",
            "-Wl,--wrap,vsnprintf",
            "-Wl,--wrap,fprintf",
            "-Wl,--wrap,vfprintf",
        ]

        for i in minimal_printf_wraps:
            self.assertIn(i, gcc_obj.flags["ld"])

    def test_gcc_arm_default_lib(self):
        """Test that linker flags are correctly added to an instance of GCC_ARM."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.supported_c_libs = {"gcc_arm": ["small"]}
        mock_target.default_lib = "sMALL"
        mock_target.supported_toolchains = ["GCC_ARM"]
        mock_target.is_TrustZone_secure_target = False
        gcc_arm_obj = GCC_ARM(mock_target)
        self.assertIn("-DMBED_RTOS_SINGLE_THREAD", gcc_arm_obj.flags["common"])
        self.assertIn("-D__NEWLIB_NANO", gcc_arm_obj.flags["common"])
        self.assertIn("--specs=nano.specs", gcc_arm_obj.flags["ld"])

    def test_gcc_arm_default_lib_std_exception(self):
        """Test that exception raised when default_lib is std but supported_c_libs parameter arm is not suppoted std lib."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.default_toolchain = "ARM"
        mock_target.default_lib = "std"
        mock_target.supported_c_libs = {"arm": ["small"]}
        with self.assertRaisesRegexp(NotSupportedException, UNSUPPORTED_C_LIB_EXECPTION_STRING.format(mock_target.default_lib)):
            GCC_ARM(mock_target)

    def test_gcc_arm_default_lib_small_exception(self):
        """Test that exception raised when default_lib is small but supported_c_libs parameter arm is not suppoted small lib."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.default_lib = "small"
        mock_target.supported_c_libs = {"arm": ["std"]}
        mock_target.default_toolchain = "ARM"
        mock_target.supported_toolchains = ["ARM", "uARM", "ARMC5"]
        with self.assertRaisesRegexp(NotSupportedException, UNSUPPORTED_C_LIB_EXECPTION_STRING.format(mock_target.default_lib)):
            GCC_ARM(mock_target)

class TestIarToolchain(TestCase):
    """Test the IAR class."""

    def test_iar_minimal_printf(self):
        """Test that linker flags are correctly added to an instance of IAR."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.printf_lib = "minimal-printf"
        mock_target.supported_toolchains = ["IAR"]
        mock_target.default_lib = "std"
        mock_target.supported_c_libs = {"iar": ["std"]}
        mock_target.is_TrustZone_secure_target = False

        iar_obj = IAR(mock_target)
        var = "-DMBED_MINIMAL_PRINTF"
        self.assertIn("-DMBED_MINIMAL_PRINTF", iar_obj.flags["common"])

    def test_iar_default_lib(self):
        """Test that linker flags are correctly added to an instance of IAR."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.supported_c_libs = {"iar": ["std"]}
        mock_target.default_lib = "sTD"
        mock_target.supported_toolchains = ["IAR"]
        mock_target.is_TrustZone_secure_target = False
        try:
            IAR(mock_target)
        except NotSupportedException:
            self.fail(UNSUPPORTED_C_LIB_EXECPTION_STRING.format(mock_target.default_lib))

    def test_iar_default_lib_std_exception(self):
        """Test that exception raised when default_lib is small but supported_c_libs parameter iar is not supported small lib."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.microlib_supported = False
        mock_target.default_lib = "std"
        mock_target.supported_c_libs = {"iar": ["small"]}
        mock_target.supported_toolchains = ["IAR"]
        with self.assertRaisesRegexp(NotSupportedException, UNSUPPORTED_C_LIB_EXECPTION_STRING.format(mock_target.default_lib)):
            IAR(mock_target)

    def test_iar_default_lib_small_exception(self):
        """Test that exception raised when default_lib is small but supported_c_libs parameter iar is not supported small lib."""
        mock_target = mock.MagicMock()
        mock_target.core = "Cortex-M4"
        mock_target.microlib_supported = False
        mock_target.default_lib = "small"
        mock_target.supported_c_libs = {"iar": ["std"]}
        mock_target.supported_toolchains = ["IAR"]
        with self.assertRaisesRegexp(NotSupportedException, UNSUPPORTED_C_LIB_EXECPTION_STRING.format(mock_target.default_lib)):
            IAR(mock_target)
