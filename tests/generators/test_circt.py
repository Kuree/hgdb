import os
import sys
import tempfile

import pytest
import hgdb
import asyncio

from .util import (get_vector_file, get_root, XceliumTester, get_uri, VerilatorTester)


@pytest.mark.parametrize("simulator", [VerilatorTester])
def test_circt(find_free_port, simulator):
    if not simulator.available():
        pytest.skip(simulator.__name__ + " not available")
    with tempfile.TemporaryDirectory() as temp:
        temp = os.path.abspath(temp)
        db_filename = get_vector_file("test_circt.json")
        rtl_filename = get_vector_file("test_circt.sv")
        ext = ".sv" if simulator == XceliumTester else ".cc"
        tb_filename = get_vector_file("test_circt_tb" + ext)
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
                # 16   value := 0.U // No change given                <- we pause here. value should not be 0
                # 17 } .otherwise {
                # 18   value := value + incValue
                # 19  }
                # we are supposed to break at 16:11 (the same as the chisel firrtl test).
                # however, the location tracking seems to be messed up in circt
                # if you look at generated RTL, it's also labeled as ImplicitStateVendingMachine.scala:18:{11,20}
                # see circt issue https://github.com/llvm/circt/issues/2733
                await client.set_breakpoint("ImplicitStateVendingMachine.scala", 18, 11)
                await client.continue_()
                bp = await client.recv()
                assert bp["payload"]["instances"][0]["generator"]["value"] == "4"

            asyncio.get_event_loop().run_until_complete(client_logic())


if __name__ == "__main__":
    sys.path.append(get_root())
    from conftest import find_free_port_fn

    test_circt(find_free_port_fn, VerilatorTester)
