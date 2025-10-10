from conan import ConanFile
from conan.tools.cmake import cmake_layout
from conan.tools.cmake import CMakeToolchain

class FeatherRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    def requirements(self):
        self.requires("sdl/3.2.20")

    def build_requirements(self):
        self.tool_requires("cmake/4.1.0")

    def layout(self):
        # self.folders.source = ".."
        # self.folders.build = "conan/build"
        # self.folders.generators = "conan/build"
        cmake_layout(self)
        
    def generate(self):
        tc = CMakeToolchain(self)
        tc.user_presets_path = '../ConanPresets.json'
        tc.generate()