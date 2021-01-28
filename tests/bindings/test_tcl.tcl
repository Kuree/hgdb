source ../../bindings/tcl/hgdb.tcl

proc assert_str_eq {str1 str2} {
    if {$str1 != $str2} {
        error "$str1 != $str2"
    }
}

proc test_open_db {filename} {
    set db [open_symbol_table $filename]
}

proc test_get_design_signals_with_anno {filename} {
    set db [open_symbol_table $filename]
    set result [get_design_signals_with_anno db "test"]
    assert_str_eq $result "mod.clk"
}

proc test_remap_name {filename} {
    set r1 [remap_name "a" "b.c.c.a"]
    if {$r1 != "a.c.c.a"} {
        error
    }
    assert_str_eq $r1 "a.c.c.a"
    set r2 [remap_name "a" "b"]
    assert_str_eq $r2 "a.b"
}

proc test_get_signals_with_anno {filename} {
    set db [open_symbol_table $filename]
    set result [get_signals_with_anno $db "top.a" "test"]
    assert_str_eq $result "top.a.clk"
}

# test_get_signals_with_anno debug.db