# Run under XSCT 2020.1 after exporting this project's XSA.
# Usage: xsct build_dual_software.tcl <matching.xsa> ?fresh_workspace?
set root [file normalize [file join [file dirname [info script]] ..]]
if {$argc < 1 || $argc > 2} {
    error "usage: xsct build_dual_software.tcl <this-project.xsa> ?fresh_workspace?"
}
set xsa [file normalize [lindex $argv 0]]
if {![file isfile $xsa]} { error "XSA missing: $xsa" }
set workspace [file join $root build vitis_dual]
if {$argc == 2} { set workspace [file normalize [lindex $argv 1]] }
if {[file exists [file join $workspace dual_platform]]} {
    error "Platform already exists; select a fresh workspace: $workspace"
}
file mkdir $workspace
setws $workspace
platform create -name dual_platform -hw $xsa -proc psu_cortexa53_0 -os standalone -out $workspace
platform write
platform generate

set bsp [file join $workspace dual_platform psu_cortexa53_0 standalone_domain bsp psu_cortexa53_0]
set params [file join $bsp include xparameters.h]
set lib [file join $bsp lib libxil.a]
if {![file isfile $params] || ![file isfile $lib]} {
    error "Matching A53 standalone BSP was not generated under $bsp"
}
set h [open $params r]
set parameter_text [read $h]
close $h
foreach address {0xA0000000 0xB0010000 0xB0020000} {
    if {[string first [string tolower $address] [string tolower $parameter_text]] < 0} {
        error "XSA/BSP lacks required HPM address $address"
    }
}

# Keep the fixed-address Vortex kernel in sync with the source hex.
set h [open [file join $root kernel vecadd16.hex] r]
set hex_text [read $h]
close $h
set h [open [file join $root sw vecadd16.h] r]
set header [read $h]
close $h
set hexwords {}
foreach word [split [string trim $hex_text] "\n"] {
    set word [string tolower [string trim $word]]
    if {![regexp {^[0-9a-f]{8}$} $word]} { error "Invalid kernel hex word: $word" }
    lappend hexwords $word
}
set embedded {}
foreach token [regexp -all -inline -nocase {0x[0-9a-f]{8}u} $header] {
    lappend embedded [string tolower [string range $token 2 9]]
}
if {[llength $hexwords] != 22 || [llength $embedded] != 22 || $embedded ne $hexwords} {
    error "Embedded kernel sequence differs from 22 ordered words in kernel/vecadd16.hex"
}

set gcc [auto_execok aarch64-none-elf-gcc]
set readelf [auto_execok aarch64-none-elf-readelf]
if {$gcc eq "" || $readelf eq ""} {
    error "Vitis 2020.1 AArch64 GNU tools are not on PATH; run from its XSCT environment"
}
set out [file join $root build dual_sw]
file mkdir $out
set obj [file join $out main.o]
set elf [file join $out dual_ddr_host.elf]
set map [file join $out dual_ddr_host.map]
set source [file join $root sw main.c]
set linker [file join $root sw lscript.ld]
exec $gcc -std=c11 -Wall -Wextra -Werror -O0 -g3 \
    -I[file join $bsp include] -I[file join $root sw] \
    -c $source -o $obj
exec $gcc -Wl,-T,$linker -Wl,-Map,$map -L[file join $bsp lib] \
    -o $elf $obj -Wl,--start-group -lxil -lgcc -lc -Wl,--end-group
set headers [exec $readelf -h -l $elf]
if {[string first "AArch64" $headers] < 0} { error "ELF machine is not AArch64" }
set h [open [file join $out elf_headers.txt] w]
puts $h $headers
close $h
proc sha256_file {path} {
    set result [exec certutil -hashfile $path SHA256]
    if {![regexp -nocase {[0-9a-f]{64}} $result digest]} {
        error "Could not parse SHA-256 for $path"
    }
    return [string tolower $digest]
}
set manifest [file join $out software_manifest.sha256]
set h [open $manifest w]
foreach pair [list \
    [list xsa $xsa] \
    [list kernel_ordered_hex [file join $root kernel vecadd16.hex]] \
    [list embedded_kernel_header [file join $root sw vecadd16.h]] \
    [list main_c $source] \
    [list hardware_header [file join $root sw gpgpu_hw.h]] \
    [list linker_script $linker] \
    [list bsp_parameters $params] \
    [list bsp_library $lib] \
    [list a53_elf $elf]] {
    lassign $pair label path
    puts $h "[sha256_file $path]  $label  $path"
}
close $h
puts "DUAL_A53_ELF_BUILD_PASS: $elf"
puts "XSA: $xsa"
puts "SHA-256 manifest: $manifest"
puts "Build only; no bitstream programming or board execution was performed."
