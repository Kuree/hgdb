import abc
import asyncio
import collections
import json
import time
import multiprocessing
from typing import List, Tuple, Dict, Union

import websockets

VariableSymbol = collections.namedtuple("VariableSymbol", ["id", "value", "is_rtl"])
GeneratorVariableSymbol = collections.namedtuple("GeneratorVariableSymbol", ["name", "instance_id", "variable_id",
                                                                             "annotation"])
ContextVariableSymbol = collections.namedtuple("ContextVariableSymbol", ["name", "breakpoint_id", "variable_id"])
BreakpointSymbol = collections.namedtuple("BreakpointSymbol",
                                          ["id", "instance_id", "filename", "line_num", "column_num", "condition",
                                           "trigger"])


def _to_dict(obj):
    if hasattr(obj, "_asdict"):
        return obj._asdict()
    elif isinstance(obj, list) or isinstance(obj, tuple):
        return [_to_dict(o) for o in obj]
    else:
        return obj


class SymbolTableProvider:
    """Pure python implementation of the symbol table provider.
    This is the reference implementation"""

    def __init__(self, hostname="127.0.0.1", port=8889):
        self.hostname = hostname
        self.port = port

        self.loop = asyncio.new_event_loop()
        self.__stop = self.loop.create_future()
        self.__thread: Union[None, multiprocessing.Process] = None

    async def _on_message(self, websocket, _):
        async for message in websocket:
            try:
                data = json.loads(message)
            except json.JSONDecodeError:
                await self.__send_resp(websocket, None)
                continue
            # decode everything
            # we assume the debugger client is implemented properly
            payload = data["payload"]
            req_type = payload["type"]
            resp = None
            if req_type == "get_breakpoint":
                resp = self.get_breakpoint(payload["breakpoint_id"])
            elif req_type == "get_breakpoints":
                resp = self.get_breakpoints(payload["filename"], payload["line_num"], payload["col_num"])
            elif req_type == "get_instance_name":
                resp = self.get_instance_name(payload["instance_id"])
            elif req_type == "get_instance_id":
                if "instance_name" in payload:
                    resp = self.get_instance_id_by_name(payload["instance_name"])
                else:
                    resp = self.get_instance_id_by_bp(payload["breakpoint_id"])
            elif req_type == "get_context_variables":
                resp = self.get_context_variable(payload["breakpoint_id"])
            elif req_type == "get_generator_variables":
                resp = self.get_generator_variable(payload["instance_id"])
            elif req_type == "get_instance_names":
                resp = self.get_instance_names()
            elif req_type == "get_annotation_values":
                resp = self.get_annotation_values(payload["name"])
            elif req_type == "get_all_array_names":
                resp = self.get_all_array_names()
            elif req_type == "execution_bp_orders":
                resp = self.execution_bp_orders()

            await self.__send_resp(websocket, resp)

    @staticmethod
    async def __send_resp(ws, obj):
        if obj is None:
            res = {}
        else:
            res = {"result": _to_dict(obj)}
        await ws.send(json.dumps(res))

    async def _main(self):
        async with websockets.serve(self._on_message, self.hostname, self.port):
            await self.__stop

    def run(self, blocking=True):
        if blocking:
            self.loop.run_until_complete(self._main())
        else:
            def run():
                self.loop.run_until_complete(self._main())
            self.__thread = multiprocessing.Process(target=run)
            self.__thread.start()

    def stop(self):
        self.__stop.cancel()
        self.loop.stop()
        if self.__thread:
            self.__thread.kill()

    # abstract functions
    @abc.abstractmethod
    def get_breakpoint(self, breakpoint_id: int) -> Union[None, BreakpointSymbol]:
        pass

    @abc.abstractmethod
    def get_breakpoints(self, filename: str, line_num: int, column_num: int) -> List[BreakpointSymbol]:
        pass

    @abc.abstractmethod
    def get_instance_name(self, instance_id: int) -> Union[None, str]:
        pass

    @abc.abstractmethod
    def get_instance_id_by_name(self, instance_name: int) -> Union[None, int]:
        pass

    @abc.abstractmethod
    def get_instance_id_by_bp(self, breakpoint_id: int) -> Union[None, int]:
        pass

    @abc.abstractmethod
    def get_context_variable(self, breakpoint_id: int) -> List[Tuple[ContextVariableSymbol, VariableSymbol]]:
        pass

    @abc.abstractmethod
    def get_generator_variable(self, instance_id: int) -> List[Tuple[GeneratorVariableSymbol, VariableSymbol]]:
        pass

    @abc.abstractmethod
    def get_instance_names(self) -> List[str]:
        pass

    def get_annotation_values(self, name: str) -> List[str]:
        # optional. used when you have unconventional clock names
        return []

    def get_all_array_names(self) -> List[str]:
        # optional. only used when replaying VCD, since VCD doesn't have array information
        return []

    def execution_bp_orders(self) -> List[int]:
        # optional. only if the breakpoint execution order is different from the lexical order
        return []
