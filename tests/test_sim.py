import asyncio
import os
import tempfile

import pytest
import sys
from hgdb import HGDBClient, DebugSymbolTable

from generators.util import (XceliumTester, VCSTester, VerilatorTester, IVerilogTester, QuestaTester, get_uri, get_root)

vector_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "vectors")


def set_value_db(db_filename):
    db = DebugSymbolTable(db_filename)
    db.store_instance(0, "mod")
    for i, name in enumerate(["clk", "rst", "in", "out", "data"]):
        db.store_variable(i, name)
        db.store_generator_variable(name, 0, i)
    db.store_breakpoint(0, 0, "test.sv", 1, condition="rst == 0")


@pytest.mark.parametrize("simulator", [VerilatorTester, XceliumTester, VCSTester])
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
                # Notice that each simulator has different semantics after
                # a value is changed. Verilator and Xcelium seems to agree with each other
                # but VCS produces different result

            asyncio.get_event_loop().run_until_complete(test_logic())


def create_iverilog_db(db_filename):
    db = DebugSymbolTable(db_filename)
    db.store_instance(0, "top")
    for i, name in enumerate(["clk", "rst", "a", "b"]):
        db.store_variable(i, name)
        db.store_generator_variable(name, 0, i)
    db.store_breakpoint(0, 0, "test_simulator.v", 1, condition="rst == 0")


@pytest.mark.parametrize("simulator", [IVerilogTester, QuestaTester])
def test_other_simulators(find_free_port, simulator):
    if not simulator.available():
        pytest.skip("{0} not available".format(simulator.__name__))
    with tempfile.TemporaryDirectory() as temp:
        tb_filename = os.path.join(vector_dir, "test_simulator.v")
        db_filename = os.path.join(temp, "debug.db")
        create_iverilog_db(db_filename)
        with simulator(tb_filename, cwd=temp, top_name="top") as tester:
            port = find_free_port()
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True)
            uri = get_uri(port)

            async def test_logic():
                client = HGDBClient(uri, db_filename)
                await client.connect()
                await client.set_breakpoint("test_simulator.v", 1)
                await client.continue_()
                bp = await client.recv()
                assert bp["payload"]["time"] == 10
                assert bp["payload"]["instances"][0]["generator"]["clk"] == "1"

            asyncio.get_event_loop().run_until_complete(test_logic())


@pytest.mark.parametrize("simulator", [VerilatorTester, XceliumTester, VCSTester])
def test_complex_construct(find_free_port, simulator):
    if not simulator.available():
        pytest.skip("{0} not available".format(simulator.__name__))
    with tempfile.TemporaryDirectory() as temp:
        db_filename = os.path.join(vector_dir, "complex_db.json")
        tb_filename = os.path.join(vector_dir, "test_complex.cc") if simulator == VerilatorTester else os.path.join(
            vector_dir, "test_complex_tb.sv")
        mod_filename = os.path.join(vector_dir, "test_complex.sv")
        with simulator(tb_filename, mod_filename, cwd=temp, top_name="top") as tester:
            port = find_free_port()
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True)
            uri = get_uri(port)

            async def test_logic():
                client = HGDBClient(uri, db_filename)
                await client.connect()
                await client.set_breakpoint("hgdb.cc", 1)
                await client.continue_()
                bp = await client.recv()
                gen_vars = bp["payload"]["instances"][0]["generator"]
                if simulator == VCSTester:
                    assert "b.a" in gen_vars and gen_vars["b.a"] != "ERROR"
                    assert "b.b.15" in gen_vars and gen_vars["b.b.15"] != "ERROR"
                else:
                    assert "a.a" in gen_vars and gen_vars["a.a"] != "ERROR"
                    assert "a.c.0" in gen_vars and gen_vars["a.c.0"] != "ERROR"
                    assert "a.c.1" in gen_vars and gen_vars["a.c.1"] != "ERROR"
                    # notice that verilator doesn't show the complete values
                    if simulator != VerilatorTester:
                        assert "a.b.15" in gen_vars and gen_vars["a.b.15"] != "ERROR"
                        assert "b.a" in gen_vars and gen_vars["b.a"] != "ERROR"
                        assert "b.b.15" in gen_vars and gen_vars["b.b.15"] != "ERROR"

            asyncio.get_event_loop().run_until_complete(test_logic())


if __name__ == "__main__":
    sys.path.append(get_root())
    from conftest import find_free_port_fn

    test_complex_construct(find_free_port_fn, VCSTester)
