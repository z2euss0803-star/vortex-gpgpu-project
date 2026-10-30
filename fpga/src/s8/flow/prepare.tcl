# Frozen W4 source is already materialized. Never regenerate its physical top.
set root [file normalize [file join $::env(S8_PHYSICAL_ROOT) candidate]]
set run [file normalize $::env(S8_RUN_DIR)]
# Ask vendor IP/BD only for synthesis products. Keep the frozen generators
# unchanged and do not generate simulation targets or export simulation scripts.
rename generate_target ::s8_vendor_generate_target
proc generate_target {args} {
 if {[lindex $args 0] eq "all"} {lset args 0 synthesis}
 if {[lindex $args 0] ne "synthesis"} {error "Unexpected target outside synthesis-only preparation: $args"}
 return [uplevel 1 [linsert $args 0 ::s8_vendor_generate_target]]
}
source [file join $root scripts create_project.tcl]
rename generate_target {}
rename ::s8_vendor_generate_target generate_target
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
set audit [open [file join $run reports downstream_properties.txt] w]
foreach obj [list [get_bd_intf_ports HP] [get_bd_intf_pins memory_fabric/S00_AXI] [get_bd_intf_pins memory_fabric/M01_AXI] [get_bd_intf_pins ddr/C0_DDR4_S_AXI] [get_bd_cells memory_fabric] [get_bd_cells ddr]] {
 puts $audit "OBJECT=$obj"
 foreach prop [lsort [list_property $obj]] {
  if {[string match CONFIG.* $prop]} {puts $audit "$prop=[get_property $prop $obj]"}
 }
}
close $audit
write_bd_tcl -force [file join $run reports generated_bd.tcl]
close_bd_design [get_bd_designs ps_bd]
report_ip_status -file [file join $run reports ip_status.rpt]
file mkdir [file join $root build artifacts]
close_project
puts "S8_W4_PREPARE_COMPLETE"
