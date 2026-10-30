set root [file normalize [file join $::env(S8_PHYSICAL_ROOT) candidate]]
set run [file normalize $::env(S8_RUN_DIR)]
set artifacts [file join $run artifacts]
open_project [file join $root build vivado gpgpu_soc.xpr]
source [file join $root config sources.tcl]
set_property verilog_define $gpu_defines [get_filesets sources_1]
set_property include_dirs $gpu_includes [get_filesets sources_1]
set_param general.maxThreads 1
set_param synth.maxThreads 1
puts "BUILD_THREAD_LIMIT general=[get_param general.maxThreads] synth=[get_param synth.maxThreads]"
set_property source_mgmt_mode None [current_project]
reorder_files -front [list [get_files *VX_gpu_pkg.sv] [get_files *VX_fpu_pkg.sv] [get_files *VX_tcu_pkg.sv]]
synth_design -top top -part xczu11eg-ffvc1156-2-i -flatten_hierarchy none
write_checkpoint -force [file join $artifacts top_before_ram_fix.dcp]
report_utilization -file [file join $run reports utilization_before_ram_fix.rpt]
close_design
open_checkpoint [file join $artifacts top_before_ram_fix.dcp]
source [file join $root scripts post_synth.tcl]
write_checkpoint -force [file join $artifacts top_synth.dcp]
report_utilization -file [file join $run reports utilization_synth.rpt]
report_utilization -hierarchical -hierarchical_depth 15 -file [file join $run reports utilization_synth_hier.rpt]
report_drc -file [file join $run reports synthesis_drc.rpt]
set audit [open [file join $run reports synthesis_structure.txt] w]
puts $audit "PART=[get_property PART [current_design]]"
puts $audit "BLACKBOX_COUNT=[llength [get_cells -quiet -hierarchical -filter {IS_BLACKBOX == 1}]]"
puts $audit "LATCH_COUNT=[llength [get_cells -quiet -hierarchical -filter {REF_NAME =~ LD*}]]"
puts $audit "NATIVE_S8_INSTANCES=[get_cells -quiet -hierarchical -filter {ORIG_REF_NAME == VX_tcu_native_s8 || REF_NAME == VX_tcu_native_s8}]"
puts $audit "S8_COMPUTE_INSTANCES=[get_cells -quiet -hierarchical -filter {ORIG_REF_NAME == s8_compute || REF_NAME == s8_compute}]"
foreach c [get_cells -quiet -hierarchical -filter {ORIG_REF_NAME == s8_compute || REF_NAME == s8_compute}] {puts $audit "S8_ARRAY_INSTANCE=$c"}
foreach c [get_cells -quiet -hierarchical -filter {REF_NAME == DSP48E2}] {puts $audit "DSP=$c"}
close $audit
close_project
puts "S8_W4_SYNTHESIS_COMPLETE"
