import os, shutil
from conans import ConanFile, tools, MSBuild, AutoToolsBuildEnvironment
from conans.errors import ConanException

from contextlib import contextmanager


@contextmanager
def append_to_env_variable(var, value, separator, prepend=False):
    old_value = os.getenv(var, None)
    try:
        new_value = [old_value, value] if old_value else [value, ]
        if prepend:
            new_value = reversed(new_value)
        os.environ[var] = separator.join(new_value)
        yield
    finally:
        if old_value is not None:
            os.environ[var] = old_value
        else:
            del os.environ[var]

class LASAGNECoreConan(ConanFile):
    name = "LASAGNE-Core"
    version = "1.5.1" #make this automatic
    license = ""
    url = "https://github.com/LASAGNE-Open-Systems/LASAGNE-Core"
    description = "LASAGNE-Core"
    settings = "os", "compiler", "build_type", "arch"
    options = {"branch": "ANY"}
    generators = "visual_studio", "gcc"

    source_subfolder = 'source_subfolder'

    short_paths = True

    def build_requirements(self):
        if self.settings.os == "Windows":
            self.build_requires('strawberryperl/5.26.0@conan/stable')

    def configure(self):
        self.requires('ACE_TAO_MPC/6.4.8@lasagne/stable')
        self.requires('OpenDDS/3.12.2@lasagne/stable')
        
        if self.settings.os not in ["Windows", "Linux", "Macos"]:
            raise ConanException("Recipe for settings.os='{}' not implemented.".format(self.settings.os))
        if self.settings.os == "Windows" and self.settings.compiler != "Visual Studio":
            raise ConanException("Recipe for settings.os='{}' and compiler '{}' not implemented.".format(self.settings.os, self.settings.compiler))

    def source(self):
# set up these variables
        self.options.branch = "develop"                       # (master/develop/head/1.51 etc.)
        repository = "https://github.com/LASAGNE-Open-Systems/LASAGNE-Core.git"
#       if the repository has not been cloned yet
#       clone the repo
        command = ['git clone -b', str(self.options.branch), repository, self.source_folder, ]
        self.run(' '.join(command))
        
# If we want to pull submodules as well, add this to the end --recurse-submodules(?)
#
# If the repository is already there
#       pull the latest changes (TO TEST: can we just call clone and get Git to work it out?)
#        
        
    def build(self):
        working_dir = self.build_folder

        # Set env variables and run build
        with tools.environment_append({'DAF_ROOT': working_dir,
                                       'TAF_ROOT': os.path.join(working_dir, 'TAF')}):
            if self.settings.os == "Windows":
                self.build_windows(working_dir)
            elif self.settings.os == "Linux":
                self.build_linux(working_dir)
            else:
                self.build_macos(working_dir)

    def _exec_mpc(self, working_dir, mpc_type, mwc=None):
        mwc = mwc or os.path.join(working_dir, 'TAF', 'TAF.mwc')
        command = ['perl', os.path.join(os.environ['ACE_ROOT'], 'bin', 'mwc.pl'), '--type', mpc_type, mwc, ]
        self.run(' '.join(command))

    def build_windows(self, working_dir):
        assert self.settings.os == "Windows"
        assert self.settings.arch == 'x86_64'
        assert self.settings.compiler == "Visual Studio"

        # Generate project using MPC
        compiler_version = int(str(self.settings.compiler.version))
        if compiler_version <= 14:
            self._exec_mpc(working_dir, mpc_type='vc{}'.format(compiler_version))
        else:
            compiler_type = {15: '2017', }[compiler_version]
            self._exec_mpc(working_dir, mpc_type='vs{}'.format(compiler_type))

        # Compile
        # Do we want the .sln files to have the names represent the vs version being used?
        with append_to_env_variable("PATH", os.path.join(working_dir, 'lib'), ';', prepend=True):
            with append_to_env_variable("PATH", os.path.join(working_dir, 'bin'), ';', prepend=True):
                msbuild = MSBuild(self)
                sln = os.path.join(working_dir, 'TAF', 'TAF.sln')
                # for build_type='Debug' build release first
                if self.settings.build_type == 'Debug':
                    msbuild.build(sln, build_type='Release', upgrade_project=False)
                msbuild.build(sln, upgrade_project=False)

    def build_linux(self, working_dir):
        assert self.settings.os == "Linux"

    def build_macos(self, working_dir):
        assert self.settings.os == "Macos"
    
    def package(self):
        self.copy("*.*", dst="", src="", keep_path="True", excludes=("*.iobj", "*.ipdb", "*.tlog", "*.obj", "*.lastbuildstate"))

    def package_info(self):
        working_dir = self.package_folder
        #self.cpp_info.libs = tools.collect_libs(self)
        self.env_info.DAF_ROOT = working_dir
        self.env_info.TAF_ROOT = str(os.path.join(working_dir, 'TAF'))
        self.env_info.PATH.append(os.path.join(working_dir, 'lib'))
        self.env_info.PATH.append(os.path.join(working_dir, 'bin'))




# There is a Conan function :
#    def exports_sources(self): 

# This could be useful because what it does is tell the Conan package to include the source code 
# This means that a conan package will build from Sources that it drags with it, rather than going to the GitHub repo and downloading it (which will cause issues on the PRN) 
