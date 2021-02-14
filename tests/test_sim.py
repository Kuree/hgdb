from hgdb import HGDBClient, DebugSymbolTable
from generators.util import XceliumTester, get_uri, get_root
import tempfile
import os
import asyncio
import sys


def set_value_db(db_filename):
    db = DebugSymbolTable(db_filename)
    db.store_instance(0, "mod")
    for i, name in enumerate(["clk", "rst", "in", "out", "data"]):
        db.store_variable(i, name)
        db.store_generator_variable(name, 0, i)
    db.store_breakpoint(0, 0, "test.sv", 1, condition="rst == 0")


def test_set_value(find_free_port):
    with tempfile.TemporaryDirectory() as temp:
        temp = "temp"
        db_filename = os.path.abspath(os.path.join(temp, "debug.db"))
        set_value_db(db_filename)
        sv_filename = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vectors", "test_set_value.sv")
        with XceliumTester(sv_filename, cwd=temp) as tester:
            port = find_free_port()
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True)
            uri = get_uri(port)

            async def test_logic():
                client = HGDBClient(uri, db_filename)
                await client.connect()
                # set breakpoint
                await client.set_breakpoint("test.sv", 1)
                await client.continue_()
                bp = await client.recv()
                print(bp)
                # change value
                await client.set_value("data", 8, instance_id=0)
                await client.continue_()
                bp = await client.recv()
                print(bp)
                await client.continue_()
                bp = await client.recv()
                print(bp)

            asyncio.get_event_loop().run_until_complete(test_logic())


if __name__ == "__main__":
    sys.path.append(get_root())
    from conftest import find_free_port_fn

    test_set_value(find_free_port_fn)
