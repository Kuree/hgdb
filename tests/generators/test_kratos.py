from util import (VerilatorTester, get_vector_file, get_uri, get_root, get_line_num,
                  XceliumTester)
import tempfile
import os
import sys
import hgdb
import asyncio
import pytest
import time


@pytest.mark.parametrize("simulator", [VerilatorTester, XceliumTester])
def test_kratos(find_free_port, simulator):
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
    data = mod.var("data", input_width, size=buffer_size, packed=True)

    @always_comb
    def sum_data():
        out = 0
        for i in range(buffer_size):
            out = out + data[i]

    @always_ff((posedge, clk), (posedge, rst))
    def buffer_logic():
        if rst:
            data = 0
            counter = 0
        else:
            data[counter] = in_
            counter += 1

    mod.add_always(sum_data, ssa_transform=True)
    mod.add_always(buffer_logic)

    py_filename = os.path.abspath(__file__)
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
                    assert bp["payload"]["values"]["local"]["i"] == str(i)
                    await client.continue_()

                for i in range(4):
                    # the first breakpoint, out is not calculated yet
                    # so it should be 0
                    # after that, it should be 1
                    bp = await client.recv()
                    if i == 0:
                        assert bp["payload"]["values"]["local"]["out"] == "0"
                    else:
                        assert bp["payload"]["values"]["local"]["out"] == "1"
                    await client.continue_()

                # remove the breakpoint and set a conditional breakpoint
                # discard the current breakpoint information
                await client.recv()
                # remove the current breakpoint
                await client.remove_breakpoint(py_filename, py_line_num)
                await client.set_breakpoint(py_filename, py_line_num, cond="out == 6 and i == 3")
                await client.continue_()
                bp = await client.recv()
                assert bp["payload"]["values"]["local"]["out"] == "6"
                assert bp["payload"]["values"]["local"]["i"] == "3"

            asyncio.get_event_loop().run_until_complete(client_logic())


if __name__ == "__main__":
    sys.path.append(get_root())
    from conftest import find_free_port_fn
    test_kratos(find_free_port_fn, XceliumTester)
