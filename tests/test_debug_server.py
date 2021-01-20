# blackbox testing the debug server
# this is necessary since ultimately we will have a separate process (IDE debugger)
# that talks to the debug server via ws

import asyncio
import time
import os

import hgdb


def kill_server(s):
    s.terminate()
    while s.poll() is None:
        pass


def is_killed(s):
    t = time.time()
    killed = False
    while time.time() < t + 1:
        if s.poll() is not None:
            killed = True
            break
    return killed


def test_continue_stop(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG", "+NO_EVAL"], stdout=False)
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        await client.continue_()
        await client.stop()

    asyncio.get_event_loop().run_until_complete(test_logic())
    # check if process exit
    killed = is_killed(s)
    if not killed:
        s.terminate()
    assert killed
    out = s.communicate()[0].decode("ascii")
    assert "INFO: START RUNNING" in out
    assert "INFO: STOP RUNNING" in out


def test_bp_location_request(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"])
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)
    num_instances = 2

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        # search for the whole file
        resp = await client.request_breakpoint_location("/tmp/test.py")
        assert not resp["request"]
        assert resp["type"] == "bp-location"
        bps = resp["payload"]
        assert len(bps) == 5 * num_instances
        lines = set()
        for bp in bps:
            lines.add(bp["line_num"])
        assert len(lines) == 5
        assert 5 in lines

        # single line
        resp = await client.request_breakpoint_location("/tmp/test.py", 1)
        assert not resp["request"]
        bps = resp["payload"]
        assert len(bps) == 1 * num_instances

        # no bps
        resp = await client.request_breakpoint_location("/tmp/test.py", 42)
        assert not resp["request"]
        assert len(resp["payload"]) == 0

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_breakpoint_request(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"])
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)
    num_instances = 2

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        resp = await client.set_breakpoint("/tmp/test.py", 1, token="bp1")
        assert resp["token"] == "bp1"
        # asking for status
        info = (await client.get_info())["payload"]
        assert len(info["breakpoints"]) == 1 * num_instances
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
        # add bp by id
        await client.set_breakpoint_id(1)
        info = (await client.get_info())["payload"]
        assert len(info["breakpoints"]) == 1
        # remove by id
        await client.remove_breakpoint_id(1)
        info = (await client.get_info())["payload"]
        assert len(info["breakpoints"]) == 0

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_breakpoint_hit_continue(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"])
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
        # should receive two instances
        # they should have the same information
        assert len(bp_info1["payload"]["instances"]) == 2
        assert bp_info1["payload"]["instances"][0]["instance_name"] == "mod"
        assert bp_info1["payload"]["instances"][1]["instance_name"] == "mod2"
        await client.continue_()
        bp_info2 = await client.recv()
        assert bp_info2["payload"]["line_num"] == 1

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_breakpoint_step_over(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"])
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        await client.step_over()
        await client.continue_()
        bp1 = await client.recv()
        await client.continue_()
        await client.recv()  # second instance
        await client.continue_()  # second instance
        bp2 = await client.recv()
        await client.continue_()
        await client.recv()  # second instance
        await client.continue_()  # second instance
        bp3 = await client.recv()
        bp4 = None
        for i in range(2):  # skip the 6
            await client.continue_()
            await client.recv()  # second instance
            await client.continue_()  # second instance
            bp4 = await client.recv()
        # the sequence should be 1, 2, 5, 6, 1, ...
        assert bp1["payload"]["line_num"] == 1 and bp4["payload"]["line_num"] == 1
        assert bp2["payload"]["line_num"] == 2
        assert bp3["payload"]["line_num"] == 5

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_trigger(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"])
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        await client.set_breakpoint("/tmp/test.py", 6)
        await client.continue_()
        await client.recv()
        await client.continue_()
        await client.recv()
        await client.continue_()
        # set with timeout
        await client.recv(0.5)
    # should not trigger any more since things are stable
    # as a result the simulation should finish

    try:
        asyncio.get_event_loop().run_until_complete(test_logic())
        assert False, "Should not receive breakpoint"
    except asyncio.TimeoutError:
        pass

    kill_server(s)


def test_src_mapping(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"])
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)
    dirname = "/workspace/test"
    mapping = {dirname: "/tmp/"}
    filename = os.path.join(dirname, "test.py")

    async def test_logic():
        client = hgdb.HGDBClient(uri, None, mapping)
        await client.connect()
        await client.set_src_mapping(mapping)
        await client.set_breakpoint(filename, 1)
        await client.continue_()
        bp = await client.recv()
        assert bp["payload"]["filename"] == filename

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_evaluate(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"])
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        resp = await client.evaluate("", "42")
        assert resp["payload"]["result"] == "42"
        resp = await client.evaluate("mod", "a + 41")
        assert resp["payload"]["result"] == "42"
        resp = await client.evaluate("", "mod.a + 41")
        assert resp["payload"]["result"] == "42"
        resp = await client.evaluate("1", "a + 41")
        assert resp["payload"]["result"] == "42"
        resp = await client.evaluate("test", "1", check_error=False)
        assert resp["status"] == "error"

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_options(start_server, find_free_port):
    port = find_free_port()
    s = start_server(port, "test_debug_server", ["+DEBUG_LOG"])
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        resp = await client.get_info("options")
        assert resp["payload"]["options"]["log_enabled"]
        assert not resp["payload"]["options"]["single_thread_mode"]

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


if __name__ == "__main__":
    import sys

    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from conftest import start_server_fn, find_free_port_fn

    test_options(start_server_fn, find_free_port_fn)
