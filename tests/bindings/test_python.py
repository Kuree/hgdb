import sqlite3
import tempfile
import hgdb
import os
import pytest
import subprocess
import filecmp


def get_conn_cursor(db_name):
    conn = sqlite3.connect(db_name)
    c = conn.cursor()
    return conn, c


def get_vector_folder():
    root = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(root, "vectors")


def test_store_instance():
    with tempfile.TemporaryDirectory() as temp:
        db_name = os.path.join(temp, "debug.db")
        db = hgdb.DebugSymbolTable(db_name)
        db.store_instance(42, "test")

        conn, c = get_conn_cursor(db_name)
        c.execute("SELECT COUNT(*) FROM instance WHERE id=?", (42,))
        r = c.fetchone()[0]
        assert r == 1
        conn.close()


def test_store_breakpoint():
    with tempfile.TemporaryDirectory() as temp:
        db_name = os.path.join(temp, "debug.db")
        db = hgdb.DebugSymbolTable(db_name)
        # no instance matching yet
        with pytest.raises(hgdb.db.DebugSymbolTableException) as ex:
            db.store_breakpoint(1, 42, "/tmp/test.py", 1)
        assert ex.value.args[0]
        db.store_instance(42, "test")
        db.store_breakpoint(1, 42, "/tmp/test.py", 1)

        conn, c = get_conn_cursor(db_name)
        c.execute("SELECT COUNT(*) FROM breakpoint WHERE filename=? AND line_num=?", ("/tmp/test.py", 1))
        r = c.fetchone()[0]
        assert r == 1
        conn.close()


def test_store_context_variable():
    with tempfile.TemporaryDirectory() as temp:
        db_name = os.path.join(temp, "debug.db")
        db = hgdb.DebugSymbolTable(db_name)
        # no variable matching yet
        with pytest.raises(hgdb.db.DebugSymbolTableException) as ex:
            db.store_context_variable("a", 1, 43)
        assert ex.value.args[0]

        db.store_instance(42, "test")
        db.store_breakpoint(1, 42, "/tmp/test.py", 1)
        db.store_variable(43, "value")
        db.store_context_variable("a", 1, 43)

        conn, c = get_conn_cursor(db_name)
        c.execute("SELECT COUNT(*) FROM context_variable WHERE breakpoint_id=?", (1, ))
        r = c.fetchone()[0]
        assert r == 1
        conn.close()


def test_store_generator_variable():
    with tempfile.TemporaryDirectory() as temp:
        db_name = os.path.join(temp, "debug.db")
        db = hgdb.DebugSymbolTable(db_name)
        # no instance matching yet
        with pytest.raises(hgdb.db.DebugSymbolTableException) as ex:
            db.store_generator_variable("a", 42, 43)
        assert ex.value.args[0]

        db.store_instance(42, "test")
        db.store_breakpoint(1, 42, "/tmp/test.py", 1)
        db.store_variable(43, "value")
        db.store_generator_variable("a", 42, 43)

        conn, c = get_conn_cursor(db_name)
        c.execute("SELECT COUNT(*) FROM generator_variable WHERE instance_id=?", (42, ))
        r = c.fetchone()[0]
        assert r == 1
        conn.close()


def test_store_scope():
    with tempfile.TemporaryDirectory() as temp:
        db_name = os.path.join(temp, "debug.db")
        db = hgdb.DebugSymbolTable(db_name)

        db.store_instance(42, "test")
        for i in range(4):
            db.store_breakpoint(i, 42, "/tmp/test.py", i + 1)
        db.store_scope(0, *[0, 1, 2, 3])

        conn, c = get_conn_cursor(db_name)
        c.execute("SELECT breakpoints FROM scope WHERE scope=?", (0, ))
        r = c.fetchone()[0]
        assert r == " ".join([str(i) for i in range(4)])
        conn.close()


def test_toml_scope_parsing():
    vectors_dir = get_vector_folder()
    t = os.path.join(vectors_dir, "test_toml_scope_parsing.toml")
    with tempfile.TemporaryDirectory() as temp:
        db_name = os.path.join(temp, "debug.db")
        subprocess.check_call(["toml2hgdb", t, db_name])

        conn, c = get_conn_cursor(db_name)
        c.execute("SELECT * FROM context_variable WHERE breakpoint_id = 0")
        r = c.fetchall()
        assert len(r) == 2
        c.execute("SELECT * FROM context_variable WHERE breakpoint_id = 1")
        r = c.fetchall()
        assert len(r) == 4
        c.execute("SELECT * FROM context_variable WHERE breakpoint_id = 2")
        r = c.fetchall()
        assert len(r) == 5
        c.execute("SELECT * FROM scope")
        r = c.fetchall()
        assert len(r) == 1
        assert r[0][-1] == " ".join(["0", "1", "2"])
        conn.close()


def test_python_json_table():
    from hgdb.db import JSONSymbolTable, Module, Variable, VarAssign, Scope
    gold_file = os.path.join(get_vector_folder(), "python.json")
    with tempfile.TemporaryDirectory() as temp:
        filename = os.path.join(temp, "debug.json")
        mod = Module("mod")
        block = mod.add_child(Scope, filename="test.py")
        block.filename = "test.py"
        block.add_child(Scope, line=1)
        var = Variable("a", "a0", True)
        block.add_child(VarAssign, var, line=2)
        child_mod = Module("child")
        mod.add_instance(child_mod, "inst")
        with JSONSymbolTable("mod", filename) as db:
            db.add_definition(mod)
            db.add_definition(child_mod)
        assert filecmp.cmp(gold_file, filename)


if __name__ == "__main__":
    test_python_json_table()
