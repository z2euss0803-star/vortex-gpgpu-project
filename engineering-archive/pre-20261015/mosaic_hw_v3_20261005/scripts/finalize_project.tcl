set root [file normalize [file join [file dirname [info script]] ..]]
open_project [file join $root build vivado gpgpu_soc.xpr]
source [file join $root config sources.tcl]
set old [get_files -quiet */vendor/vortex/hw/rtl/*]
if {[llength $old]} {remove_files $old}
add_files -norecurse $gpu_sources
set_property include_dirs $gpu_includes [get_filesets sources_1]
set_property include_dirs [concat $gpu_includes [file join $root tb]] [get_filesets sim_1]
add_files -norecurse [glob [file join $root rtl *.sv]]
set_property top top [get_filesets sources_1]
foreach ip [get_ips {ctrl_axi_converter xil_*}] {set_property generate_synth_checkpoint false [get_files [get_property IP_FILE $ip]]}
generate_target all [get_files ps_bd.bd]
generate_target all [get_ips {ctrl_axi_converter xil_*}]
update_compile_order -fileset sources_1
file mkdir [file join $root build artifacts]
if {![file exists [file join $root build artifacts gpgpu_no_bit.xsa]]} {write_hw_platform -fixed -force -file [file join $root build artifacts gpgpu_no_bit.xsa]}
add_files -fileset sim_1 -norecurse [file join $root tb tb_gpgpu.sv]
set_property top tb_gpgpu [get_filesets sim_1]
set_property include_dirs [concat $gpu_includes [file join $root tb]] [get_filesets sim_1]
update_compile_order -fileset sim_1
launch_simulation -scripts_only
# Force the complete synthesis source set after generating simulation scripts.
set_property source_mgmt_mode None [current_project]
reorder_files -front [list [get_files *VX_gpu_pkg.sv] [get_files *VX_fpu_pkg.sv] [get_files *VX_tcu_pkg.sv]]
set_property STEPS.SYNTH_DESIGN.ARGS.FLATTEN_HIERARCHY none [get_runs synth_1]
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $root scripts post_synth.tcl] [get_runs impl_1]
synth_design -rtl -name rtl_1 -top top -part xczu11eg-ffvc1156-2-i
set fp [open [file join $root evidence hierarchy.txt] w]
foreach c [get_cells -hierarchical -filter {REF_NAME =~ VX_core* || REF_NAME =~ VX_cache* || REF_NAME =~ Vortex* || REF_NAME =~ VX_tcu* || REF_NAME =~ VX_tex* || REF_NAME =~ gpgpu_tex*}] {puts $fp "$c : [get_property REF_NAME $c]"}
close $fp
if {[llength [get_cells -hierarchical -filter {REF_NAME =~ VX_core*}]] != 4} {error "Not exactly four Vortex cores"}
puts "FULL_GPU_RTL_ELABORATION_PASS"
close_project
