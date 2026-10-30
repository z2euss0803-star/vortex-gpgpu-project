# Four-core GPU / CDMA / A53 dual-memory fabric. Vivado 2020.1.
set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:zynq_ultra_ps_e:3.3 ps]
source [file join $root board ps_config.tcl]
set_property -dict [list CONFIG.PSU__USE__M_AXI_GP0 {1} CONFIG.PSU__MAXIGP0__DATA_WIDTH {32} CONFIG.PSU__USE__M_AXI_GP1 {1} CONFIG.PSU__MAXIGP1__DATA_WIDTH {128} CONFIG.PSU__USE__M_AXI_GP2 {0} CONFIG.PSU__USE__S_AXI_GP2 {1} CONFIG.PSU__SAXIGP2__DATA_WIDTH {128} CONFIG.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ {100}] $ps
set ddr [create_bd_cell -type ip -vlnv xilinx.com:ip:ddr4:2.2 ddr]
# Physical parameters taken from PARAM_VALUE entries of the original XCI.
set_property -dict [list CONFIG.C0.DDR4_MemoryPart {MT40A512M16HA-083E} CONFIG.C0.DDR4_DataWidth {64} CONFIG.C0.DDR4_TimePeriod {833} CONFIG.C0.DDR4_InputClockPeriod {4998} CONFIG.C0.DDR4_PhyClockRatio {4:1} CONFIG.C0.DDR4_CasLatency {18} CONFIG.C0.DDR4_CasWriteLatency {12} CONFIG.C0.DDR4_DataMask {DM_NO_DBI} CONFIG.C0.DDR4_Ecc {false} CONFIG.C0.DDR4_AxiSelection {true} CONFIG.C0.DDR4_AxiDataWidth {512} CONFIG.C0.DDR4_AxiAddressWidth {32} CONFIG.C0.DDR4_AxiIDWidth {6} CONFIG.C0.BANK_GROUP_WIDTH {1} CONFIG.System_Clock {Differential}] $ddr
make_bd_intf_pins_external [get_bd_intf_pins ddr/C0_DDR4]
set_property name c0_ddr4 [get_bd_intf_ports C0_DDR4_0]
make_bd_intf_pins_external [get_bd_intf_pins ddr/C0_SYS_CLK]
set_property name c0_sys_clk [get_bd_intf_ports C0_SYS_CLK_0]
# The Z11-P differential PL_CLK0 input is 200 MHz; match DDR4 4998 ps reference.
set_property CONFIG.FREQ_HZ {200000000} [get_bd_intf_ports c0_sys_clk]
set cdma [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_cdma:4.1 cdma]
set_property -dict [list CONFIG.C_INCLUDE_SG {0} CONFIG.C_INCLUDE_DRE {0} CONFIG.C_M_AXI_DATA_WIDTH {128} CONFIG.C_ADDR_WIDTH {40} CONFIG.C_M_AXI_MAX_BURST_LEN {64}] $cdma
set gpio [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 status_gpio]
set_property -dict [list CONFIG.C_GPIO_WIDTH {1} CONFIG.C_ALL_INPUTS {1} CONFIG.C_INTERRUPT_PRESENT {0}] $gpio
set sc [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 memory_fabric]
set_property -dict [list CONFIG.NUM_SI {3} CONFIG.NUM_MI {4} CONFIG.NUM_CLKS {2}] $sc
make_bd_intf_pins_external [get_bd_intf_pins ps/M_AXI_HPM0_FPD]
set_property name HPM [get_bd_intf_ports M_AXI_HPM0_FPD_0]
set hp [create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 HP]
set_property -dict [list CONFIG.PROTOCOL {AXI4} CONFIG.ADDR_WIDTH {49} CONFIG.DATA_WIDTH {512} CONFIG.ID_WIDTH {6} CONFIG.HAS_BURST {1} CONFIG.HAS_LOCK {1} CONFIG.HAS_CACHE {1} CONFIG.HAS_PROT {1} CONFIG.HAS_QOS {1} CONFIG.HAS_REGION {0} CONFIG.ARUSER_WIDTH {1} CONFIG.AWUSER_WIDTH {1} CONFIG.NUM_READ_OUTSTANDING {16} CONFIG.NUM_WRITE_OUTSTANDING {16}] $hp
connect_bd_intf_net $hp [get_bd_intf_pins memory_fabric/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins cdma/M_AXI] [get_bd_intf_pins memory_fabric/S01_AXI]
connect_bd_intf_net [get_bd_intf_pins ps/M_AXI_HPM1_FPD] [get_bd_intf_pins memory_fabric/S02_AXI]
connect_bd_intf_net [get_bd_intf_pins memory_fabric/M00_AXI] [get_bd_intf_pins ps/S_AXI_HP0_FPD]
connect_bd_intf_net [get_bd_intf_pins memory_fabric/M01_AXI] [get_bd_intf_pins ddr/C0_DDR4_S_AXI]
connect_bd_intf_net [get_bd_intf_pins memory_fabric/M02_AXI] [get_bd_intf_pins cdma/S_AXI_LITE]
connect_bd_intf_net [get_bd_intf_pins memory_fabric/M03_AXI] [get_bd_intf_pins status_gpio/S_AXI]
set rst [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst]
# ext_reset_in polarity is propagated from PS pl_resetn0; Vivado 2020.1 marks C_EXT_RESET_HIGH read-only.
set rddr [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 rst_ddr]
# Both auxiliary reset inputs are explicitly active-low and held inactive.
# The previous generated IP propagated C_AUX_RESET_HIGH=0 while tied to GND.
set_property CONFIG.C_AUX_RESET_HIGH 0 $rst
set_property CONFIG.C_AUX_RESET_HIGH 0 $rddr
set inv [create_bd_cell -type ip -vlnv xilinx.com:ip:util_vector_logic:2.0 reset_invert]
set_property -dict [list CONFIG.C_OPERATION {not} CONFIG.C_SIZE {1}] $inv
set c1 [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 const_one]
set_property CONFIG.CONST_VAL 1 $c1
set c0 [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 const_zero]
set_property CONFIG.CONST_VAL 0 $c0
set clk [create_bd_port -dir I -type clk axi_clk]
set_property -dict [list CONFIG.FREQ_HZ {100000000} CONFIG.ASSOCIATED_BUSIF {HPM:HP} CONFIG.ASSOCIATED_RESET {axi_resetn}] $clk
connect_bd_net $clk [get_bd_pins ps/maxihpm0_fpd_aclk] [get_bd_pins ps/maxihpm1_fpd_aclk] [get_bd_pins ps/saxihp0_fpd_aclk] [get_bd_pins memory_fabric/aclk] [get_bd_pins cdma/m_axi_aclk] [get_bd_pins cdma/s_axi_lite_aclk] [get_bd_pins status_gpio/s_axi_aclk] [get_bd_pins rst/slowest_sync_clk]
set clkout [create_bd_port -dir O -type clk pl_clk0]
connect_bd_net [get_bd_pins ps/pl_clk0] $clkout
connect_bd_net [get_bd_pins ps/pl_resetn0] [get_bd_pins rst/ext_reset_in] [get_bd_pins reset_invert/Op1]
connect_bd_net [get_bd_pins reset_invert/Res] [get_bd_pins ddr/sys_rst]
connect_bd_net [get_bd_pins const_one/dout] [get_bd_pins rst/dcm_locked] [get_bd_pins rst_ddr/dcm_locked]
connect_bd_net [get_bd_pins const_one/dout] [get_bd_pins rst/aux_reset_in] [get_bd_pins rst_ddr/aux_reset_in]
connect_bd_net [get_bd_pins const_zero/dout] [get_bd_pins rst/mb_debug_sys_rst] [get_bd_pins rst_ddr/mb_debug_sys_rst]
set rn [create_bd_port -dir O -type rst axi_resetn]
# Port polarity propagates from rst/peripheral_aresetn; manual POLARITY write is read-only.
connect_bd_net [get_bd_pins rst/peripheral_aresetn] $rn [get_bd_pins memory_fabric/aresetn] [get_bd_pins cdma/s_axi_lite_aresetn] [get_bd_pins status_gpio/s_axi_aresetn]
connect_bd_net [get_bd_pins ddr/c0_ddr4_ui_clk] [get_bd_pins memory_fabric/aclk1] [get_bd_pins rst_ddr/slowest_sync_clk]
connect_bd_net [get_bd_pins ddr/c0_ddr4_ui_clk_sync_rst] [get_bd_pins rst_ddr/ext_reset_in]
connect_bd_net [get_bd_pins rst_ddr/peripheral_aresetn] [get_bd_pins ddr/c0_ddr4_aresetn]
# Calib is synchronized in top at 100 MHz before use by GPU and status GPIO.
set cal [create_bd_port -dir O calib_complete]
connect_bd_net [get_bd_pins ddr/c0_init_calib_complete] $cal
set ready [create_bd_port -dir I memory_ready]
connect_bd_net $ready [get_bd_pins status_gpio/gpio_io_i]
# No interrupt requirement: software polls CDMA status/error registers.
assign_bd_address -offset 0xA0000000 -range 64K -target_address_space [get_bd_addr_spaces ps/Data] [get_bd_addr_segs HPM/Reg] -force
foreach space [list [get_bd_addr_spaces HP] [get_bd_addr_spaces cdma/Data]] {
    assign_bd_address -offset 0x00000000 -range 2G -target_address_space $space [get_bd_addr_segs ps/SAXIGP2/HP0_DDR_LOW] -force
    assign_bd_address -offset 0x500000000 -range 4G -target_address_space $space [get_bd_addr_segs ddr/C0_DDR4_MEMORY_MAP/C0_DDR4_ADDRESS_BLOCK] -force
}
assign_bd_address -offset 0x500000000 -range 4G -target_address_space [get_bd_addr_spaces ps/Data] [get_bd_addr_segs ddr/C0_DDR4_MEMORY_MAP/C0_DDR4_ADDRESS_BLOCK] -force
assign_bd_address -offset 0xB0010000 -range 64K -target_address_space [get_bd_addr_spaces ps/Data] [get_bd_addr_segs cdma/S_AXI_LITE/Reg] -force
assign_bd_address -offset 0xB0020000 -range 64K -target_address_space [get_bd_addr_spaces ps/Data] [get_bd_addr_segs status_gpio/S_AXI/Reg] -force
# Mark intentionally unreachable decode paths explicitly; PS/Data keeps CDMA/GPIO and PL DDR mappings.
foreach seg {ps/SAXIGP2/HP0_DDR_LOW ps/SAXIGP2/HP0_QSPI ps/SAXIGP2/HP0_PCIE_LOW ps/SAXIGP2/HP0_LPS_OCM ps/SAXIGP2/HP0_DDR_HIGH} {
    exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces ps/Data] [get_bd_addr_segs $seg]
}
foreach space {HP cdma/Data} {
    foreach seg {cdma/S_AXI_LITE/Reg status_gpio/S_AXI/Reg} {
        exclude_bd_addr_seg -target_address_space [get_bd_addr_spaces $space] [get_bd_addr_segs $seg]
    }
}
validate_bd_design
save_bd_design
write_bd_tcl -force [file join $root evidence generated_bd.tcl]
generate_target all [get_files ps_bd.bd]
add_files -norecurse [make_wrapper -files [get_files ps_bd.bd] -top]
set_property generate_synth_checkpoint false [get_files ps_bd.bd]
foreach ip [get_ips {ctrl_axi_converter xil_*}] { set_property generate_synth_checkpoint false [get_files [get_property IP_FILE $ip]] }
report_ip_status -file [file join $root evidence ip_status.rpt]
puts "DUAL_DDR_BD_READY"



