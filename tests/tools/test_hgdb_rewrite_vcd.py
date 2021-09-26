import hgdb
import subprocess
import tempfile
import os
import pytest
import filecmp


def create_db(db_filename):
    db = hgdb.DebugSymbolTable(db_filename)
    db.store_instance(0, "child2")
    db.store_instance(1, "child2.inst1")
    # create generator variables
    var_id = 0
    for idx, inst in enumerate(["child2", "child2.inst1", "child2.inst2"]):
        db.store_instance(idx, inst)
        db.store_variable(var_id, "io_a_b")
        db.store_variable(var_id + 1, "io_a_c")
        db.store_generator_variable("io.a.b", idx, var_id)
        db.store_generator_variable("io.a.c", idx, var_id + 1)
        var_id += 2
    for i in range(4):
        db.store_variable(var_id, "a[{0}]".format(i))
        # we rename array as well
        db.store_generator_variable("abc[{0}]".format(i), 0, var_id)
        var_id += 1


def test_hgdb_rewrite_vcd(get_build_folder, get_tools_vector_dir):
    with tempfile.TemporaryDirectory() as temp:
        db = os.path.join(temp, "debug.db")
        create_db(db)
        rewrite = os.path.join(get_build_folder(), "tools", "hgdb-rewrite-vcd", "hgdb-rewrite-vcd")
        if not os.path.exists(rewrite):
            pytest.skip("hgdb-rewrite-vcd not available")
        new_vcd = os.path.join(temp, "new.vcd")
        vector_dir = get_tools_vector_dir()
        target_vcd = os.path.join(vector_dir, "waveform5.vcd")
        gold_vcd = os.path.join(vector_dir, "waveform5.rewrite.vcd")
        subprocess.check_call([rewrite, "-i", target_vcd, "-d", db, "-o", new_vcd])
        assert filecmp.cmp(gold_vcd, new_vcd)


if __name__ == "__main__":
    import sys

    sys.path.append(os.getcwd())
    from conftest import get_build_folder_fn, get_tools_vector_dir_fn
    test_hgdb_rewrite_vcd(get_build_folder_fn, get_tools_vector_dir_fn)
