import _hgdb
import json
import typing
import enum


class DebugSymbolTableException(Exception):
    def __init__(self, what):
        super().__init__(what)


# wrapper class
class DebugSymbolTable:
    def __init__(self, filename):
        self.db = _hgdb.init_debug_db(filename)

    def store_variable(self, id_: int, value: str, is_rtl: bool = True):
        _hgdb.store_variable(self.db, id_, value, is_rtl)

    def store_breakpoint(self, id_: int, instance_id: int, filename: str, line_num: int, column_num: int = 0,
                         condition: str = "", trigger: str = ""):
        # check instance id
        if not self.has_instance_id(instance_id):
            raise DebugSymbolTableException(f"Instance {instance_id} does not exist!")
        _hgdb.store_breakpoint(self.db, id_, instance_id, filename, line_num, column_num, condition, trigger)

    def store_instance(self, id_: int, full_name: str, annotation: str = ""):
        _hgdb.store_instance(self.db, id_, full_name, annotation)

    def store_scope(self, id_: int, *args: int):
        for breakpoint_id in args:
            if not self.has_breakpoint_id(breakpoint_id):
                raise DebugSymbolTableException(f"Breakpoint {breakpoint_id} does not exist!")
        _hgdb.store_scope(self.db, id_, *args)

    def store_context_variable(self, name: str, breakpoint_id: int, variable_id: int, delay_mode: bool = False):
        if not self.has_breakpoint_id(breakpoint_id):
            raise DebugSymbolTableException(f"Breakpoint {breakpoint_id} does not exist!")
        if not self.has_variable_id(variable_id):
            raise DebugSymbolTableException(f"Variable {variable_id} does not exist!")
        _hgdb.store_context_variable(self.db, name, breakpoint_id, variable_id, delay_mode)

    def store_generator_variable(self, name: str, instance_id: int, variable_id: int, annotation: str = ""):
        if not self.has_instance_id(instance_id):
            raise DebugSymbolTableException(f"Instance {instance_id} does not exist!")
        if not self.has_variable_id(variable_id):
            raise DebugSymbolTableException(f"Variable {variable_id} does not exist!")
        _hgdb.store_generator_variable(self.db, name, instance_id, variable_id, annotation)

    def store_assignment(self, name: str, value: str, breakpoint_id: int, cond: str = ""):
        if not self.has_breakpoint_id(breakpoint_id):
            raise DebugSymbolTableException(f"Breakpoint {breakpoint_id} does not exist!")
        _hgdb.store_assignment(self.db, name, value, breakpoint_id, cond)

    # checkers
    def has_instance_id(self, id_):
        return _hgdb.has_instance_id(self.db, id_)

    def has_breakpoint_id(self, id_):
        return _hgdb.has_breakpoint_id(self.db, id_)

    def has_variable_id(self, id_):
        return _hgdb.has_variable_id(self.db, id_)

    # get other information
    def get_filenames(self):
        return _hgdb.get_filenames(self.db)

    # transaction based insertion
    def begin_transaction(self):
        return _hgdb.begin_transaction(self.db)

    def end_transaction(self):
        return _hgdb.end_transaction(self.db)


# pure Python implementation
class Scope:
    def __init__(self, parent: typing.Union["Scope", None]):
        self.__children = []
        self.filename = ""
        self.line = 0
        self.col = 0
        self.parent = parent
        if parent is not None:
            parent.__children.append(self)

    def get_filename(self):
        p = self
        while p is not None:
            if p.filename:
                return p.filename
            p = p.parent

    def type(self):
        if len(self.__children) > 0:
            return "block"
        else:
            return "none"

    def to_dict(self):
        res = {"type": self.type(), "line": self.line}
        if self.col > 0:
            res["col"] = self.col
        scopes = []
        for scope in self.__children:
            assert scope.type() != "module"
            scopes.append(scope.to_dict())
        if len(scopes) > 0 or self.type() == "module":
            assert self.type() in {"block", "module"}
            res["scope"] = scopes
        if self.filename:
            res["filename"] = self.filename
        return res

    def add_child(self, t: type, *args, **kwargs):
        c = t(*args, self)
        for key, value in kwargs.items():
            setattr(c, key, value)
        return c


class VariableType(enum.Enum):
    normal = "normal"
    delay = "delay"


class VariableIndex:
    def __init__(self, var: "Variable", min_: int, max_: int):
        self.var = var
        self.min = min_
        self.max = max_

    def to_dict(self):
        return {"var": self.var.to_dict(), "min": self.min, "max": self.max}


class Variable:
    def __init__(self, name: str, value: str, rtl: bool = True):
        self.name = name
        self.value = value
        self.rtl = rtl
        self.indices: typing.List[VariableIndex] = []
        self.type = VariableType.normal

    def to_dict(self):
        res = {"name": self.name, "value": self.value, "rtl": self.rtl}
        if self.indices:
            indices = [v.to_dict() for v in self.indices]
            res["indices"] = indices
        if self.type != VariableType.normal:
            res["type"] = self.type.value
        return res


class VarAssign(Scope):
    def __init__(self, var: Variable, parent: Scope):
        super(VarAssign, self).__init__(parent)
        self.var = var

    def type(self):
        return "assign"

    def to_dict(self):
        res = super(VarAssign, self).to_dict()
        res["variable"] = self.var.to_dict()
        return res


class VarDecl(VarAssign):
    def __init__(self, var: Variable, parent: Scope):
        super(VarDecl, self).__init__(var, parent)

    def type(self):
        return "decl"


class Module(Scope):
    def __init__(self, name: str):
        super(Module, self).__init__(None)
        self.name = name
        self.instances: typing.Dict[str, str] = {}

    def add_instance(self, mod: "Module", inst_name: str):
        self.instances[inst_name] = mod.name

    def type(self):
        return "module"

    def to_dict(self):
        res = super(Module, self).to_dict()
        res["name"] = self.name
        instances = []
        for mod, inst in self.instances.items():
            instances.append({"name": inst, "module": mod})
        res["instances"] = instances
        res["variables"] = []
        return res


class JSONSymbolTable:
    def __init__(self, top: str, filename: str, generator: str = "python"):
        self.__top = top
        self.__filename = filename
        self.__generator = generator
        self.__fd = open(self.__filename, "w+")
        self.table = []

    def add_definition(self, mod: Module):
        self.table.append(mod)

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __enter__(self):
        return self

    def __to_dict(self):
        res = {"top": self.__top, "generator": self.__generator, "table": []}
        for entry in self.table:
            res["table"].append(entry.to_dict())
        return res

    def close(self):
        json.dump(self.__to_dict(), self.__fd)
        self.__fd.close()
