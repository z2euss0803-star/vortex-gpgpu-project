# DDR calibration is a level from the independent DDR UI clock. The top-level
# two-FF ASYNC_REG chain samples it in pl_clk0. Exclude only the first D pin;
# stage 0 -> stage 1 and all SmartConnect CDC constraints remain timed.
set calib_first_d [get_pins -quiet {calib_sync_reg[0]/D}]
if {[llength $calib_first_d] != 1} {error "Expected exactly one calibration synchronizer first D pin"}
set_false_path -to $calib_first_d
