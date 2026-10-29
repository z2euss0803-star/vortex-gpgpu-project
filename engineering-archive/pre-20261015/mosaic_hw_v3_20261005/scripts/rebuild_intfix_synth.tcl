# Rebuild the copied project. Original project, RTL and checkpoints stay intact.
set root [file normalize [file join [file dirname [info script]] ..]]
set artifacts [file join $root build artifacts]
set audit [file join $root evidence intfix_synthesis]
file mkdir $audit
set_param general.maxThreads 4
open_project [file join $root build vivado gpgpu_soc.xpr]
set_property source_mgmt_mode None [current_project]
reorder_files -front [list [get_files *VX_gpu_pkg.sv] [get_files *VX_fpu_pkg.sv] [get_files *VX_tcu_pkg.sv]]
set inputs [open [file join $audit source_files.txt] w]
foreach f [get_files -compile_order sources -used_in synthesis] {
    set path [file normalize [get_property NAME $f]]
    puts $inputs $path
    if {[string match *gpgpu_c1w4t4_soc_tcu_tex/* $path]} {error "Original project source still referenced: $path"}
}
close $inputs
set integer_file [get_files *VX_tcu_fedp_int.sv]
if {[llength $integer_file] != 1} {error "Expected one integer FEDP source"}
puts "INTFIX_SYNTH_SOURCE: $integer_file"
synth_design -top top -part xczu11eg-ffvc1156-2-i -flatten_hierarchy none
write_checkpoint -force [file join $artifacts top_before_ram_fix.dcp]
source [file join $root scripts post_synth.tcl]
write_checkpoint -force [file join $artifacts top_synth.dcp]
report_utilization -file [file join $root evidence utilization_synth.rpt]
report_utilization -hierarchical -hierarchical_depth 15 -file [file join $root evidence utilization_synth_hier.rpt]
set fedps [lsort [get_cells -hierarchical -filter {REF_NAME =~ VX_tcu_fedp_int*}]]
if {[llength $fedps] != 4} {error "Expected four integer FEDPs, got [llength $fedps]"}
set i 0
foreach c $fedps {
    write_verilog -force -mode funcsim -cell $c [file join $audit fedp_$i.v]
    incr i
}
puts "INTFIX_SYNTHESIS_COMPLETE: corrected-source checkpoint and four actual leaf netlists"
close_project
