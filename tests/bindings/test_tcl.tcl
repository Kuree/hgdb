source ../../bindings/tcl/hgdb.tcl

proc test_open_db {filename} {
    set db [open_symbol_table $filename]
}

proc test_get_singles_with_anno {filename} {
    set db [open_symbol_table $filename]
    set result [get_singles_with_anno db "test"]
    assert $result == "mod.clk"
}

proc test_remap_name {filename} {
    set r1 [remap_name "a" "b.c.c.a"]
    if {$r1 != "a.c.c.a"} {
        error
    }
    set r2 [remap_name "a" "b"]
    if {$r2 != "a.b"} {
        error
    }
}

# test_remap_name "a"