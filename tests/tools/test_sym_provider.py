import asyncio
import os
import time
from typing import Union, List, Dict, Tuple

import hgdb
import pytest
from hgdb import (SymbolTableProvider, VariableSymbol, GeneratorVariableSymbol, ContextVariableSymbol,
                  BreakpointSymbol)


# this is tested inside tools because we need to waveforms to emulate the simulation


class DummySymbolProvider(SymbolTableProvider):
    def __init__(self, port=10000):
        super(DummySymbolProvider, self).__init__(port=port)
        # only one breakpoint
        self.breakpoint = BreakpointSymbol(id=0, instance_id=0, filename="waveform1.sv", line_num=6, column_num=0,
                                           condition="", trigger="")
        self.variable = VariableSymbol(id=0, value="b", is_rtl=True)
        self.context_var = ContextVariableSymbol(name="b", breakpoint_id=0, variable_id=0)
        self.gen_var = GeneratorVariableSymbol(name="b", instance_id=0, variable_id=0, annotation="")
        self.instance_name = "child"

    def get_breakpoint(self, breakpoint_id: int) -> Union[None, BreakpointSymbol]:
        if breakpoint_id == 0:
            return self.breakpoint
        else:
            return None

    def get_breakpoints(self, filename: str, line_num: int, column_num: int) -> List[BreakpointSymbol]:
        if column_num == 0:
            if line_num == 0 or (line_num == self.breakpoint.line_num and filename == self.breakpoint.filename):
                return [self.breakpoint]
        return []

    def get_instance_name(self, instance_id: int) -> Union[None, str]:
        if instance_id == 0:
            return self.instance_name
        else:
            return None

    def get_instance_id_by_name(self, instance_name: int) -> Union[None, int]:
        if instance_name == self.instance_name:
            return 0
        else:
            return None

    def get_instance_id_by_bp(self, breakpoint_id: int) -> Union[None, int]:
        if breakpoint_id == self.breakpoint.id:
            return 0
        else:
            return None

    def get_context_variable(self, breakpoint_id: int) -> List[Tuple[ContextVariableSymbol, VariableSymbol]]:
        if breakpoint_id == self.breakpoint.id:
            return [(self.context_var, self.variable)]
        return []

    def get_generator_variable(self, instance_id: int) -> List[Tuple[GeneratorVariableSymbol, VariableSymbol]]:
        if instance_id == 0:
            return [(self.gen_var, self.variable)]
        return []

    def get_instance_names(self) -> List[str]:
        return [self.instance_name]

    def get_context_static_values(self, breakpoint_id: int) -> Dict[str, int]:
        return {}

    def get_annotation_values(self, name: str) -> List[str]:
        # optional. used when you have unconventional clock names
        if name == "clock":
            return ["top.clk"]
        return []


def test_symbol_table_provider_ws(start_server, find_free_port, get_tools_vector_dir):
    symbol_port = find_free_port()
    sym_provider = DummySymbolProvider(symbol_port)
    # set blocking to false so that we can run other stuff
    sym_provider.run(blocking=False)

    vector_dir = get_tools_vector_dir()
    vcd_path = os.path.join(vector_dir, "waveform1.vcd")
    port = find_free_port()
    s = start_server(port, ("tools", "hgdb-replay", "hgdb-replay"), args=[vcd_path], use_plus_arg=False)
    if s is None:
        pytest.skip("hgdb-deplay not available")

    async def test_logic():
        client = hgdb.client.HGDBClient("ws://localhost:{0}".format(port), "ws://localhost:{0}".format(symbol_port))
        await client.connect()
        time.sleep(0.1)
        await client.set_breakpoint("waveform1.sv", 6)
        await client.continue_()
        await client.recv()
        await client.continue_()
        bp = await client.recv()
        inst_bp = bp["payload"]["instances"][0]
        assert inst_bp["local"]["b"] == "0x1"
        assert inst_bp["generator"]["b"] == "0x1"

    asyncio.get_event_loop_policy().get_event_loop().run_until_complete(test_logic())
    sym_provider.stop()
    s.kill()


if __name__ == "__main__":
    import sys

    sys.path.append(os.getcwd())
    from conftest import start_server_fn, find_free_port_fn, get_tools_vector_dir_fn

    test_symbol_table_provider_ws(start_server_fn, find_free_port_fn, get_tools_vector_dir_fn)
