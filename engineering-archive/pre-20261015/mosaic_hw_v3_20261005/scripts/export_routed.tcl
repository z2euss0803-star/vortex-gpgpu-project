# Shared final audit/export for a fully routed design. root/artifacts are set.
set evidence [file join $root evidence]
set unresolved [get_cells -quiet -hierarchical -filter {IS_BLACKBOX == 1}]
if {[llength $unresolved]} {error "Unresolved black boxes: $unresolved"}
set audit [open [file join $evidence routed_structure_audit.txt] w]
puts $audit "BLACKBOX_COUNT=[llength $unresolved]"
foreach name {{calib_sync_reg[0]} {calib_sync_reg[1]}} {
    set cell [get_cells -quiet $name]
    if {[llength $cell] != 1 || ![get_property ASYNC_REG $cell]} {error "Missing ASYNC_REG on $name"}
    puts $audit "$name ASYNC_REG=[get_property ASYNC_REG $cell] LOC=[get_property LOC $cell] BEL=[get_property BEL $cell]"
}
set chain_loads [get_pins -leaf -of_objects [get_nets -of_objects [get_pins {calib_sync_reg[0]/Q}]] -filter {DIRECTION == IN}]
puts $audit "CALIB_FIRST_STAGE_LOADS=$chain_loads"
if {[llength $chain_loads] != 1 || [get_property NAME $chain_loads] ne {calib_sync_reg[1]/D}} {error "First calibration stage has unexpected fanout"}
set chain_path [get_timing_paths -quiet -from [get_cells {calib_sync_reg[0]}] -to [get_cells {calib_sync_reg[1]}] -max_paths 1]
if {[llength $chain_path] != 1} {error "Calibration stage 0 -> 1 was incorrectly removed from timing"}
puts $audit "CALIB_STAGE0_TO_STAGE1_SETUP_SLACK=[get_property SLACK $chain_path]"
close $audit
report_timing -from [get_cells {calib_sync_reg[0]}] -to [get_cells {calib_sync_reg[1]}] -file [file join $evidence calib_sync_timing.rpt]
report_clocks -file [file join $evidence clocks.rpt]
report_route_status -file [file join $evidence route_status.rpt]
report_utilization -file [file join $evidence utilization_routed.rpt]
report_utilization -hierarchical -hierarchical_depth 12 -file [file join $evidence utilization_routed_hier.rpt]
report_drc -file [file join $evidence drc.rpt]
if {[llength [get_drc_violations -quiet -filter {SEVERITY == Error || SEVERITY == {Critical Warning}}]]} {error "DRC error/critical warning requires review"}
report_cdc -details -file [file join $evidence cdc.rpt]
report_bus_skew -file [file join $evidence bus_skew.rpt]
set report [open [file join $evidence bus_skew.rpt] r]
set skew_text [read $report]
close $report
if {[regexp {Slack \(VIOLATED\)} $skew_text]} {error "Bus skew constraint violated"}
report_timing_summary -check_timing_verbose -report_unconstrained -file [file join $evidence timing_summary.rpt]
if {[llength [get_timing_paths -quiet -delay_type max -slack_lesser_than 0 -max_paths 1]]} {error "Setup timing failed"}
if {[llength [get_timing_paths -quiet -delay_type min -slack_lesser_than 0 -max_paths 1]]} {error "Hold timing failed"}
write_checkpoint -force [file join $artifacts top_routed.dcp]
write_bitstream -force [file join $artifacts top.bit]
source [file join $root scripts export_platform_open.tcl]
puts "HARDWARE_BUILD_PASS: board execution still requires verification"
close_project
