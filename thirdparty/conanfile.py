from conan import ConanFile
from conan.tools.cmake import cmake_layout
from conan.tools.cmake import CMakeToolchain
import os

class FeatherRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    def requirements(self):
        self.requires("sdl/3.2.20")

    def build_requirements(self):
        self.tool_requires("cmake/3.27.9")
        self.tool_requires("ninja/1.13.0")

    def layout(self):
        # self.folders.source = ".."
        # self.folders.build = "conan/build"
        # self.folders.generators = "conan/build"
        cmake_layout(self, src_folder="..", generator="Ninja")
        # self.folders.root = ".."
        # self.folders.source = "."
        # # self.folders.build = "./conan"
        # self.folders.generators = os.path.join("build", str(self.settings.build_type), "generators")
        # self.folders.build = os.path.join("build", str(self.settings.build_type))

    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = 'ConanPresets.json'
        tc.generator="Ninja"
        tc.absolute_paths = True
        tc.generate()