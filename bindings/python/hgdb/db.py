import _hgdb
import json
import typing


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
        self.children = []
        self.filename = ""
        self.line = 0
        self.col = 0
        self.parent = parent
        if parent is not None:
            parent.children.append(self)

    def get_filename(self):
        p = self
        while p is not None:
            if p.filename:
                return p.filename
            p = p.parent

    def type(self):
        if len(self.children) > 0:
            return "block"
        else:
            return "none"

    def to_dict(self):
        res = {"type": self.type(), "line": self.line}
        if self.col > 0:
            res["col"] = self.col
        scopes = []
        for scope in self.children:
            scopes.append(scope.to_dict())
        if len(scopes):
            res["scopes"] = scopes
        if self.filename:
            res["filename"] = self.filename
        return res


class Variable:
    def __init__(self, name: str, value: str, rtl: bool = True):
        self.name = name
        self.value = value
        self.rtl = rtl

    def to_dict(self):
        return {"name": self.name, "value": self.value, "rtl": self.rtl}


class VarAssign(Scope):
    def __init__(self, var: Variable, parent):
        super(VarAssign, self).__init__(parent)
        self.var = var

    def type(self):
        return "assign"

    def to_dict(self):
        res = super(VarAssign, self).to_dict()
        res["var"] = self.var.to_dict()
        return res


class VarDecl(VarAssign):
    def __init__(self, var: Variable, parent):
        super(VarDecl, self).__init__(var, parent)

    def type(self):
        return "decl"


class Module(Scope):
    def __init__(self, name: str):
        super(Module, self).__init__(None)
        self.name = name


class JSONSymbolTable:

    def __init__(self, filename: str):
        self.__filename = filename
        self.__fd = open(self.__filename, "w+")

        # indexed by definition name
        self.scopes = {}

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __enter__(self):
        pass

    def close(self):
        json.dump(self.scopes, self.__fd)
        self.__fd.close()
