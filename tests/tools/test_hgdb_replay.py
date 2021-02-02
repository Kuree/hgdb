import os
import pytest
import hgdb
import tempfile
import asyncio
import time


def get_root():
    dirname = os.path.abspath(__file__)
    for i in range(3):
        dirname = os.path.dirname(dirname)
    return dirname


def get_line_num(filename, pattern):
    with open(filename) as f:
        lines = f.readlines()
    lines = [l.rstrip() for l in lines]
    return lines.index(pattern) + 1


def write_out_db(filename, sv):
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


def test_replay(start_server, find_free_port):
    root = get_root()
    vcd_path = os.path.join(root, "tests", "tools", "vectors", "waveform3.vcd")
    port = find_free_port()
    s = start_server(port, ("tools", "hgdb-replay", "hgdb-replay"), args=[vcd_path])
    if s is None:
        pytest.skip("hgdb-deplay not available")
    sv = os.path.join(get_root(), "tests", "tools", "vectors", "waveform3.sv")
    with tempfile.TemporaryDirectory() as tempdir:
        db = os.path.join(tempdir, "debug.db")
        write_out_db(db, sv)

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
            assert bp["payload"]["instances"][0]["local"]["in"] == "1"
            assert bp["payload"]["instances"][0]["local"]["out"] == "0"

        asyncio.get_event_loop().run_until_complete(test_logic())

    s.kill()


if __name__ == "__main__":
    import sys

    sys.path.append(get_root())
    from conftest import start_server_fn, find_free_port_fn
    test_replay(start_server_fn, find_free_port_fn)
