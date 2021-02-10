import os
import pytest
import hgdb
import tempfile
import asyncio
import time


def get_line_num(filename, pattern):
    with open(filename) as f:
        lines = f.readlines()
    lines = [l.rstrip() for l in lines]
    return lines.index(pattern) + 1


def write_out_db3(filename, sv):
    db = hgdb.DebugSymbolTable(filename)
    db.store_instance(0, "mod")
    db.store_breakpoint(0, 0, sv, get_line_num(sv, "    if (rst)"))
    db.store_breakpoint(1, 0, sv, get_line_num(sv, "        out <= 0;"), condition="rst")
    db.store_breakpoint(2, 0, sv, get_line_num(sv, "        out <= in;"), condition="!rst")
    for i, name in enumerate(["clk", "rst", "in", "out"]):
        db.store_variable(i, "{0}.{1}".format("mod", name))
        db.store_generator_variable(name, 0, i)
        for i_ in range(3):
            db.store_context_variable(name, i_, i)


def write_out_db4(filename, sv):
    db = hgdb.DebugSymbolTable(filename)
    db.store_instance(0, "child")
    db.store_breakpoint(0, 0, sv, get_line_num(sv, "logic [3:0][1:0][15:0] a;"))
    db.store_breakpoint(1, 0, sv, get_line_num(sv, "logic [15:0] b[3:0][1:0];"))
    id_ = 0
    for i in range(4):
        for j in range(2):
            db.store_variable(id_, "a[{0}][{1}]".format(i, j))
            db.store_variable(id_ + 1, "b[{0}][{1}]".format(i, j))
            db.store_generator_variable("a[{0}][{1}]".format(i, j), 0, id_)
            db.store_generator_variable("b[{0}][{1}]".format(i, j), 0, id_ + 1)
            id_ += 1


def test_replay3(start_server, find_free_port, get_tools_vector_dir):
    vector_dir = get_tools_vector_dir()
    vcd_path = os.path.join(vector_dir, "waveform3.vcd")
    port = find_free_port()
    s = start_server(port, ("tools", "hgdb-replay", "hgdb-replay"), args=[vcd_path])
    if s is None:
        pytest.skip("hgdb-deplay not available")
    sv = os.path.join(vector_dir, "waveform3.sv")
    with tempfile.TemporaryDirectory() as tempdir:
        db = os.path.join(tempdir, "debug.db")
        write_out_db3(db, sv)

        async def test_logic():
            client = hgdb.client.HGDBClient("ws://localhost:{0}".format(port), db)
            await client.connect()
            time.sleep(0.1)
            await client.set_breakpoint(sv, get_line_num(sv, "        out <= in;"))
            await client.continue_()
            bp = await client.recv()
            assert bp["payload"]["time"] == 5
            await client.continue_()
            bp = await client.recv()
            assert bp["payload"]["time"] == 15
            assert bp["payload"]["instances"][0]["local"]["clk"] == "1"
            assert bp["payload"]["instances"][0]["local"]["in"] == "0x1"
            assert bp["payload"]["instances"][0]["local"]["out"] == "0x0"
            await client.continue_()
            bp = await client.recv()
            assert bp["payload"]["time"] == 25
            # go back in time. notice that step back will also visit un-inserted breakpoint
            await client.step_back()
            bp = await client.recv()
            assert bp["payload"]["time"] == 25
            await client.step_back()
            bp = await client.recv()
            assert bp["payload"]["time"] == 15

        asyncio.get_event_loop().run_until_complete(test_logic())

    s.kill()


def test_replay4(start_server, find_free_port, get_tools_vector_dir):
    vector_dir = get_tools_vector_dir()
    vcd_path = os.path.join(vector_dir, "waveform4.vcd")
    port = find_free_port()
    s = start_server(port, ("tools", "hgdb-replay", "hgdb-replay"), args=[vcd_path])
    if s is None:
        pytest.skip("hgdb-deplay not available")
    sv = os.path.join(vector_dir, "waveform4.sv")
    with tempfile.TemporaryDirectory() as tempdir:
        db = os.path.join(tempdir, "debug.db")
        write_out_db4(db, sv)

        async def test_logic():
            client = hgdb.client.HGDBClient("ws://localhost:{0}".format(port), db)
            await client.connect()
            time.sleep(0.1)
            await client.set_breakpoint(sv, get_line_num(sv, "logic [15:0] b[3:0][1:0];"))
            await client.continue_()
            bp = await client.recv()
            assert bp["payload"]["instances"][0]["generator"]["a[0][0]"] == "0x0001"
            await client.continue_()
            bp = await client.recv()
            assert bp["payload"]["instances"][0]["generator"]["a[0][0]"] == "0x000B"

        asyncio.get_event_loop().run_until_complete(test_logic())

    s.kill()


if __name__ == "__main__":
    import sys

    sys.path.append(os.getcwd())
    from conftest import start_server_fn, find_free_port_fn, get_tools_vector_dir_fn
    test_replay4(start_server_fn, find_free_port_fn, get_tools_vector_dir_fn)
