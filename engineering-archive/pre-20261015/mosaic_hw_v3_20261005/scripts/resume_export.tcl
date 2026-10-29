set root [file normalize [file join [file dirname [info script]] ..]]
set artifacts [file join $root build artifacts]
open_project [file join $root build vivado gpgpu_soc.xpr]
source [file join $root scripts export_platform_open.tcl]
puts "HARDWARE_EXPORT_PASS"
close_project
