import websockets
import json


class HGDBClient:
    def __init__(self, uri, filename):
        self.filename = filename
        self.uri = uri
        self.ws = None

    async def recv(self):
        return json.loads(await self.ws.recv())

    async def send(self, payload):
        await self.ws.send(json.dumps(payload))

    async def connect(self):
        self.ws = await websockets.connect(self.uri)
        if self.filename is not None:
            payload = {"request": True, "type": "connection", "payload": {
                "db_filename": self.filename,
            }}
            await self.send(payload)
            res = await self.recv()
            self.__check_status(res)

    async def set_breakpoint(self, filename, line_num, column_num=0, token="", cond="",
                             check_error=True):
        payload = {"request": True, "type": "breakpoint", "token": token,
                   "payload": {"filename": filename, "line_num": line_num, "column_num": column_num,
                               "action": "add"}}
        if len(cond) > 0:
            payload["payload"]["condition"] = cond
        await self.send(payload)
        res = await self.recv()
        if check_error:
            self.__check_status(res)
        return res

    async def remove_breakpoint(self, filename, line_num, column_num=0, token="", check_error=True):
        payload = {"request": True, "type": "breakpoint", "token": token,
                   "payload": {"filename": filename, "line_num": line_num, "column_num": column_num,
                               "action": "remove"}}
        await self.send(payload)
        res = await self.recv()
        if check_error:
            self.__check_status(res)
            return res
        return res

    async def request_breakpoint_location(self, filename, line_num=None, column_num=None):
        payload = {"request": True, "type": "bp-location", "payload": {"filename": filename}}
        if line_num is not None:
            payload["payload"]["line_num"] = line_num
        if column_num is not None:
            payload["payload"]["column_num"] = column_num
        await self.send(payload)
        res = await self.recv()
        return res

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
        await self.send(payload)
        res = await self.recv()
        if check_error:
            self.__check_status(res)
        return res

    async def close(self):
        await self.ws.close()

    @staticmethod
    def __check_status(res):
        if res["status"] != "success":
            raise Exception(res["payload"]["reason"])

