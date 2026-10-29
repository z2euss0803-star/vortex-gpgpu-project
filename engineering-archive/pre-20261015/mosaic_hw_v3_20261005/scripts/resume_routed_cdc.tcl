# Preserve the failed first route and re-evaluate unchanged routing with the
# missing first-stage calibration CDC constraint. No synthesis/place/route.
set root [file normalize [file join [file dirname [info script]] ..]]
set artifacts [file join $root build artifacts]
set_param general.maxThreads 4
open_project [file join $root build vivado gpgpu_soc.xpr]
set cdc_file [file join $root board calib_cdc.xdc]
if {![llength [get_files -quiet $cdc_file]]} {add_files -fileset constrs_1 -norecurse $cdc_file}
set_property USED_IN_SYNTHESIS false [get_files $cdc_file]
set_property PROCESSING_ORDER LATE [get_files $cdc_file]
open_checkpoint [file join $artifacts top_routed.dcp]
read_xdc $cdc_file
source [file join $root scripts export_routed.tcl]
