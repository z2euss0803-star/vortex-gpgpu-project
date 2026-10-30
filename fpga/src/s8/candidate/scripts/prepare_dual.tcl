set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root scripts create_project.tcl]
source [file join $root scripts finalize_dual.tcl]
