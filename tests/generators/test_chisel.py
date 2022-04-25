import os
import sys
import tempfile
import subprocess

import pytest
import hgdb
import asyncio

from .util import (get_vector_file, get_root, XceliumTester, get_uri, VerilatorTester)


@pytest.mark.parametrize("simulator", [VerilatorTester, XceliumTester])
def test_chisel_firrtl(find_free_port, simulator):
    if not simulator.available():
        pytest.skip(simulator.__name__ + " not available")
    with tempfile.TemporaryDirectory() as temp:
        temp = os.path.abspath(temp)
        db_filename = os.path.join(temp, "debug.db")
        fir_filename = get_vector_file("test_chisel_firrtl.toml")
        rtl_filename = get_vector_file("test_chisel_firrtl.v")
        ext = ".sv" if simulator == XceliumTester else ".cc"
        tb_filename = get_vector_file("test_chisel_firrtl_tb" + ext)
        assert os.path.isfile(fir_filename)
        # convert it to the hgdb
        subprocess.check_call(["toml2hgdb", fir_filename, db_filename])
        # call the simulator directly
        with simulator(rtl_filename, tb_filename, cwd=temp) as tester:
            port = find_free_port()
            uri = get_uri(port)
            # set the port
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True)

            async def client_logic():
                client = hgdb.HGDBClient(uri, db_filename)
                await client.connect()
                # set on if statement
                #
                # 15 when (doDispense) {
                # 16   value := 0.U // No change given                 <- we pause here. value should not be 0
                # 17 } .otherwise {
                # 18   value := value + incValue
                # 19  }
                await client.set_breakpoint("ImplicitStateVendingMachine.scala", 16, 11)
                await client.continue_()
                bp = await client.recv()
                assert bp["payload"]["instances"][0]["generator"]["value"] == "4"

            asyncio.get_event_loop_policy().get_event_loop().run_until_complete(client_logic())


@pytest.mark.parametrize("simulator", [VerilatorTester, XceliumTester])
def test_chisel_firrtl_data(find_free_port, simulator):
    if not simulator.available():
        pytest.skip(simulator.__name__ + " not available")
    with tempfile.TemporaryDirectory() as temp:
        temp = os.path.abspath(temp)
        db_filename = os.path.join(temp, "debug.db")
        fir_filename = get_vector_file("test_chisel_firrtl.toml")
        rtl_filename = get_vector_file("test_chisel_firrtl.v")
        ext = ".sv" if simulator == XceliumTester else ".cc"
        tb_filename = get_vector_file("test_chisel_firrtl_tb" + ext)
        assert os.path.isfile(fir_filename)
        # convert it to the hgdb
        subprocess.check_call(["toml2hgdb", fir_filename, db_filename])
        # call the simulator directly
        with simulator(rtl_filename, tb_filename, cwd=temp) as tester:
            port = find_free_port()
            uri = get_uri(port)
            # set the port
            tester.run(blocking=False, DEBUG_PORT=port, DEBUG_LOG=True)

            async def client_logic():
                client = hgdb.HGDBClient(uri, db_filename)
                await client.connect()
                # set on if statement
                #
                # 15 when (doDispense) {
                # 16   value := 0.U // No change given                 <- should be paused
                # 17 } .otherwise {
                # 18   value := value + incValue                       <- should be paused
                # 19  }
                bp_info = await client.request_breakpoint_location("ImplicitStateVendingMachine.scala", 16, 11)
                bp_id = bp_info["payload"][0]["id"]
                await client.set_data_breakpoint(bp_id, "value")
                await client.continue_()
                lns = set()
                for i in range(6):
                    bp = await client.recv()
                    lns.add(bp["payload"]["line_num"])
                    await client.continue_()

                assert 18 in lns
                assert 16 in lns

            asyncio.get_event_loop_policy().get_event_loop().run_until_complete(client_logic())


if __name__ == "__main__":
    sys.path.append(get_root())
    from conftest import find_free_port_fn

    test_chisel_firrtl_data(find_free_port_fn, VerilatorTester)
