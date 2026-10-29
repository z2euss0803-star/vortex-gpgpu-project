set root [file normalize [file join [file dirname [info script]] ..]]
set artifacts [file join $root build artifacts]
open_project [file join $root build vivado gpgpu_soc.xpr]
source [file join $root config sources.tcl]
set_property verilog_define $gpu_defines [get_filesets sources_1]
set_property include_dirs $gpu_includes [get_filesets sources_1]
set_param general.maxThreads 2
set_param synth.maxThreads 2
puts "BUILD_THREAD_LIMIT general=[get_param general.maxThreads] synth=[get_param synth.maxThreads]"
set_property STEPS.SYNTH_DESIGN.ARGS.FLATTEN_HIERARCHY none [get_runs synth_1]
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $root scripts post_synth.tcl] [get_runs impl_1]
# The old automatic hierarchy parser prunes synthesis-only async RAM helpers.
set_property source_mgmt_mode None [current_project]
reorder_files -front [list [get_files *VX_gpu_pkg.sv] [get_files *VX_fpu_pkg.sv] [get_files *VX_tcu_pkg.sv]]
synth_design -top top -part xczu11eg-ffvc1156-2-i -flatten_hierarchy none
write_checkpoint -force [file join $artifacts top_before_ram_fix.dcp]
report_utilization -file [file join $root evidence utilization_before_ram_fix.rpt]
# Reopen the saved design before debug-core implementation (Vivado 2020.1).
# This matches the checkpoint-resume flow already used for this project.
close_design
open_checkpoint [file join $artifacts top_before_ram_fix.dcp]
source [file join $root scripts post_synth.tcl]
write_checkpoint -force [file join $artifacts top_synth.dcp]
report_utilization -file [file join $root evidence utilization_synth.rpt]
report_utilization -hierarchical -hierarchical_depth 15 -file [file join $root evidence utilization_synth_hier.rpt]
puts "RESOURCE_SYNTHESIS_REPORTED"
if {[info exists ::env(GPGPU_SYNTH_ONLY)] && $::env(GPGPU_SYNTH_ONLY) == 1} {close_project; exit}
source [file join $root scripts finish_hardware.tcl]
