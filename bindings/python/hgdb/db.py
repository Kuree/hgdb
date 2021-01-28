import _hgdb


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

    def store_context_variable(self, name: str, breakpoint_id: int, variable_id: int):
        if not self.has_breakpoint_id(breakpoint_id):
            raise DebugSymbolTableException(f"Breakpoint {breakpoint_id} does not exist!")
        if not self.has_variable_id(variable_id):
            raise DebugSymbolTableException(f"Variable {variable_id} does not exist!")
        _hgdb.store_context_variable(self.db, name, breakpoint_id, variable_id)

    def store_generator_variable(self, name: str, instance_id: int, variable_id: int, annotation: str = ""):
        if not self.has_instance_id(instance_id):
            raise DebugSymbolTableException(f"Instance {instance_id} does not exist!")
        if not self.has_variable_id(variable_id):
            raise DebugSymbolTableException(f"Variable {variable_id} does not exist!")
        _hgdb.store_generator_variable(self.db, name, instance_id, variable_id, annotation)

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
