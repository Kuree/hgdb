try:
    import hgdb
    import tkinter
    tcl_available = True
except ImportError:
    tcl_available = False
import os
import tempfile
import pytest
import re


def find_test_tcl():
    path = os.path.dirname(os.path.abspath(__file__))
    tcl = os.path.join(path, "test_tcl.tcl")
    return tcl


@pytest.fixture()
def symbol_table():
    with tempfile.TemporaryDirectory() as temp:
        filename = os.path.join(temp, "debug.db")
        db = hgdb.DebugSymbolTable(filename)
        db.store_instance(0, "mod")
        db.store_variable(0, "clk")
        db.store_generator_variable("clk", 0, 0, "test")
        yield filename


@pytest.fixture(scope="function")
def initialize():
    cwd = os.getcwd()
    dirname = os.path.dirname(os.path.abspath(__file__))
    os.chdir(dirname)
    yield dirname
    os.chdir(cwd)


# search for all available tests
with open(find_test_tcl()) as f:
    file_context = f.read()
matches = re.finditer(r"proc\s+(test_.*)\s{filename", file_context, re.MULTILINE)
test_list = []
for _, match in enumerate(matches, start=1):
    name = match.group(1)
    test_list.append(name)


@pytest.mark.skipif(not tcl_available, reason="tcl not available")
@pytest.mark.parametrize("test_name", test_list)
def test_tcl_function(symbol_table, test_name, initialize):
    test_tcl = find_test_tcl()
    tcl = tkinter.Tcl()
    tcl.eval("source {0}".format(test_tcl))
    tcl.eval("{0} {1}".format(test_name, symbol_table))
