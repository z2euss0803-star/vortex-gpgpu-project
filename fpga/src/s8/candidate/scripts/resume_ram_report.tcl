set root [file normalize [file join [file dirname [info script]] ..]]
set artifacts [file join $root build artifacts]
open_checkpoint [file join $artifacts top_before_ram_fix.dcp]
source [file join $root scripts post_synth.tcl]
write_checkpoint -force [file join $artifacts top_synth.dcp]
report_utilization -file [file join $root evidence utilization_synth.rpt]
report_utilization -hierarchical -hierarchical_depth 15 -file [file join $root evidence utilization_synth_hier.rpt]
puts "RESOURCE_SYNTHESIS_REPORTED"
close_design
