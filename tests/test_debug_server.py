# blackbox testing the debug server
# this is necessary since ultimately we will have a separate process (IDE debugger)
# that talks to the debug server via ws

import asyncio
import time
import hgdb


def kill_server(s):
    s.terminate()
    while s.poll() is None:
        pass


def test_continue_stop(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG", "+NO_EVAL"], use_plus_arg=True)
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        await client.continue_()
        await client.stop()
    asyncio.get_event_loop().run_until_complete(test_logic())
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
    out = s.communicate()[0].decode("ascii")
    assert "INFO: START RUNNING" in out
    assert "INFO: STOP RUNNING" in out


def test_bp_location_request(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"], use_plus_arg=True)
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        # search for the whole file
        resp = await client.request_breakpoint_location("/tmp/test.py")
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
        resp = await client.request_breakpoint_location("/tmp/test.py", 1)
        assert not resp["request"]
        bps = resp["payload"]
        assert len(bps) == 1

        # no bps
        resp = await client.request_breakpoint_location("/tmp/test.py", 42)
        assert not resp["request"]
        assert len(resp["payload"]) == 0

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_breakpoint_request(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"], use_plus_arg=True)
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        resp = await client.set_breakpoint("/tmp/test.py", 1, token="bp1")
        assert resp["token"] == "bp1"
        # asking for status
        info = (await client.get_info())["payload"]
        assert len(info["breakpoints"]) == 1
        assert info["breakpoints"][0]["line_num"] == 1
        # send a wrong one
        resp = await client.set_breakpoint("/tmp/test.py", 42, token="bp2", check_error=False)
        assert resp["token"] == "bp2" and resp["status"] == "error"
        # remove the breakpoint
        resp = await client.remove_breakpoint("/tmp/test.py", 1, token="bp1")
        assert resp["token"] == "bp1" and resp["status"] == "success"
        # query the system about breakpoints. it should be empty now
        info = (await client.get_info())["payload"]
        assert len(info["breakpoints"]) == 0

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_breakpoint_hit_continue(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"], use_plus_arg=True)
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        await client.set_breakpoint("/tmp/test.py", 1, token="bp1")
        # inserted but shall not trigger
        await client.set_breakpoint("/tmp/test.py", 4, token="bp1")
        # continue
        await client.continue_()
        # should get breakpoint info
        bp_info1 = await client.recv()
        assert bp_info1["payload"]["line_num"] == 1
        await client.continue_()
        bp_info2 = await client.recv()
        assert bp_info2["payload"]["line_num"] == 1

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_breakpoint_step_over(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"], use_plus_arg=True)
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        await client.step_over()
        await client.continue_()
        bp1 = await client.recv()
        await client.continue_()
        bp2 = await client.recv()
        await client.continue_()
        bp3 = await client.recv()
        await client.continue_()
        bp4 = await client.recv()
        # the sequence should be 1, 2, 5, 1, ...
        assert bp1["payload"]["line_num"] == 1 and bp4["payload"]["line_num"] == 1
        assert bp2["payload"]["line_num"] == 2
        assert bp3["payload"]["line_num"] == 5

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


if __name__ == "__main__":
    import os
    import sys

    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from conftest import start_server_fn, find_free_port_fn

    test_breakpoint_step_over(start_server_fn, find_free_port_fn)
