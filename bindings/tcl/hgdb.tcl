#!/usr/bin/tclsh

package require sqlite3

proc open_symbol_table {filename} {
    sqlite3 db $filename;
    return db;
}

proc get_singles_with_anno {symbol_table anno} {
    set query "SELECT instance.name, variable.value FROM instance, variable, generator_variable \
    WHERE instance.id = generator_variable.instance_id \
          AND generator_variable.annotation = \"$anno\" \
          AND variable.id = generator_variable.variable_id"
    set result {}
    $symbol_table eval $query values {
        set column_names $values(*)
        set row_list {}
        foreach column $column_names {
            lappend row_list $values($column)
        }
        set signal_name [join $row_list "."]
        lappend result $signal_name
    }
    return $result;
}
