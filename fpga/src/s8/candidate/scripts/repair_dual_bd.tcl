# Repair the existing candidate only; run after prepare_dual.tcl closes it.
set root [file normalize [file join [file dirname [info script]] ..]]
open_project [file join $root build vivado gpgpu_soc.xpr]
set bd [get_files ps_bd.bd]
if {[llength $bd] != 1} { error "Expected exactly one ps_bd.bd" }
open_bd_design $bd
set source_pin [get_bd_pins rst/peripheral_aresetn]
set reset_pin [get_bd_pins cdma/s_axi_lite_aresetn]
if {[llength $source_pin] != 1 || [llength $reset_pin] != 1} {
    error "Missing reset source or CDMA s_axi_lite_aresetn pin"
}
set target_net [get_bd_nets -quiet -of_objects $source_pin]
set old_net [get_bd_nets -quiet -of_objects $reset_pin]
if {[llength $old_net] && $old_net ne $target_net} {
    # Detach only the CDMA sink, leaving any shared constant net untouched.
    disconnect_bd_net $old_net $reset_pin
}
if {![llength [get_bd_nets -quiet -of_objects $reset_pin]]} {
    connect_bd_net $source_pin $reset_pin
}
if {[get_bd_nets -of_objects $reset_pin] ne [get_bd_nets -of_objects $source_pin]} {
    error "CDMA reset repair did not connect the intended synchronous reset"
}
validate_bd_design
save_bd_design
write_bd_tcl -force [file join $root evidence generated_bd.tcl]
generate_target all $bd
set wrapper [make_wrapper -files $bd -top]
foreach wrapper_file $wrapper {
    if {![llength [get_files -quiet $wrapper_file]]} { add_files -norecurse $wrapper_file }
}
set_property generate_synth_checkpoint false $bd
set python {C:/Users/13151/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe}
exec $python -E [file join $root scripts generate_dual_top.py]
set_property top top [get_filesets sources_1]
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1
report_ip_status -file [file join $root evidence ip_status_after_reset_repair.rpt]
puts "DUAL_DDR_CDMA_RESET_REPAIRED: cdma/s_axi_lite_aresetn <- rst/peripheral_aresetn"
close_project

