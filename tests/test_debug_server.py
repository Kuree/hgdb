# blackbox testing the debug server
# this is necessary since ultimately we will have a separate process (IDE debugger)
# that talks to the debug server via ws

import asyncio
import os
import socket
import subprocess
import time
from contextlib import closing

import pytest
import websockets


def start_server(port_num):
    root = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
    # find build folder
    dirs = [d for d in os.listdir(root) if os.path.isdir(d) and "build" in d]
    assert len(dirs) > 0, "Unable to detect build folder"
    # use the first one
    build_dir = dirs[0]
    server_path = os.path.join(build_dir, "tests", "test_debug_server")
    return subprocess.Popen([server_path, str(port_num)])


def find_free_port():
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(('', 0))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return s.getsockname()[1]


def test_echo():
    port = find_free_port()
    s = start_server(port)

    async def send_msg():
        uri = "ws://localhost:{0}".format(port)
        payload = "hello world"
        async with websockets.connect(uri) as ws:
            await ws.send(payload)
            echo = await ws.recv()
            assert echo == payload

    asyncio.get_event_loop().run_until_complete(send_msg())
    # kill the server
    s.terminate()


@pytest.mark.skip(reason="stop not working yet")
def test_shutdown():
    port = find_free_port()
    s = start_server(port)

    async def send_msg():
        uri = "ws://localhost:{0}".format(port)
        payload = "stop"
        async with websockets.connect(uri) as ws:
            await ws.send(payload)
            await ws.recv()
    asyncio.get_event_loop().run_until_complete(send_msg())
    # check if process exit
    time.sleep(0.5)
    assert s.poll() is None


if __name__ == "__main__":
    test_shutdown()
