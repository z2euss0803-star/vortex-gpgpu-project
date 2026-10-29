# Shared physical implementation tail; root/artifacts/project are already open.
read_xdc [file join $root board calib_cdc.xdc]
opt_design
write_checkpoint -force [file join $artifacts top_opt.dcp]
report_utilization -file [file join $root evidence utilization_opt.rpt]
report_utilization -hierarchical -hierarchical_depth 12 -file [file join $root evidence utilization_opt_hier.rpt]
place_design
write_checkpoint -force [file join $artifacts top_placed.dcp]
report_utilization -file [file join $root evidence utilization_placed.rpt]
phys_opt_design
route_design
write_checkpoint -force [file join $artifacts top_routed.dcp]
source [file join $root scripts export_routed.tcl]
