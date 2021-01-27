#!/usr/bin/tclsh

package require sqlite3


proc get_singals {filename anno} {
    sqlite3 opendb $filename
    set query "SELECT instance.name, variable.value FROM instance, variable, generator_variable \
    WHERE instance.id = generator_variable.instance_id \
          AND generator_variable.annotation = $anno \
          AND variable.id = generator_variable.variable_id"
    puts $query
    opendb eval $query values {
        puts values
    }
    
}

set filename "debug.db"
set anno ""
get_singals "debug.db" "a"