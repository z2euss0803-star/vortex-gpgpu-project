# Vitis/XSCT 2020.1; creates a new matching BSP instead of using an old platform.
set root [file normalize [file join [file dirname [info script]] ..]]
set xsa [file join $root build artifacts gpgpu_with_bit.xsa]
if {![file exists $xsa]} { set xsa [file join $root build artifacts gpgpu_no_bit.xsa] }
if {![file exists $xsa]} { error "Export the new hardware platform first" }
set ws [file join $root build vitis]
if {$argc > 0} { set ws [file normalize [lindex $argv 0]] }
if {[file exists [file join $ws gpgpu_platform]]} { error "Workspace already exists: reuse Vitis GUI or choose a fresh build directory" }
setws $ws
platform create -name gpgpu_platform -hw $xsa -proc psu_cortexa53_0 -os standalone -out $ws
platform write
platform generate
puts [exec D:/PYTHON/python.exe [file join $root scripts build_elf.py] --workspace $ws]
puts "A53_BUILD_PASS: $xsa"
