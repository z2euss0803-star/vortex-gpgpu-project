set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root evidence intfix_routed]
file mkdir $out
open_checkpoint [file join $root build artifacts top_routed.dcp]
set blackboxes [get_cells -quiet -hierarchical -filter {IS_BLACKBOX == 1}]
if {[llength $blackboxes]} {error "Unresolved black boxes: $blackboxes"}
if {[llength [get_timing_paths -quiet -delay_type max -slack_lesser_than 0 -max_paths 1]]} {error "Negative setup slack"}
if {[llength [get_timing_paths -quiet -delay_type min -slack_lesser_than 0 -max_paths 1]]} {error "Negative hold slack"}
set fedps [lsort [get_cells -hierarchical -filter {REF_NAME =~ VX_tcu_fedp_int*}]]
if {[llength $fedps] != 4} {error "Expected four integer FEDPs"}
set fd [open [file join $out facts.txt] w]
puts $fd "part=[get_property PART [current_design]]"
puts $fd "blackboxes=[llength $blackboxes]"
puts $fd "integer_fedps=[llength $fedps]"
puts $fd "negative_setup_paths=0"
puts $fd "negative_hold_paths=0"
set i 0
foreach c $fedps {
    puts $fd "fedp_$i=$c"
    write_verilog -force -mode funcsim -cell $c [file join $out fedp_$i.v]
    incr i
}
close $fd
puts "INTFIX_ROUTED_EXPORT_COMPLETE"
close_design
