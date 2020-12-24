# blackbox testing the debug server
# this is necessary since ultimately we will have a separate process (IDE debugger)
# that talks to the debug server via ws

import asyncio
import os
import socket
import subprocess
import time
import json
from contextlib import closing

import websockets


def start_server(port_num):
    root = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
    # find build folder
    dirs = [d for d in os.listdir(root) if os.path.isdir(d) and "build" in d]
    assert len(dirs) > 0, "Unable to detect build folder"
    # use the first one
    build_dir = dirs[0]
    server_path = os.path.join(build_dir, "tests", "test_debug_server")
    p = subprocess.Popen([server_path, "+DEBUG_PORT=" + str(port_num),
                          "+DEBUG_LOG"], stdout=subprocess.PIPE)
    # sleep a little bit so that the server will setup properly
    time.sleep(0.1)
    return p


def find_free_port():
    with closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as s:
        s.bind(('', 0))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return s.getsockname()[1]


def test_continue_stop():
    port = find_free_port()
    s = start_server(port)
    assert s.poll() is None

    continue_payload = {"request": True, "type": "command", "payload": {"command": "continue"}}
    continue_str = json.dumps(continue_payload)

    async def send_msg():
        uri = "ws://localhost:{0}".format(port)
        async with websockets.connect(uri) as ws:
            await ws.send(continue_str)

    asyncio.get_event_loop().run_until_complete(send_msg())
    s.terminate()
    out = s.communicate()[0].decode("ascii")
    assert "INFO: START RUNNING" in out
    while s.poll() is None:
        time.sleep(0.1)
    # TODO:
    #   add test for stop


if __name__ == "__main__":
    test_continue_stop()
