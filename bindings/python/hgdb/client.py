import asyncio
import json
import time

import websockets


def crete_debug_protocol(*args, **kwargs):
    if "ping_interval" in kwargs:
        kwargs.pop("ping_interval")
    kwargs["ping_interval"] = None
    return websockets.WebSocketClientProtocol(*args, **kwargs)


class HGDBClientException(RuntimeError):
    def __init__(self, *args):
        super(HGDBClientException, self).__init__(*args)


class HGDBClient:
    def __init__(self, uri, filename, src_mapping=None, debug=False):
        self.filename = filename
        self.uri = uri
        self.ws = None
        self.src_mapping = src_mapping
        self.token_count = 0
        self._bps = []
        self.debug = debug

    async def recv(self, timeout=0):
        try:
            if timeout == 0:
                res = json.loads(await self.ws.recv())
            else:
                res = json.loads(await asyncio.wait_for(self.ws.recv(), timeout))
            if res["type"] == "breakpoint":
                self._bps.append(res)
            return res
        except (asyncio.exceptions.IncompleteReadError, websockets.exceptions.ConnectionClosedError,
                asyncio.exceptions.TimeoutError):
            return None

    async def recv_bp(self, timeout=0):
        while len(self._bps) == 0:
            res = await self.recv(timeout)
            if res is None and len(self._bps) == 0:
                return None
        bp = self._bps[0]
        self._bps = self._bps[1:]
        return bp

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
                protocol = crete_debug_protocol if self.debug else None
                self.ws = await websockets.connect(self.uri, create_protocol=protocol)
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

    async def set_data_breakpoint(self, breakpoint_id, var_name, token="", cond="", check_error=True):
        payload = {"request": True, "type": "data-breakpoint", "token": token,
                   "payload": {"var_name": var_name, "breakpoint-id": breakpoint_id, "action": "add"}}
        if len(cond) > 0:
            payload["payload"]["condition"] = cond
        return await self.__send_check(payload, check_error)

    async def valid_data_breakpoint(self, breakpoint_id, var_name, token="", cond=""):
        payload = {"request": True, "type": "data-breakpoint", "token": token,
                   "payload": {"var_name": var_name, "breakpoint-id": breakpoint_id, "action": "info"}}
        if len(cond) > 0:
            payload["payload"]["condition"] = cond
        res = await self.__send_check(payload, False)
        return res["status"] == "success"

    async def set_breakpoint_id(self, bp_id, cond="", token="", check_error=True):
        payload = {"request": True, "type": "breakpoint-id", "token": token,
                   "payload": {"id": bp_id, "action": "add"}}
        if len(cond) > 0:
            payload["payload"]["condition"] = cond
        return await self.__send_check(payload, check_error)

    async def remove_breakpoint(self, filename, line_num, column_num=0, token=""):
        payload = {"request": True, "type": "breakpoint", "token": token,
                   "payload": {"filename": filename, "line_num": line_num, "column_num": column_num,
                               "action": "remove"}}
        return await self.__send_check(payload)

    async def remove_breakpoint_id(self, bp_id, token="", check_error=True):
        payload = {"request": True, "type": "breakpoint-id", "token": token,
                   "payload": {"id": bp_id, "action": "remove"}}
        return await self.__send_check(payload, check_error)

    async def remove_data_breakpoint(self, bp_id, token="", check_error=True):
        payload = {"request": True, "type": "data-breakpoint", "token": token,
                   "payload": {"breakpoint-id": bp_id, "action": "remove"}}
        return await self.__send_check(payload, check_error)

    async def request_breakpoint_location(self, filename, line_num=None, column_num=None):
        payload = {"request": True, "type": "bp-location", "payload": {"filename": filename}}
        if line_num is not None:
            payload["payload"]["line_num"] = line_num
        if column_num is not None:
            payload["payload"]["column_num"] = column_num
        return await self.__send_check(payload)

    async def continue_(self, timeout=0):
        await self.__send_command("continue", timeout=timeout)

    async def stop(self, timeout=0):
        await self.__send_command("stop", timeout=timeout)

    async def step_over(self, timeout=0):
        await self.__send_command("step_over", timeout=timeout)

    async def step_back(self, timeout=0):
        await self.__send_command("step_back", timeout=timeout)

    async def reverse_continue(self, timeout=0):
        await self.__send_command("reverse_continue", timeout=timeout)

    async def jump(self, time_val, timeout=0):
        return await self.__send_command("jump", time=time_val, timeout=timeout)

    async def __send_command(self, command_str, timeout=0, **kwargs):
        payload = {"request": True, "type": "command", "payload": {"command": command_str}}
        payload["payload"].update(**kwargs)
        await self.send(payload)
        # in case the downstream is interested about the response
        return await self.recv(timeout=timeout)

    async def get_info(self, status_command="breakpoints", check_error=True):
        payload = {"request": True, "type": "debugger-info", "payload": {"command": status_command}}
        return await self.__send_check(payload, check_error)

    async def __get_current_breakpoints(self, flag):
        # helper function
        info = await self.get_info()
        bps = info["payload"]["breakpoints"]
        result = []
        for bp in bps:
            if bp["type"] & flag:
                result.append(bp)
        return result

    async def get_current_normal_breakpoints(self):
        return await self.__get_current_breakpoints(1)

    async def get_current_data_breakpoints(self):
        return await self.__get_current_breakpoints(2)

    async def get_filenames(self):
        info = await self.get_info("filename")
        return info["payload"]["filenames"]

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
            raise HGDBClientException(res["payload"]["reason"])
