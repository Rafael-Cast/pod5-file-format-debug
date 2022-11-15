"""
lib_pod5_format setup.py
Proprietary and confidential information of Oxford Nanopore Technologies plc
All rights reserved; (c)2022: Oxford Nanopore Technologies plc

This script can either install a development version of pod5_format to the current
Python environment, or create a Python wheel.

Note that this is *not* intended to be run from within the "pod5_format" folder of
the pod5_file_format repository, because the libraries are
not actually installed there. See INSTALL.md for further details.

"""
import sys
import setuptools

extra_setup_args = {}

if "bdist_wheel" in sys.argv:
    # We need to convince distutils to put lib-pod5 in a platform-dependent
    # location (as opposed to a "universal" one) or auditwheel will complain
    # later. This is a hack to get it to do that.
    # See https://github.com/pypa/auditwheel/pull/28#issuecomment-212082647
    import platform
    from distutils.command.install import install

    class BinaryInstall(install):
        def __init__(self, dist):
            super().__init__(dist)
            # We should be able to set install_lib = self.install_platlib
            # but that doesn't appear to work on OSX or Linux, so we have to do this.
            if platform.system() != "Windows":
                self.install_lib = ""
            else:
                self.install_lib = self.install_platlib

    extra_setup_args["cmdclass"] = {"install": BinaryInstall}


if __name__ == "__main__":
    setuptools.setup(
        has_ext_modules=lambda: True,
        **extra_setup_args,
    )
