import sqlite3
import tempfile
import hgdb
import os
import pytest


def get_conn_cursor(db_name):
    conn = sqlite3.connect(db_name)
    c = conn.cursor()
    return conn, c


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


if __name__ == "__main__":
    test_store_scope()
