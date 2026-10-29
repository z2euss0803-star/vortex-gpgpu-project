set root [file normalize [file join [file dirname [info script]] ..]]
open_project [file join $root build vivado gpgpu_soc.xpr]
open_bd_design [get_files ps_bd.bd]
set actual [get_property CONFIG.FREQ_HZ [get_bd_pins ps/pl_clk0]]
puts "ACTUAL_PL_CLOCK_HZ=$actual"
set_property CONFIG.FREQ_HZ $actual [get_bd_ports axi_clk]
validate_bd_design
save_bd_design
generate_target all [get_files ps_bd.bd]
write_hw_platform -fixed -force -file [file join $root build artifacts gpgpu_no_bit.xsa]
puts "PLATFORM_CLOCK_CONTRACT_PASS"
close_project
