from hgdb import HGDBClient, DebugSymbolTable
from generators.util import XceliumTester, VerilatorTester, IVerilogTester, QuestaTester, get_uri, get_root
import tempfile
import os
import asyncio
import sys
import pytest


vector_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vectors")


def set_value_db(db_filename):
    db = DebugSymbolTable(db_filename)
    db.store_instance(0, "mod")
    for i, name in enumerate(["clk", "rst", "in", "out", "data"]):
        db.store_variable(i, name)
        db.store_generator_variable(name, 0, i)
    db.store_breakpoint(0, 0, "test.sv", 1, condition="rst == 0")


@pytest.mark.parametrize("simulator", [VerilatorTester, XceliumTester])
def test_set_value(find_free_port, simulator):
    if not simulator.available():
        pytest.skip("{0} not available".format(simulator.__name__))
    with tempfile.TemporaryDirectory() as temp:
        db_filename = os.path.abspath(os.path.join(temp, "debug.db"))
        set_value_db(db_filename)
        mod_filename = os.path.join(vector_dir, "test_set_value.sv")
        tb_filename = os.path.join(vector_dir,
                                   "test_set_value_tb.cc" if simulator == VerilatorTester else "test_set_value_tb.sv")
        filenames = [mod_filename, tb_filename]
        with simulator(*filenames, cwd=temp) as tester:
            port = find_free_port()
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True)
            uri = get_uri(port)

            async def test_logic():
                client = HGDBClient(uri, db_filename)
                await client.connect()
                # set breakpoint
                await client.set_breakpoint("test.sv", 1)
                await client.continue_()
                await client.recv()
                # change value
                await client.set_value("data", 8, instance_id=0)
                await client.continue_()
                bp = await client.recv()
                assert bp["payload"]["instances"][0]["generator"]["out"] == "8"
                await client.continue_()
                bp = await client.recv()
                assert bp["payload"]["instances"][0]["generator"]["out"] == "0"

            asyncio.get_event_loop().run_until_complete(test_logic())


def create_iverilog_db(db_filename):
    db = DebugSymbolTable(db_filename)
    db.store_instance(0, "top")
    for i, name in enumerate(["clk", "rst", "a", "b"]):
        db.store_variable(i, name)
        db.store_generator_variable(name, 0, i)
    db.store_breakpoint(0, 0, "test_iverilog.v", 1, condition="rst == 0")


@pytest.mark.parametrize("simulator", [IVerilogTester, QuestaTester])
def test_other_simulators(find_free_port, simulator):
    if not simulator.available():
        pytest.skip("{0} not available".format(simulator.__name__))
    with tempfile.TemporaryDirectory() as temp:
        tb_filename = os.path.join(vector_dir, "test_iverilog.v")
        db_filename = os.path.join(temp, "debug.db")
        create_iverilog_db(db_filename)
        with simulator(tb_filename, cwd=temp, top_name="top") as tester:
            port = find_free_port()
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True)
            uri = get_uri(port)

            async def test_logic():
                client = HGDBClient(uri, db_filename)
                await client.connect()
                await client.set_breakpoint("test_iverilog.v", 1)
                await client.continue_()
                bp = await client.recv()
                assert bp["payload"]["time"] == 10
                assert bp["payload"]["instances"][0]["generator"]["clk"] == "1"

            asyncio.get_event_loop().run_until_complete(test_logic())


if __name__ == "__main__":
    sys.path.append(get_root())
    from conftest import find_free_port_fn

    test_other_simulators(find_free_port_fn, QuestaTester)
