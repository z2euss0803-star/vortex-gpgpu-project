# Resume the measured synthesis checkpoint without repeating synthesis.
set root [file normalize [file join [file dirname [info script]] ..]]
set artifacts [file join $root build artifacts]
set_param general.maxThreads 4
open_checkpoint [file join $artifacts top_synth.dcp]
opt_design
place_design
phys_opt_design
route_design
write_checkpoint -force [file join $artifacts top_routed.dcp]
report_clocks -file [file join $root evidence clocks.rpt]
report_route_status -file [file join $root evidence route_status.rpt]
report_utilization -file [file join $root evidence utilization_routed.rpt]
report_utilization -hierarchical -hierarchical_depth 15 -file [file join $root evidence utilization_routed_hier.rpt]
report_drc -file [file join $root evidence drc.rpt]
report_timing_summary -check_timing_verbose -report_unconstrained -file [file join $root evidence timing_summary.rpt]
puts "IMPLEMENTATION_REPORTS_READY"
if {[llength [get_timing_paths -quiet -delay_type max -slack_lesser_than 0 -max_paths 1]]} {error "Setup timing failed"}
if {[llength [get_timing_paths -quiet -delay_type min -slack_lesser_than 0 -max_paths 1]]} {error "Hold timing failed"}
write_bitstream -force [file join $artifacts top_int8_signext_fix_20260927.bit]
puts "TIMING_AND_BITSTREAM_PASS: board execution not performed"
close_design
