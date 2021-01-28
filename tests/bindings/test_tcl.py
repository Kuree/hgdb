import hgdb
import tkinter
import os
import tempfile


def find_hgdb_tcl():
    path = os.path.abspath(__file__)
    for i in range(3):
        path = os.path.dirname(path)
    tcl = os.path.join(path, "bindings", "tcl", "hgdb.tcl")
    return tcl


def write_db(dirname):
    filename = os.path.join(dirname, "debug.db")
    db = hgdb.DebugSymbolTable(filename)
    db.store_instance(0, "mod")
    db.store_variable(0, "clk")
    db.store_generator_variable("clk", 0, 0, "test")
    return filename


def test_open_db():
    with tempfile.TemporaryDirectory() as temp:
        db_filename = write_db(temp)
        hgdb_tcl = find_hgdb_tcl()
        tcl = tkinter.Tcl()
        tcl.eval("source {0}".format(hgdb_tcl))
        tcl.eval("open_symbol_table {0}".format(db_filename))


def test_read_signals():
    with tempfile.TemporaryDirectory() as temp:
        db_filename = write_db(temp)
        hgdb_tcl = find_hgdb_tcl()
        tcl = tkinter.Tcl()
        tcl.eval("source {0}".format(hgdb_tcl))
        tcl.eval("set db [open_symbol_table {0}]".format(db_filename))
        result = tcl.eval('get_singles_with_anno db "test"')
        assert result == "mod.clk"


if __name__ == "__main__":
    test_read_signals()
