# Reuse completed synthesis; only RAM/debug resolution and physical stages rerun.
set root [file normalize [file join [file dirname [info script]] ..]]
set artifacts [file join $root build artifacts]
set_param general.maxThreads 2
set_param synth.maxThreads 2
puts "RESUME_THREAD_LIMIT general=[get_param general.maxThreads] synth=[get_param synth.maxThreads]"
open_project [file join $root build vivado gpgpu_soc.xpr]
open_checkpoint [file join $artifacts top_before_ram_fix.dcp]
source [file join $root scripts post_synth.tcl]
write_checkpoint -force [file join $artifacts top_synth.dcp]
report_utilization -file [file join $root evidence utilization_synth.rpt]
report_utilization -hierarchical -hierarchical_depth 15 -file [file join $root evidence utilization_synth_hier.rpt]
source [file join $root scripts finish_hardware.tcl]
