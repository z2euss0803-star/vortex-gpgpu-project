# IP declarations from pinned upstream xilinx_ip_gen.tcl; see source_manifest.json
create_ip -name floating_point -vendor xilinx.com -library ip -version 7.1 -module_name xil_fdiv -dir ${ip_dir}
set_property -dict [list CONFIG.Component_Name {xil_fdiv} CONFIG.Operation_Type {Divide} CONFIG.Flow_Control {NonBlocking} CONFIG.Has_ACLKEN {true} CONFIG.C_Has_UNDERFLOW {true} CONFIG.C_Has_OVERFLOW {true} CONFIG.C_Has_INVALID_OP {true} CONFIG.C_Has_DIVIDE_BY_ZERO {true} CONFIG.A_Precision_Type {Single} CONFIG.C_A_Exponent_Width {8} CONFIG.C_A_Fraction_Width {24} CONFIG.Result_Precision_Type {Single} CONFIG.C_Result_Exponent_Width {8} CONFIG.C_Result_Fraction_Width {24} CONFIG.C_Mult_Usage {No_Usage} CONFIG.Has_RESULT_TREADY {false} CONFIG.C_Latency {28} CONFIG.C_Rate {1}] [get_ips xil_fdiv]

create_ip -name floating_point -vendor xilinx.com -library ip -version 7.1 -module_name xil_fsqrt -dir ${ip_dir}
set_property -dict [list CONFIG.Component_Name {xil_fsqrt} CONFIG.Operation_Type {Square_root} CONFIG.Flow_Control {NonBlocking} CONFIG.Has_ACLKEN {true} CONFIG.C_Has_INVALID_OP {true} CONFIG.A_Precision_Type {Single} CONFIG.C_A_Exponent_Width {8} CONFIG.C_A_Fraction_Width {24} CONFIG.Result_Precision_Type {Single} CONFIG.C_Result_Exponent_Width {8} CONFIG.C_Result_Fraction_Width {24} CONFIG.C_Mult_Usage {No_Usage} CONFIG.Has_RESULT_TREADY {false} CONFIG.C_Latency {28} CONFIG.C_Rate {1}] [get_ips xil_fsqrt]

create_ip -name floating_point -vendor xilinx.com -library ip -version 7.1 -module_name xil_fma -dir ${ip_dir}
set_property -dict [list CONFIG.Component_Name {xil_fma} CONFIG.Operation_Type {FMA} CONFIG.Add_Sub_Value {Add} CONFIG.Flow_Control {NonBlocking} CONFIG.Has_ACLKEN {true} CONFIG.C_Has_UNDERFLOW {true} CONFIG.C_Has_OVERFLOW {true} CONFIG.C_Has_INVALID_OP {true} CONFIG.Has_A_TUSER {false} CONFIG.A_Precision_Type {Single} CONFIG.C_A_Exponent_Width {8} CONFIG.C_A_Fraction_Width {24} CONFIG.Result_Precision_Type {Single} CONFIG.C_Result_Exponent_Width {8} CONFIG.C_Result_Fraction_Width {24} CONFIG.C_Mult_Usage {Medium_Usage} CONFIG.Has_RESULT_TREADY {false} CONFIG.C_Latency {16} CONFIG.C_Rate {1} CONFIG.A_TUSER_Width {1}] [get_ips xil_fma]

create_ip -name floating_point -vendor xilinx.com -library ip -version 7.1 -module_name xil_fmul -dir ${ip_dir}
set_property -dict [list CONFIG.Operation_Type {Multiply} CONFIG.A_Precision_Type {Single} CONFIG.Result_Precision_Type {Single} CONFIG.Has_RESULT_TREADY {false} CONFIG.Flow_Control {NonBlocking} CONFIG.Has_ACLKEN {true} CONFIG.C_Rate {1} CONFIG.C_Mult_Usage {Full_Usage}] [get_ips xil_fmul]

create_ip -name floating_point -vendor xilinx.com -library ip -version 7.1 -module_name xil_fadd -dir ${ip_dir}
set_property -dict [list CONFIG.Operation_Type {Add_Subtract} CONFIG.A_Precision_Type {Single} CONFIG.Result_Precision_Type {Single} CONFIG.Has_RESULT_TREADY {false} CONFIG.Flow_Control {NonBlocking} CONFIG.Has_ACLKEN {true} CONFIG.C_Rate {1} CONFIG.C_Mult_Usage {Full_Usage}] [get_ips xil_fadd]


set_property CONFIG.C_Latency 8 [get_ips xil_fmul]
set_property CONFIG.C_Latency 11 [get_ips xil_fadd]
