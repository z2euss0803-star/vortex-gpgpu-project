# Upstream Vortex flow resolves deliberate BRAM placeholders before opt_design.
set soc_root [file normalize [file join [file dirname [info script]] ..]]
source [file join $soc_root scripts async_bram_scoped.tcl]
# DDR4's calibration XSDB endpoint makes Vivado insert a Debug Hub even with
# Debug_Signal=Disable. The tool initially represents it as a black box and
# normally implements it in opt_design. Resolve it explicitly before our
# strict all-black-boxes check; do not exempt unresolved hardware.
set hubs [get_debug_cores -quiet dbg_hub]
if {[llength $hubs]} {
    puts "IMPLEMENTING_DDR_CALIBRATION_DEBUG_HUB: $hubs"
    implement_debug_core $hubs
}
set unresolved [get_cells -quiet -hierarchical -filter {IS_BLACKBOX == 1}]
if {[llength $unresolved]} {error "Unresolved hardware black boxes after Vortex RAM fix: $unresolved"}
puts "VORTEX_RAM_PLACEHOLDERS_RESOLVED"
