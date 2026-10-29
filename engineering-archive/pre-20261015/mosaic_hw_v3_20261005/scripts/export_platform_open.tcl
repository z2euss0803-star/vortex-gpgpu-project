# Vivado 2020.1 -include_bit requires a managed implementation run, whereas
# this design was implemented from a DCP. Its supported legacy write_sysdef
# command takes an explicit bit file and exports the hardware definition.
# Use the official packager and verify the resulting archive/BSP separately.
set bit [file join $artifacts top.bit]
if {![file isfile $bit] || [file size $bit] < 1000000} {error "Completed top.bit is missing"}
puts "XSA_BIT_SOURCE=$bit"
write_hw_platform -fixed -force -file [file join $artifacts gpgpu_no_bit.xsa]
write_sysdef -force -hwdef [file join $artifacts gpgpu_no_bit.xsa] -bitfile $bit -file [file join $artifacts gpgpu.sysdef]
set python {C:/Users/13151/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe}
puts [exec $python -E [file join $root scripts package_xsa.py]]
