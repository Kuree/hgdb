# blackbox testing the debug server
# this is necessary since ultimately we will have a separate process (IDE debugger)
# that talks to the debug server via ws

import asyncio
import json
import time
import pytest

import websockets


def kill_server(s):
    s.terminate()
    while s.poll() is None:
        pass


def test_continue_stop(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG", "+NO_EVAL"], use_plus_arg=True)
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
    assert "INFO: STOP RUNNING" in out


def test_bp_location_request(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"], use_plus_arg=True)
    assert s.poll() is None

    # search for the whole file
    bp_location_payload1 = {"request": True, "type": "bp-location", "payload": {"filename": "/tmp/test.py"}}
    # only search for particular line
    bp_location_payload2 = {"request": True, "type": "bp-location",
                            "payload": {"filename": "/tmp/test.py", "line_num": 1}}
    bp_location_payload3 = {"request": True, "type": "bp-location",
                            "payload": {"filename": "/tmp/test.py", "line_num": 42}}
    bp_location_str1 = json.dumps(bp_location_payload1)
    bp_location_str2 = json.dumps(bp_location_payload2)
    bp_location_str3 = json.dumps(bp_location_payload3)

    async def send_msg():
        uri = "ws://localhost:{0}".format(port)
        async with websockets.connect(uri) as ws:
            await ws.send(bp_location_str1)
            resp = json.loads(await ws.recv())
            assert not resp["request"]
            assert resp["type"] == "bp-location"
            bps = resp["payload"]
            assert len(bps) == 4
            lines = set()
            for bp in bps:
                lines.add(bp["line_num"])
            assert len(lines) == 4
            assert 5 in lines

            # single line
            await ws.send(bp_location_str2)
            resp = json.loads(await ws.recv())
            assert not resp["request"]
            bps = resp["payload"]
            assert len(bps) == 1

            # no bps
            await ws.send(bp_location_str3)
            resp = json.loads(await ws.recv())
            assert not resp["request"]
            assert len(resp["payload"]) == 0

    asyncio.get_event_loop().run_until_complete(send_msg())
    kill_server(s)


def test_breakpoint_request(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"], use_plus_arg=True)
    assert s.poll() is None
    bp_payload1 = {"request": True, "type": "breakpoint", "token": "bp1",
                   "payload": {"filename": "/tmp/test.py", "line_num": 1, "action": "add"}}
    bp_payload2 = {"request": True, "type": "breakpoint", "token": "bp2",
                   "payload": {"filename": "/tmp/test.py", "line_num": 42, "action": "add"}}
    bp_payload3 = {"request": True, "type": "breakpoint", "token": "bp1",
                   "payload": {"filename": "/tmp/test.py", "line_num": 1, "action": "remove"}}
    info_payload = {"request": True, "type": "debugger-info", "payload": {"command": "breakpoints"}}
    bp_payload1_str = json.dumps(bp_payload1)
    bp_payload2_str = json.dumps(bp_payload2)
    bp_payload3_str = json.dumps(bp_payload3)
    info_payload_str = json.dumps(info_payload)

    async def send_msg():
        uri = "ws://localhost:{0}".format(port)
        async with websockets.connect(uri) as ws:
            await ws.send(bp_payload1_str)
            resp = json.loads(await ws.recv())
            assert resp["token"] == "bp1" and resp["status"] == "success"
            # send the duplicated request again
            await ws.send(bp_payload1_str)
            await ws.recv()
            # asking for status
            await ws.send(info_payload_str)
            payload = json.loads(await ws.recv())["payload"]
            assert len(payload["breakpoints"]) == 1
            assert payload["breakpoints"][0]["line_num"] == 1
            # send a wrong one
            await ws.send(bp_payload2_str)
            resp = json.loads(await ws.recv())
            assert resp["token"] == "bp2" and resp["status"] == "error"
            # remove the breakpoint
            await ws.send(bp_payload3_str)
            resp = json.loads(await ws.recv())
            assert resp["token"] == "bp1" and resp["status"] == "success"
            # query the system about breakpoints. it should be empty now
            await ws.send(info_payload_str)
            payload = json.loads(await ws.recv())["payload"]
            assert len(payload["breakpoints"]) == 0

    asyncio.get_event_loop().run_until_complete(send_msg())
    kill_server(s)


@pytest.mark.skip(reason="Not working yet")
def test_breakpoint_hit_continue(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"], use_plus_arg=True)
    assert s.poll() is None
    bp_payload = {"request": True, "type": "breakpoint", "token": "bp1",
                  "payload": {"filename": "/tmp/test.py", "line_num": 1, "action": "add"}}
    continue_payload = {"request": True, "type": "command", "payload": {"command": "continue"}}
    bp_payload_str = json.dumps(bp_payload)
    continue_payload_str = json.dumps(continue_payload)

    async def send_msg():
        uri = "ws://localhost:{0}".format(port)
        async with websockets.connect(uri) as ws:
            await ws.send(bp_payload_str)
            await ws.recv()
            # continue
            await ws.send(continue_payload_str)
            bp_info1 = await ws.recv()
            await ws.send(continue_payload_str)
            bp_info2 = await ws.recv()

    asyncio.get_event_loop().run_until_complete(send_msg())
    kill_server(s)


if __name__ == "__main__":
    import os
    import sys

    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from conftest import start_server_fn, find_free_port_fn

    test_breakpoint_hit_continue(start_server_fn, find_free_port_fn)
