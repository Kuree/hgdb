# blackbox testing the debug server
# this is necessary since ultimately we will have a separate process (IDE debugger)
# that talks to the debug server via ws

import asyncio
import time
import json
import websockets


def test_continue_stop(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"], use_plus_arg=True)
    assert s.poll() is None

    continue_payload = {"request": True, "type": "command", "payload": {"command": "continue"}}
    stop_payload = {"request": True, "type": "command", "payload": {"command": "stop"}}
    continue_str = json.dumps(continue_payload)
    stop_payload = json.dumps(stop_payload)

    async def send_msg():
        uri = "ws://localhost:{0}".format(port)
        async with websockets.connect(uri) as ws:
            await ws.send(continue_str)
            # wait a little bit
            time.sleep(0.1)
            # send stop
            await ws.send(stop_payload)

    asyncio.get_event_loop().run_until_complete(send_msg())
    # check if process exit
    t = time.time()
    killed = False
    while time.time() < t + 1:
        if s.poll() is not None:
            killed = True
            break
    assert killed
    out = s.communicate()[0].decode("ascii")
    assert "INFO: START RUNNING" in out


if __name__ == "__main__":
    from conftest import start_server_fn, find_free_port_fn
    test_continue_stop(start_server_fn, find_free_port_fn)
