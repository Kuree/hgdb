import os
import sys
import subprocess
import shutil
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext


# Notice:
# Much of the content is copied from
# https://github.com/pybind/cmake_example/blob/master/setup.py
# with the following changes:
#  - On unix, make is used instead of Ninja.
#  - use DEBUG=1 to build in debug mode

# Convert distutils Windows platform specifiers to CMake -A arguments
PLAT_TO_CMAKE = {
    "win32": "Win32",
    "win-amd64": "x64",
    "win-arm32": "ARM",
    "win-arm64": "ARM64",
}


# A CMakeExtension needs a sourcedir instead of a file list.
# The name must be the _single_ output extension from the CMake build.
# If you need multiple extensions, see scikit-build.
class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        # required for auto-detection of auxiliary "native" libs
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        cfg = "Debug" if self.debug else "Release"
        if "DEBUG" in os.environ:
            cfg = "Debug"

        # CMake lets you override the generator - we need to check this.
        # Can be set with Conda-Build, for example.
        cmake_generator = os.environ.get("CMAKE_GENERATOR", "")

        # Set Python_EXECUTABLE instead if you use PYBIND11_FINDPYTHON
        cmake_args = [
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={}".format(extdir),
            "-DPYTHON_EXECUTABLE={}".format(sys.executable),
            "-DCMAKE_BUILD_TYPE={}".format(cfg),  # not used on MSVC, but no harm
        ]
        build_args = []

        if self.compiler.compiler_type == "msvc":
            # Single config generators are handled "normally"
            single_config = any(x in cmake_generator for x in {"NMake", "Ninja"})

            # CMake allows an arch-in-generator style for backward compatibility
            contains_arch = any(x in cmake_generator for x in {"ARM", "Win64"})

            # Specify the arch if using MSVC generator, but only if it doesn't
            # contain a backward-compatibility arch spec already in the
            # generator name.
            if not single_config and not contains_arch:
                cmake_args += ["-A", PLAT_TO_CMAKE[self.plat_name]]

            # Multi-config generators have a different way to specify configs
            if not single_config:
                cmake_args += [
                    "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}".format(cfg.upper(), extdir)
                ]
                build_args += ["--config", cfg]

        build_args += ["-j2"]

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        subprocess.check_call(
            ["cmake", ext.sourcedir] + cmake_args, cwd=self.build_temp
        )
        make_targets = ["hgdb", "hgdb-replay-bin"]
        subprocess.check_call(
            ["cmake", "--build", ".", "--target"] + make_targets + build_args, cwd=self.build_temp
        )
        # need to copy binaries over
        binaries = [os.path.join(self.build_temp, "tools", "hgdb-replay", "hgdb-replay")]
        for binary in binaries:
            assert os.path.isfile(binary)
            print(extdir, binary)
            shutil.copy(binary, extdir)


root_dir = os.path.abspath(os.path.dirname(__file__))
with open(os.path.join(root_dir, "README.rst")) as f:
    long_description = f.read()
with open(os.path.join(root_dir, "VERSION")) as f:
    version = f.read().strip()

setup(
    name='libhgdb',
    version=version,
    author='Keyi Zhang',
    author_email='keyi@cs.stanford.edu',
    long_description=long_description,
    long_description_content_type='text/x-rst',
    url="https://github.com/Kuree/hgdb",
    python_requires=">=3.6",
    scripts=["scripts/tools/hgdb-replay"],
    ext_modules=[CMakeExtension("libhgdb")],
    cmdclass={"build_ext": CMakeBuild},
)
