set root [file normalize [file join [file dirname [info script]] ..]]
open_project [file join $root build vivado gpgpu_soc.xpr]
open_bd_design [get_files ps_bd.bd]
if {[llength [get_bd_cells]] != 0} {error "BD already populated"}
source [file join $root scripts build_bd.tcl]
close_project
