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
        self.token_count = 0

    async def recv(self, timeout=0):
        try:
            if timeout == 0:
                return json.loads(await self.ws.recv())
            else:
                return json.loads(await asyncio.wait_for(self.ws.recv(), timeout))
        except (asyncio.exceptions.IncompleteReadError, websockets.exceptions.ConnectionClosedError):
            return None

    async def send(self, payload):
        # we set our own token
        if "token" not in payload:
            payload["token"] = "python-{0}".format(self.token_count)
            self.token_count += 1
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

    async def step_back(self):
        await self.__send_command("step_back")

    async def reverse_continue(self):
        await self.__send_command("reverse_continue")

    async def __send_command(self, command_str):
        payload = {"request": True, "type": "command", "payload": {"command": command_str}}
        await self.send(payload)
        # no care about the response
        await self.recv()

    async def get_info(self, status_command="breakpoints", check_error=True):
        payload = {"request": True, "type": "debugger-info", "payload": {"command": status_command}}
        return await self.__send_check(payload, check_error)

    async def evaluate(self, scope, expression, is_context=True, check_error=True):
        payload = {"request": True, "type": "evaluation",
                   "payload": {"scope": scope, "expression": expression, "is_context": is_context}}
        return await self.__send_check(payload, check_error=check_error)

    async def change_option(self, check_error=True, **kwargs):
        payload = {"request": True, "type": "option-change", "payload": {}}
        for name, value in kwargs.items():
            payload["payload"][name] = value
        return await self.__send_check(payload, check_error=check_error)

    async def add_monitor(self, name, instance_id=None, breakpoint_id=None, monitor_type="breakpoint"):
        assert (instance_id is not None) or (breakpoint_id is None)
        assert monitor_type in {"breakpoint", "clock_edge"}
        payload = {"request": True, "type": "monitor",
                   "payload": {"action_type": "add", "monitor_type": monitor_type, "var_name": name}}
        if breakpoint_id is not None:
            payload["payload"]["breakpoint_id"] = breakpoint_id
        if instance_id is not None:
            payload["payload"]["instance_id"] = instance_id
        resp = await self.__send_check(payload, True)
        return resp["payload"]["track_id"]

    async def set_value(self, name, value, instance_id=None, breakpoint_id=None, check_error=False):
        payload = {"request": True, "type": "set-value", "payload": {"var_name": name, "value": value}}
        if instance_id is not None:
            payload["payload"]["instance_id"] = instance_id
        if breakpoint_id is not None:
            payload["payload"]["breakpoint_id"] = breakpoint_id
        return await self.__send_check(payload, check_error=check_error)

    async def remove_monitor(self, track_id):
        payload = {"request": True, "type": "monitor", "payload": {"action_type": "remove", "track_id": track_id}}
        await self.__send_check(payload, True)

    async def close(self):
        await self.ws.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        if self.ws is not None:
            await self.ws.close()
            del self.ws

    @staticmethod
    def __check_status(res):
        if res["status"] != "success":
            raise Exception(res["payload"]["reason"])
