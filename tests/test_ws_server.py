# blackbox testing the ws server
# this is necessary since ultimately we will have a separate process (IDE debugger)
# that talks to the debug server via ws

import asyncio
import time

import websockets


def test_echo(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_ws_server", wait=0.05)

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
    while s.poll() is None:
        pass


def test_shutdown(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_ws_server", wait=0.05)

    async def send_msg():
        uri = "ws://localhost:{0}".format(port)
        payload = "stop"
        async with websockets.connect(uri) as ws:
            await ws.send(payload)

    asyncio.get_event_loop().run_until_complete(send_msg())
    # check if process exit
    t = time.time()
    killed = False
    while time.time() < t + 1:
        if s.poll() is not None:
            killed = True
            break
    if not killed:
        s.terminate()
    assert killed


def test_topic_pub(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_ws_server", wait=0.05)

    async def send_msg():
        uri = "ws://localhost:{0}".format(port)
        payload1 = "42"
        payload2 = "hello world"
        async with websockets.connect(uri) as ws1:
            async with websockets.connect(uri) as ws2:
                await ws1.send(payload1)
                echo1 = await ws1.recv()
                assert echo1 == payload1
                await ws2.send(payload2)
                await ws1.recv()
                echo2 = await ws1.recv()
                assert(echo2 == payload2 * 2)

    asyncio.get_event_loop().run_until_complete(send_msg())
    # kill the server
    s.terminate()
    while s.poll() is None:
        pass


if __name__ == "__main__":
    import os
    import sys
    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from conftest import start_server_fn, find_free_port_fn

    test_topic_pub(start_server_fn, find_free_port_fn)
