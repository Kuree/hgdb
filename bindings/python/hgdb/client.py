import asyncio
import json
import time

import websockets


class HGDBClient:
    def __init__(self, uri, filename, src_mapping=None):
        self.filename = filename
        self.uri = uri
        self.ws = None
        self.src_mapping = src_mapping

    async def recv(self, timeout=0):
        try:
            if timeout == 0:
                return json.loads(await self.ws.recv())
            else:
                return json.loads(await asyncio.wait_for(self.ws.recv(), timeout))
        except (asyncio.exceptions.IncompleteReadError, websockets.exceptions.ConnectionClosedError):
            return None

    async def send(self, payload):
        await self.ws.send(json.dumps(payload))

    async def connect(self):
        start = time.time()
        while time.time() < start + 10:
            try:
                self.ws = await websockets.connect(self.uri)
                break
            except (ConnectionRefusedError, OSError):
                time.sleep(0.5)
        if self.filename is not None:
            payload = {"request": True, "type": "connection", "payload": {
                "db_filename": self.filename,
            }}
            if self.src_mapping is not None:
                payload["payload"]["path-mapping"] = self.src_mapping
            return await self.__send_check(payload, True)

    async def set_src_mapping(self, mapping):
        self.src_mapping = mapping
        payload = {"request": True, "type": "path-mapping", "payload": {
            "path-mapping": mapping,
        }}
        await self.__send_check(payload, True)

    async def __send_check(self, payload, check_error=False):
        await self.send(payload)
        res = await self.recv()
        if check_error:
            self.__check_status(res)
        return res

    async def set_breakpoint(self, filename, line_num, column_num=0, token="", cond="",
                             check_error=True):
        payload = {"request": True, "type": "breakpoint", "token": token,
                   "payload": {"filename": filename, "line_num": line_num, "column_num": column_num,
                               "action": "add"}}
        if len(cond) > 0:
            payload["payload"]["condition"] = cond
        return await self.__send_check(payload, check_error)

    async def set_breakpoint_id(self, bp_id, cond="", token="", check_error=True):
        payload = {"request": True, "type": "breakpoint-id", "token": token,
                   "payload": {"id": bp_id, "action": "add"}}
        if len(cond) > 0:
            payload["payload"]["condition"] = cond
        return await self.__send_check(payload, check_error)

    async def remove_breakpoint(self, filename, line_num, column_num=0, token="", check_error=True):
        payload = {"request": True, "type": "breakpoint", "token": token,
                   "payload": {"filename": filename, "line_num": line_num, "column_num": column_num,
                               "action": "remove"}}
        return await self.__send_check(payload, check_error)

    async def remove_breakpoint_id(self, bp_id, token="", check_error=True):
        payload = {"request": True, "type": "breakpoint-id", "token": token,
                   "payload": {"id": bp_id, "action": "remove"}}
        return await self.__send_check(payload, check_error)

    async def request_breakpoint_location(self, filename, line_num=None, column_num=None):
        payload = {"request": True, "type": "bp-location", "payload": {"filename": filename}}
        if line_num is not None:
            payload["payload"]["line_num"] = line_num
        if column_num is not None:
            payload["payload"]["column_num"] = column_num
        return await self.__send_check(payload)

    async def continue_(self):
        await self.__send_command("continue")

    async def stop(self):
        await self.__send_command("stop")

    async def step_over(self):
        await self.__send_command("step_over")

    async def __send_command(self, command_str):
        payload = {"request": True, "type": "command", "payload": {"command": command_str}}
        await self.send(payload)

    async def get_info(self, status_command="breakpoints", check_error=True):
        payload = {"request": True, "type": "debugger-info", "payload": {"command": status_command}}
        return await self.__send_check(payload, check_error)

    async def evaluate(self, scope, expression, check_error=True):
        payload = {"request": True, "type": "evaluation", "payload": {"scope": scope, "expression": expression}}
        return await self.__send_check(payload, check_error=check_error)

    async def change_option(self, check_error=True, **kwargs):
        payload = {"request": True, "type": "option-change", "payload": {}}
        for name, value in kwargs.items():
            payload["payload"][name] = value
        return await self.__send_check(payload, check_error=check_error)

    async def close(self):
        await self.ws.close()

    @staticmethod
    def __check_status(res):
        if res["status"] != "success":
            raise Exception(res["payload"]["reason"])
