import subprocess
import abc
import os
import shutil


def get_root():
    path = os.path.abspath(__file__)
    for i in range(3):
        path = os.path.dirname(path)
    return path


def get_vector_file(filename):
    root = get_root()
    vector_dir = os.path.join(root, "tests", "generators", "vectors")
    return os.path.join(vector_dir, filename)


def get_uri(port):
    return "ws://localhost:{0}".format(port)


# simple CAD tool runners
class Tester:
    __test__ = False

    def __init__(self, *files: str, cwd=None, clean_up_run=False):
        self.lib_path = self.__find_libhgdb()
        self.files = []
        for file in files:
            self.files.append(os.path.abspath(file))
        self.cwd = self._process_cwd(cwd)
        self.clean_up_run = clean_up_run
        self.__process = []

    @staticmethod
    def __find_libhgdb():
        path = os.path.abspath(__file__)
        for i in range(3):
            path = os.path.dirname(path)
        # find if there is any build folder
        dirs = [d for d in os.listdir(path) if os.path.isdir(d) and "build" in d]
        assert len(dirs) > 0, "Unable to detect build folder"
        # use the shortest one
        dirs.sort(key=lambda x: len(x))
        build_dir = dirs[0]
        lib_path = os.path.join(build_dir, "src", "libhgdb.so")
        assert os.path.exists(lib_path), "Unable to find libhgdb.so"
        return lib_path

    @abc.abstractmethod
    def run(self, blocking=False, **kwargs):
        pass

    def _process_cwd(self, cwd):
        if cwd is None:
            cwd = "build"
        if not os.path.isdir(cwd):
            os.makedirs(cwd, exist_ok=True)
        # copy files over
        files = []
        for file in self.files:
            new_filename = os.path.abspath(os.path.join(cwd, os.path.basename(file)))
            if new_filename != file:
                if os.path.isfile(new_filename):
                    os.remove(new_filename)
                shutil.copyfile(file, new_filename)
            ext = os.path.splitext(new_filename)[-1]
            if ext != ".h" and ext != ".hh":
                files.append(new_filename)
        self.files = files
        return cwd

    def _set_lib_env(self):
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = os.path.dirname(os.path.abspath(self.lib_path))
        return env

    def _run(self, args, cwd, env, blocking):
        if blocking:
            subprocess.check_call(args, cwd=cwd, env=env)
        else:
            p = subprocess.Popen(args, cwd=cwd, env=env)
            self.__process.append(p)

    def clean_up(self):
        if self.clean_up_run and os.path.exists(self.cwd) and os.path.isdir(
                self.cwd):
            shutil.rmtree(self.cwd)

    @staticmethod
    def __get_flags(kwargs):
        flags = []
        for k, v in kwargs.items():
            flags.append("+{0}={1}".format(k, v))
        return flags

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.clean_up()
        for p in self.__process:
            p.kill()

    def __enter__(self):
        return self


class VerilatorTester(Tester):
    def __init__(self, *files: str, cwd=None, clean_up_run=False):
        super().__init__(*files, cwd=cwd, clean_up_run=clean_up_run)

    def run(self, blocking=True, **kwargs):
        # compile it first
        verilator = shutil.which("verilator")
        args = [verilator, "--cc", "--exe", "--vpi"]
        args += self.files + [os.path.abspath(self.lib_path), "-Wno-fatal"]
        subprocess.check_call(args, cwd=self.cwd)
        # symbolic link it first
        env = self._set_lib_env()

        # find the shortest file
        # automatic detect the makefile
        mk_file = ""
        for f in self.files:
            name, ext = os.path.splitext(os.path.basename(f))
            if ext == ".sv" or ext == ".v":
                mk_file = "V" + name + ".mk"
                break
        assert len(mk_file) > 0, "Unable to find any makefile from Verilator"
        # make the file
        subprocess.check_call(["make", "-C", "obj_dir", "-f", mk_file],
                              cwd=self.cwd, env=env)
        # run the application
        name = os.path.join("obj_dir", mk_file.replace(".mk", ""))
        flags = self.__get_flags(kwargs)
        self._run([name] + flags, self.cwd, env, blocking)


class CadenceTester(Tester):
    def __init__(self, *files: str, cwd=None, clean_up_run=False):
        super().__init__(*files, cwd=cwd, clean_up_run=clean_up_run)
        self.toolchain = ""
        self.lib_path = os.path.relpath(self.lib_path, cwd)

    def run(self, blocking=True, **kwargs):
        assert len(self.toolchain) > 0
        env = self._set_lib_env()
        # run it
        args = [self.toolchain] + list(self.files) + self.__get_flag()
        args += self.__get_flags(kwargs)
        self._run(args, self.cwd, env, blocking)

    def __get_flag(self):
        return ["-sv_lib", self.lib_path]


class XceliumTester(CadenceTester):
    def __init__(self, *files: str, cwd=None, clean_up_run=False):
        super().__init__(*files, cwd=cwd, clean_up_run=clean_up_run)
        self.toolchain = "xrun"


class VCSTester(Tester):
    def __init__(self, *files: str, cwd=None, clean_up_run=False):
        super().__init__(*files, cwd=cwd, clean_up_run=clean_up_run)

    def run(self, blocking=True, **kwargs):
        env = self._set_lib_env()
        # run it
        args = ["vcs"] + list(self.files) + self.__get_flag()
        self._run(args, self.cwd, env, True)
        # run the simv
        flags = self.__get_flags(kwargs)
        self._run(["./simv"] + flags, self.cwd, env, blocking)

    def __get_flag(self):
        return ["-sv_lib", self.lib_path, "-sverilog"]
