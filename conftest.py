import pytest
import os
import subprocess
import sys
import time
import socket
from contextlib import closing


def start_server_fn(port_num, program_name, args=None, gdbserver_port=None, stdout=True, wait=0):
    root = os.path.dirname(os.path.realpath(__file__))
    # find build folder
    dirs = [d for d in os.listdir(root) if os.path.isdir(d) and "build" in d]
    assert len(dirs) > 0, "Unable to detect build folder"
    # use the shortest one
    dirs.sort(key=lambda x: len(x))
    build_dir = dirs[0]
    server_path = os.path.join(build_dir, "tests", program_name)
    if args is None:
        args = []
    args.append("+DEBUG_PORT=" + str(port_num))
    args = [server_path] + args
    if gdbserver_port is not None:
        args = ["gdbserver", "localhost:{0}".format(gdbserver_port)] + args
    p = subprocess.Popen(args, stdout=sys.stdout if stdout else subprocess.PIPE)
    time.sleep(wait)
    return p


def find_free_port_fn():
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(('', 0))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return s.getsockname()[1]


@pytest.fixture()
def start_server():
    return start_server_fn


@pytest.fixture()
def find_free_port():
    return find_free_port_fn
