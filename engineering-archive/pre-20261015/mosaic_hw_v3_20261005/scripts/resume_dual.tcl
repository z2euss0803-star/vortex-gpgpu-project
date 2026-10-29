set root [file normalize [file join [file dirname [info script]] ..]]
source [file join $root scripts repair_dual_bd.tcl]
source [file join $root scripts finalize_dual.tcl]
