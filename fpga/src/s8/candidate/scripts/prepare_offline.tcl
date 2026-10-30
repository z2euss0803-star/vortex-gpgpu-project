set root [file normalize [file join [file dirname [info script]] ..]]
set_param general.maxThreads 1
source [file join $root scripts create_project.tcl]
if {[info exists ::env(HUNET_BUILD_PYTHON)]} {
 set python $::env(HUNET_BUILD_PYTHON)
} else {
 set python {C:/Users/13151/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe}
}
exec $python -E [file join $root scripts regenerate_physical_top.py]
open_project [file join $root build vivado gpgpu_soc.xpr]
source [file join $root config sources.tcl]
set_property verilog_define $gpu_defines [get_filesets sources_1]
set_property include_dirs $gpu_includes [get_filesets sources_1]
add_files -norecurse [glob [file join $root rtl *.sv]]
add_files -fileset constrs_1 -norecurse [list [file join $root board dual_ddr.xdc] [file join $root board calib_cdc.xdc]]
set_property USED_IN_SYNTHESIS false [get_files calib_cdc.xdc]
set_property PROCESSING_ORDER LATE [get_files calib_cdc.xdc]
set_property top top [get_filesets sources_1]
set_property source_mgmt_mode None [current_project]
reorder_files -front [list [get_files *VX_gpu_pkg.sv] [get_files *VX_fpu_pkg.sv] [get_files *VX_tcu_pkg.sv]]
update_compile_order -fileset sources_1
open_bd_design [get_files ps_bd.bd]
set audit [open [file join $root evidence downstream_properties.txt] w]
foreach obj [list [get_bd_intf_ports HP] [get_bd_intf_pins memory_fabric/S00_AXI] [get_bd_intf_pins memory_fabric/M01_AXI] [get_bd_intf_pins ddr/C0_DDR4_S_AXI] [get_bd_cells memory_fabric] [get_bd_cells ddr]] {
 puts $audit "OBJECT=$obj"
 foreach prop [lsort [list_property $obj]] {
  if {[string match CONFIG.* $prop]} {puts $audit "$prop=[get_property $prop $obj]"}
 }
}
close $audit
write_bd_tcl -force [file join $root evidence generated_bd.tcl]
close_bd_design [get_bd_designs ps_bd]
report_ip_status -file [file join $root evidence ip_status_prepared.rpt]
file mkdir [file join $root build artifacts]
puts "OFFLINE_PREPARE_PASS"
close_project
