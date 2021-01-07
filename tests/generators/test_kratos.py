from util import VerilatorTester, get_vector_file, get_uri, get_root
import tempfile
import os
import sys
import hgdb
import asyncio
import pytest


@pytest.mark.skip(reason="Not working yet")
def test_kratos_verilator(find_free_port):
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
            counter = 0
        else:
            data[counter] = in_
            counter += 1

    mod.add_always(sum_data, ssa_transform=True)
    mod.add_always(buffer_logic)

    with tempfile.TemporaryDirectory() as temp:
        temp = "temp"
        db_filename = os.path.join(temp, "debug.db")
        sv_filename = os.path.join(temp, "mod.sv")
        verilog(mod, filename=sv_filename, insert_debug_info=True,
                debug_db_filename=db_filename, ssa_transform=True,
                insert_verilator_info=True)
        # run verilator
        main_file = get_vector_file("test_kratos.cc")
        with VerilatorTester(sv_filename, main_file, cwd=temp) as tester:
            port = find_free_port()
            uri = get_uri(port)
            # set the port
            tester.run(blocking=False, DEBUG_PORT=port)

            async def client_logic():
                client = hgdb.HGDBClient(uri, db_filename)
                await client.connect()

            asyncio.get_event_loop().run_until_complete(client_logic())


if __name__ == "__main__":
    sys.path.append(get_root())
    from conftest import find_free_port_fn
    test_kratos_verilator(find_free_port_fn)
