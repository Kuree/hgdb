# blackbox testing the debug server
# this is necessary since ultimately we will have a separate process (IDE debugger)
# that talks to the debug server via ws

import asyncio
import os
import time

import hgdb
import pytest


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


def setup_server(start_server, find_free_port, no_eval=False, stdout=False, env=None):
    port = find_free_port()
    args = ["+DEBUG_LOG", "+NO_EVAL"] if no_eval else ["+DEBUG_LOG"]
    s = start_server(port, "test_debug_server", args, stdout=stdout, env=env)
    assert s.poll() is None
    uri = "ws://localhost:{0}".format(port)
    return s, uri


def test_continue_stop(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port, True)

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
    s, uri = setup_server(start_server, find_free_port)
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
    s, uri = setup_server(start_server, find_free_port)
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
    s, uri = setup_server(start_server, find_free_port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        await client.set_breakpoint("/tmp/test.py", 1, token="bp1")
        # inserted but shall not trigger
        await client.set_breakpoint("/tmp/test.py", 4, token="bp1")
        # continue
        await client.continue_()
        # should get breakpoint info
        bp_info1 = await client.recv_bp()
        assert bp_info1["payload"]["line_num"] == 1
        # should receive two instances
        # they should have the same information
        assert len(bp_info1["payload"]["instances"]) == 2
        assert bp_info1["payload"]["instances"][0]["instance_name"] == "mod"
        assert bp_info1["payload"]["instances"][1]["instance_name"] == "mod2"
        await client.continue_()
        bp_info2 = await client.recv_bp()
        assert bp_info2["payload"]["line_num"] == 1

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_breakpoint_step_over(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        await client.step_over()
        await client.step_over()
        bp1 = await client.recv_bp()
        await client.step_over()
        await client.recv_bp()  # second instance
        await client.step_over()  # second instance
        bp2 = await client.recv_bp()
        await client.step_over()
        await client.recv_bp()  # second instance
        await client.step_over()  # second instance
        bp3 = await client.recv_bp()
        bp4 = None
        for i in range(2):  # skip the 6
            await client.step_over()
            await client.recv_bp()  # second instance
            await client.step_over()  # second instance
            bp4 = await client.recv_bp()
        # the sequence should be 1, 2, 5, 6, 1, ...
        assert bp1["payload"]["line_num"] == 1 and bp4["payload"]["line_num"] == 1
        assert bp2["payload"]["line_num"] == 2
        assert bp3["payload"]["line_num"] == 5

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_trigger(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port)

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
    s, uri = setup_server(start_server, find_free_port)
    dirname = "/workspace/test"
    mapping = {dirname: "/tmp/"}
    filename = os.path.join(dirname, "test.py")

    async def test_logic():
        client = hgdb.HGDBClient(uri, None, mapping)
        await client.connect()
        await client.set_src_mapping(mapping)
        await client.set_breakpoint(filename, 1)
        await client.continue_()
        bp = await client.recv_bp()
        assert bp["payload"]["filename"] == filename

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_evaluate(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        resp = await client.evaluate("", "42")
        assert resp["payload"]["result"] == "42"
        resp = await client.evaluate("mod", "a + 41", is_context=False)
        assert resp["payload"]["result"] == "42"
        resp = await client.evaluate("", "mod.a + 41")
        assert resp["payload"]["result"] == "42"
        resp = await client.evaluate("1", "a + 41")
        assert resp["payload"]["result"] == "42"
        # as long as it's a valid expression, even if the scope is wrong it is fine
        resp = await client.evaluate("test", "1")
        assert resp["payload"]["result"] == "1"
        resp = await client.evaluate("", "test.a", check_error=False)
        assert resp["status"] == "error"

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_options(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        resp = await client.get_info("options")
        assert resp["payload"]["options"]["log_enabled"]
        assert not resp["payload"]["options"]["single_thread_mode"]
        # change it as well
        await client.change_option(log_enabled=False, single_thread_mode=True)
        resp = await client.get_info("options")
        assert not resp["payload"]["options"]["log_enabled"]
        assert resp["payload"]["options"]["single_thread_mode"]

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_watch(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port)

    async def test_logic():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        id1 = await client.add_monitor("a", 1)
        id2 = await client.add_monitor("b", 1, monitor_type="clock_edge")
        await client.set_breakpoint("/tmp/test.py", 1)
        await client.continue_()
        await client.recv_bp()  # breakpoint
        watch1 = await client.recv()
        assert watch1["payload"]["track_id"] == id1
        await client.continue_()
        watch2 = await client.recv()
        assert watch2["payload"]["track_id"] == id2
        await client.recv()  # breakpoint
        watch3 = await client.recv()
        assert watch3["payload"]["track_id"] == id1
        # remove id2
        await client.remove_monitor(id2)
        await client.continue_()
        bp = await client.recv_bp()
        assert bp["type"] == "breakpoint"

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_detach(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port)

    async def test_logic1():
        async with hgdb.HGDBClient(uri, None) as client:
            await client.connect()
            await client.change_option(detach_after_disconnect=True)
            await client.set_breakpoint("/tmp/test.py", 1)
            await client.continue_()
            await client.recv_bp()

    async def test_logic2():
        client = hgdb.HGDBClient(uri, None)
        await client.connect()
        info = await client.get_info("status")
        assert "Simulation paused: false" in info["payload"]["status"]

    asyncio.get_event_loop().run_until_complete(test_logic1())
    asyncio.get_event_loop().run_until_complete(test_logic2())
    kill_server(s)


def test_step_back(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port)

    async def test_logic():
        async with hgdb.HGDBClient(uri, None) as client:
            await client.connect()
            await client.step_over()
            bp1 = await client.recv_bp()
            await client.step_over()
            bp2 = await client.recv_bp()
            assert bp1 != bp2
            await client.step_back()
            bp3 = await client.recv_bp()
            assert bp3 == bp1
            await client.step_back()
            bp4 = await client.recv_bp()
            # this will be stuck at the very beginning of the evaluation loop
            assert bp4 == bp1

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_set_value(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port)

    async def test_logic():
        async with hgdb.HGDBClient(uri, None) as client:
            await client.connect()
            await client.set_value("mod.a", 42)
            await client.set_breakpoint("/tmp/test.py", 1)
            await client.continue_()
            bp = await client.recv_bp()
            assert bp["payload"]["instances"][0]["local"]["a"] == "42"
            assert bp["payload"]["instances"][1]["local"]["a"] == "1"

    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


def test_special_value(start_server, find_free_port):
    s, uri = setup_server(start_server, find_free_port)

    async def test_logic():
        async with hgdb.HGDBClient(uri, None) as client:
            await client.connect()
            await client.set_breakpoint("/tmp/test.py", 1, cond="$instance == 1")
            for i in range(5):
                await client.continue_()
                bp = await client.recv_bp()
                assert bp["payload"]["instances"][0]["instance_id"] == 1
            res = await client.evaluate("0", "$time + 1")
            assert res["payload"]["result"] == "5"
    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


@pytest.mark.skip(reason="Skip for now since it always break the CI flow. Will fix it later")
def test_debug_env_value(start_server, find_free_port):
    env = {"DEBUG_BREAKPOINT0": "/tmp/test.py:1@$instance == 1"}
    s, uri = setup_server(start_server, find_free_port, env=env, stdout=True)

    async def test_logic():
        async with hgdb.HGDBClient(uri, None) as client:
            await client.connect()
            for i in range(5):
                await client.continue_()
                bp = await client.recv_bp()
                assert bp["payload"]["instances"][0]["instance_id"] == 1
    asyncio.get_event_loop().run_until_complete(test_logic())
    kill_server(s)


if __name__ == "__main__":
    import sys

    sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from conftest import start_server_fn, find_free_port_fn

    test_debug_env_value(start_server_fn, find_free_port_fn)
