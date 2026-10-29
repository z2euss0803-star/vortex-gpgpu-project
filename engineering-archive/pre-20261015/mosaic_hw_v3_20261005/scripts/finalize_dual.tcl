set root [file normalize [file join [file dirname [info script]] ..]]
set python {C:/Users/13151/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe}
exec $python -E [file join $root scripts generate_dual_top.py]
open_project [file join $root build vivado gpgpu_soc.xpr]
source [file join $root config sources.tcl]
set_property verilog_define $gpu_defines [get_filesets sources_1]
set_property verilog_define [concat $gpu_defines SIMULATION] [get_filesets sim_1]
add_files -norecurse [glob [file join $root rtl *.sv]]
add_files -fileset constrs_1 -norecurse [file join $root board dual_ddr.xdc]
add_files -fileset constrs_1 -norecurse [file join $root board calib_cdc.xdc]
set_property USED_IN_SYNTHESIS false [get_files *calib_cdc.xdc]
set_property PROCESSING_ORDER LATE [get_files *calib_cdc.xdc]
set_property top top [get_filesets sources_1]
set_property include_dirs [concat $gpu_includes [file join $root tb]] [get_filesets sim_1]
set_property source_mgmt_mode None [current_project]
reorder_files -front [list [get_files *VX_gpu_pkg.sv] [get_files *VX_fpu_pkg.sv] [get_files *VX_tcu_pkg.sv]]
add_files -fileset sim_1 -norecurse [file join $root tb tb_gpgpu.sv]
set_property top tb_gpgpu [get_filesets sim_1]
update_compile_order -fileset sources_1
update_compile_order -fileset sim_1
launch_simulation -scripts_only
file mkdir [file join $root build artifacts]
write_hw_platform -fixed -force -file [file join $root build artifacts gpgpu_no_bit.xsa]
report_ip_status -file [file join $root evidence ip_status.rpt]
puts "DUAL_DDR_PROJECT_PREPARED"
close_project
