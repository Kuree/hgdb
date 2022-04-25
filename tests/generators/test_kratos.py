import asyncio
import os
import sys
import tempfile

import hgdb
import pytest

from .util import (VerilatorTester, get_vector_file, get_uri, get_root, get_line_num,
                   XceliumTester, VCSTester)

py_filename = os.path.abspath(__file__)


@pytest.mark.parametrize("simulator", [VerilatorTester, XceliumTester, VCSTester])
def test_kratos_single_instance(find_free_port, simulator):
    if not simulator.available():
        pytest.skip(simulator.__name__ + " not available")
    from kratos import Generator, clog2, always_ff, always_comb, verilog, posedge
    input_width = 16
    buffer_size = 4
    mod = Generator("mod", debug=True)
    clk = mod.clock("clk")
    rst = mod.reset("rst")
    in_ = mod.input("in", input_width)
    out = mod.output("out", input_width)
    counter = mod.var("count", clog2(buffer_size))
    # notice that verilator does not support probing on packed array!!!
    data = mod.var("data", input_width, size=buffer_size)

    @always_comb
    def sum_data():
        out = 0
        for i in range(buffer_size):
            out = out + data[i]

    @always_ff((posedge, clk), (posedge, rst))
    def buffer_logic():
        if rst:
            for i in range(buffer_size):
                data[i] = 0
            counter = 0
        else:
            data[counter] = in_
            counter += 1

    mod.add_always(sum_data, ssa_transform=True)
    mod.add_always(buffer_logic)
    py_line_num = get_line_num(py_filename, "            out = out + data[i]")

    with tempfile.TemporaryDirectory() as temp:
        temp = os.path.abspath(temp)
        db_filename = os.path.join(temp, "debug.db")
        sv_filename = os.path.join(temp, "mod.sv")
        verilog(mod, filename=sv_filename, insert_debug_info=True,
                debug_db_filename=db_filename, ssa_transform=True,
                insert_verilator_info=True)
        # run verilator
        tb = "test_kratos.cc" if simulator == VerilatorTester else "test_kratos.sv"
        main_file = get_vector_file(tb)
        with simulator(sv_filename, main_file, cwd=temp) as tester:
            port = find_free_port()
            uri = get_uri(port)
            # set the port
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True)

            async def client_logic():
                client = hgdb.HGDBClient(uri, db_filename)
                await client.connect()
                # set breakpoint
                await client.set_breakpoint(py_filename, py_line_num)
                await client.continue_()
                for i in range(4):
                    bp = await client.recv()
                    assert bp["payload"]["instances"][0]["local"]["i"] == str(i)
                    if simulator == XceliumTester:
                        assert bp["payload"]["time"] == 10
                    await client.continue_()

                for i in range(4):
                    # the first breakpoint, out is not calculated yet
                    # so it should be 0
                    # after that, it should be 1
                    bp = await client.recv()
                    if simulator == XceliumTester:
                        assert bp["payload"]["time"] == 30
                    if i == 0:
                        assert bp["payload"]["instances"][0]["local"]["out"] == "0"
                    else:
                        assert bp["payload"]["instances"][0]["local"]["out"] == "1"
                    await client.continue_()

                # remove the breakpoint and set a conditional breakpoint
                # discard the current breakpoint information
                await client.recv()
                # remove the current breakpoint
                await client.remove_breakpoint(py_filename, py_line_num)
                await client.set_breakpoint(py_filename, py_line_num, cond="out == 6 && i == 3")
                await client.continue_()
                bp = await client.recv()
                assert bp["payload"]["instances"][0]["local"]["out"] == "6"
                assert bp["payload"]["instances"][0]["local"]["i"] == "3"
                assert bp["payload"]["instances"][0]["local"]["data.2"] == "3"

            asyncio.get_event_loop_policy().get_event_loop().run_until_complete(client_logic())


@pytest.mark.parametrize("simulator", [VerilatorTester, XceliumTester])
def test_kratos_multiple_instances(find_free_port, simulator):
    if not simulator.available():
        pytest.skip(simulator.__name__ + " not available")
    # we re-use the same test bench with different logic
    from kratos import Generator, always_ff, verilog, posedge
    input_width = 16
    mod = Generator("mod", debug=True)
    child1 = Generator("child", debug=True)
    child2 = Generator("child", debug=True)
    gens = [mod, child1, child2]
    for g in gens:
        g.clock("clk")
        g.reset("rst")
        g.input("in", input_width)
        g.output("out", input_width)

    for idx, g in enumerate([child1, child2]):
        clk = g.ports.clk
        rst = g.ports.rst
        in_ = g.ports["in"]
        out = g.ports.out

        @always_ff((posedge, clk), (posedge, rst))
        def logic():
            if rst:
                out = 0
            else:
                out = in_

        g.add_always(logic)
        mod.add_child(f"child_{idx}", g, clk=mod.ports.clk, rst=mod.ports.rst)
        mod.wire(mod.ports["in"], in_)
    mod.wire(mod.ports.out, child1.ports.out + child2.ports.out)
    with tempfile.TemporaryDirectory() as temp:
        temp = os.path.abspath(temp)
        db_filename = os.path.join(temp, "debug.db")
        sv_filename = os.path.join(temp, "mod.sv")
        verilog(mod, filename=sv_filename, insert_debug_info=True,
                debug_db_filename=db_filename, insert_verilator_info=True)
        # run verilator
        tb = "test_kratos.cc" if simulator == VerilatorTester else "test_kratos.sv"
        main_file = get_vector_file(tb)
        py_line_num = get_line_num(py_filename, "                out = in_")
        with simulator(sv_filename, main_file, cwd=temp) as tester:
            port = find_free_port()
            uri = get_uri(port)
            # set the port
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True)

            async def client_logic():
                client = hgdb.HGDBClient(uri, db_filename)
                await client.connect()
                # set breakpoint
                await client.set_breakpoint(py_filename, py_line_num)
                await client.continue_()
                bp = await client.recv()
                # bp should have two instances
                assert len(bp["payload"]["instances"]) == 2
                assert len({bp["payload"]["instances"][0]["instance_id"],
                            bp["payload"]["instances"][1]["instance_id"]}) == 2

            asyncio.get_event_loop_policy().get_event_loop().run_until_complete(client_logic())


@pytest.mark.parametrize("simulator", [VerilatorTester, XceliumTester])
def test_kratos_data_breakpoint(find_free_port, simulator):
    if not simulator.available():
        pytest.skip(simulator.__name__ + " not available")
    # we re-use the same test bench with different logic
    from kratos import Generator, always_ff, verilog, posedge
    width = 16
    size = 4
    mod = Generator("mod", debug=True)
    clk = mod.clock("clk")
    rst = mod.reset("rst")
    addr = mod.input("addr", 2)
    in_ = mod.input("in", width)
    array = mod.var("array", width, size=size)

    @always_ff((posedge, clk), (posedge, rst))
    def buffer_logic():
        if rst:
            for i in range(size):
                array[i] = 0
        else:
            array[addr] = in_

    mod.add_always(buffer_logic)

    with tempfile.TemporaryDirectory() as temp:
        temp = os.path.abspath(temp)
        db_filename = os.path.join(temp, "debug.db")
        sv_filename = os.path.join(temp, "mod.sv")
        verilog(mod, filename=sv_filename, insert_debug_info=True,
                debug_db_filename=db_filename, insert_verilator_info=True)
        # run verilator
        tb = "test_kratos_data.cc" if simulator == VerilatorTester else "test_kratos_data.sv"
        main_file = get_vector_file(tb)
        py_line_num = get_line_num(py_filename, "            array[addr] = in_")
        with simulator(sv_filename, main_file, cwd=temp) as tester:
            port = find_free_port()
            uri = get_uri(port)
            # set the port
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True, debug=True)

            async def client_logic():
                client = hgdb.HGDBClient(uri, db_filename)
                await client.connect()
                # set data breakpoint
                bp_info = await client.request_breakpoint_location(py_filename, py_line_num)
                bp_id = bp_info["payload"][0]["id"]
                await client.set_data_breakpoint(bp_id, "array[3]")
                await client.continue_()
                bp = await client.recv()
                assert bp["payload"]["line_num"] == py_line_num
                # addr should be 3 and array[3] hasn't been set yet
                assert bp["payload"]["instances"][0]["generator"]["addr"] == "3"
                assert bp["payload"]["instances"][0]["generator"]["array.3"] == "0"
                # two has been set
                assert bp["payload"]["instances"][0]["generator"]["array.2"] == "3"

                # go to the end
                await client.continue_()

            asyncio.get_event_loop_policy().get_event_loop().run_until_complete(client_logic())


if __name__ == "__main__":
    sys.path.append(get_root())
    from conftest import find_free_port_fn

    test_kratos_data_breakpoint(find_free_port_fn, XceliumTester)
