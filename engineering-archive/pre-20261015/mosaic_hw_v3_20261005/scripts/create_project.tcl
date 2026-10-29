set root [file normalize [file join [file dirname [info script]] ..]]
set out [file join $root build vivado]
file mkdir [file join $root evidence]
if {[file exists [file join $out gpgpu_soc.xpr]]} { error "New project already exists" }
create_project gpgpu_soc $out -part xczu11eg-ffvc1156-2-i
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
set_param general.maxThreads 4
source [file join $root config sources.tcl]
set_property verilog_define $gpu_defines [get_filesets sources_1]
set_property include_dirs $gpu_includes [get_filesets sources_1]
set_property verilog_define [concat $gpu_defines SIMULATION] [get_filesets sim_1]
set_property include_dirs $gpu_includes [get_filesets sim_1]
add_files -norecurse $gpu_sources

create_ip -name axi_protocol_converter -vendor xilinx.com -library ip -version 2.1 -module_name ctrl_axi_converter
set_property -dict [list CONFIG.SI_PROTOCOL {AXI4} CONFIG.MI_PROTOCOL {AXI4LITE} CONFIG.ADDR_WIDTH {40} CONFIG.DATA_WIDTH {32} CONFIG.ID_WIDTH {16} CONFIG.TRANSLATION_MODE {2}] [get_ips ctrl_axi_converter]
set ip_dir [file join $out gpgpu_soc.srcs sources_1 ip]
source [file join $root config fpu_ip.tcl]
generate_target all [get_ips]

create_bd_design ps_bd
source [file join $root scripts build_bd.tcl]
close_project
