set root [file normalize [file join [file dirname [info script]] ..]]
set artifacts [file join $root build artifacts]
set_param general.maxThreads 1
open_project [file join $root build vivado gpgpu_soc.xpr]
open_checkpoint [file join $artifacts top_synth.dcp]
source [file join $root scripts finish_hardware.tcl]
