set root [file normalize [file join $::env(S8_PHYSICAL_ROOT) candidate]]
set run [file normalize $::env(S8_RUN_DIR)]
set artifacts [file join $run artifacts]
set_param general.maxThreads 1
open_checkpoint $::env(S8_SYNTH_DCP)
read_xdc [file join $root board calib_cdc.xdc]
opt_design
write_checkpoint -force [file join $artifacts top_opt.dcp]
report_utilization -file [file join $run reports utilization_opt.rpt]
place_design
write_checkpoint -force [file join $artifacts top_placed.dcp]
report_utilization -file [file join $run reports utilization_placed.rpt]
phys_opt_design
route_design
write_checkpoint -force [file join $artifacts top_routed.dcp]
report_route_status -file [file join $run reports route_status.rpt]
report_utilization -file [file join $run reports utilization_routed.rpt]
report_utilization -hierarchical -hierarchical_depth 15 -file [file join $run reports utilization_routed_hier.rpt]
report_drc -file [file join $run reports drc.rpt]
report_cdc -details -file [file join $run reports cdc.rpt]
report_bus_skew -file [file join $run reports bus_skew.rpt]
report_clocks -file [file join $run reports clocks.rpt]
report_exceptions -coverage -file [file join $run reports exceptions.rpt]
report_timing_summary -check_timing_verbose -report_unconstrained -file [file join $run reports timing_summary.rpt]
set audit [open [file join $run reports route_structure.txt] w]
puts $audit "BLACKBOX_COUNT=[llength [get_cells -quiet -hierarchical -filter {IS_BLACKBOX == 1}]]"
puts $audit "SETUP_NEGATIVE_PATHS=[llength [get_timing_paths -quiet -delay_type max -slack_lesser_than 0 -max_paths 1]]"
puts $audit "HOLD_NEGATIVE_PATHS=[llength [get_timing_paths -quiet -delay_type min -slack_lesser_than 0 -max_paths 1]]"
puts $audit "DRC_ERROR_OR_CRITICAL=[llength [get_drc_violations -quiet -filter {SEVERITY == Error || SEVERITY == {Critical Warning}}]]"
foreach name {{calib_sync_reg[0]} {calib_sync_reg[1]}} {
 set cell [get_cells -quiet $name]
 puts $audit "$name COUNT=[llength $cell] ASYNC_REG=[get_property ASYNC_REG $cell] LOC=[get_property LOC $cell]"
}
set loads [get_pins -leaf -of_objects [get_nets -of_objects [get_pins {calib_sync_reg[0]/Q}]] -filter {DIRECTION == IN}]
puts $audit "CALIB_FIRST_STAGE_LOADS=$loads"
set path [get_timing_paths -quiet -from [get_cells {calib_sync_reg[0]}] -to [get_cells {calib_sync_reg[1]}] -max_paths 1]
puts $audit "CALIB_CHAIN_TIMED_PATH_COUNT=[llength $path]"
if {[llength $path]} {puts $audit "CALIB_CHAIN_SLACK=[get_property SLACK $path]"}
close $audit
close_design
puts "S8_W4_ROUTE_COMPLETE"
