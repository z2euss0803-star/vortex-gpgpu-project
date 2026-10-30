# Complete physical IP products only. Never request all/simulation targets.
set root [file normalize [file join $::env(S8_PHYSICAL_ROOT) candidate]]
set run [file normalize $::env(S8_RUN_DIR)]
set_param general.maxThreads 1
open_project [file join $root build vivado gpgpu_soc.xpr]
set audit [open [file join $run reports implementation_inputs.txt] w]
puts $audit "VIVADO_VERSION=[version -short]"
set bd [get_files ps_bd.bd]
if {[llength $bd] != 1} {error "Expected exactly one frozen W4 block design"}
set sources [list $bd]
foreach ip [get_ips {ctrl_axi_converter xil_*}] {lappend sources [get_files [get_property IP_FILE $ip]]}
foreach f $sources {
 puts $audit "SOURCE=$f"
 set targets [list_targets $f]
 puts $audit "AVAILABLE_TARGETS=$targets"
 if {$f eq $bd && [lsearch -exact $targets implementation] < 0} {error "Required BD implementation target not advertised by this tool"}
 report_property -all $f -file [file join $run reports [file tail $f].properties.txt]
 if {[lsearch -exact $targets implementation] >= 0} {
  puts $audit "GENERATE_IMPLEMENTATION_TARGET=$f"
  flush $audit
  generate_target implementation $f
  if {$f eq $bd} {puts $audit "BD_IMPLEMENTATION_TARGET_GENERATED=1"}
 }
}
report_ip_status -file [file join $run reports ip_status.rpt]
foreach f [get_files -all -quiet *.xdc] {
 puts $audit "CONSTRAINT=$f EXISTS=[file exists $f]"
 if {![file exists $f]} {error "Missing implementation constraint: $f"}
 foreach property {USED_IN_SYNTHESIS USED_IN_IMPLEMENTATION PROCESSING_ORDER SCOPED_TO_CELLS SCOPED_TO_REF} {
  if {[lsearch -exact [list_property $f] $property] >= 0} {puts $audit "$property=[get_property $property $f]"}
 }
}
puts $audit "IMPLEMENTATION_INPUTS_READY=1"
close $audit
close_project
puts "S8_W4_IMPLEMENTATION_INPUTS_COMPLETE"
