import pytest
import os
import subprocess
import time
import socket
from contextlib import closing


def start_server_fn(port_num, program_name, args=None, use_plus_arg=False):
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
    if use_plus_arg:
        args.append("+DEBUG_PORT=" + str(port_num))
    else:
        args.append(str(port_num))
    p = subprocess.Popen([server_path] + args, stdout=subprocess.PIPE)
    # sleep a little bit so that the server will setup properly
    time.sleep(0.05)
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
