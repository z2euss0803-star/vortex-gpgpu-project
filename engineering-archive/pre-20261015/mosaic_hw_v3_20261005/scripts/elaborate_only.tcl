set root [file normalize [file join [file dirname [info script]] ..]]
open_project [file join $root build vivado gpgpu_soc.xpr]
set_property source_mgmt_mode None [current_project]
reorder_files -front [list [get_files *VX_gpu_pkg.sv] [get_files *VX_fpu_pkg.sv]]
set fp [open [file join $root evidence synth_compile_order.txt] w]
puts $fp [join [get_files -compile_order sources -used_in synthesis] \n]
close $fp
report_property [get_files *VX_async_ram_patch.sv]
synth_design -rtl -name rtl_1 -top top -part xczu11eg-ffvc1156-2-i
set fp [open [file join $root evidence hierarchy.txt] w]
foreach c [get_cells -hierarchical -filter {REF_NAME =~ VX_core* || REF_NAME =~ VX_cache* || REF_NAME =~ Vortex*}] {puts $fp "$c : [get_property REF_NAME $c]"}
close $fp
puts "FULL_GPU_RTL_ELABORATION_PASS"
close_project
