# Load the pinned upstream algorithm without its immediate invocation. Only
# its three object lookup helpers are replaced: rewiring semantics stay intact.
set patch_file [file join $soc_root vendor vortex hw scripts xilinx_async_bram_patch.tcl]
set fd [open $patch_file r]
fconfigure $fd -encoding utf-8
set original_script [read $fd]
close $fd
set substitutions [regsub -line {^vortex::resolve_async_brams$} $original_script {} definitions]
if {$substitutions != 1} {error "Expected one upstream resolver invocation"}
eval $definitions

namespace eval vortex {
proc scoped_objects {cell kind} {
  set previous [current_instance]
  current_instance $cell
  set code [catch {
    if {$kind eq "nets"} {set objects [get_nets -quiet *]} else {set objects [get_cells -quiet -hierarchical *]}
  } result options]
  current_instance $previous
  if {$code} {return -options $options $result}
  return $objects
}
proc find_cell_descendants {parent_cell} {
  return [scoped_objects $parent_cell cells]
}
proc find_cell_nets {cell name_match {should_exist 1}} {
  set matching_nets {}
  foreach net [scoped_objects $cell nets] {
    # Assert the scoped query has the exact parent semantics of upstream.
    if {[get_property PARENT_CELL $net] ne $cell} {error "Unexpected scoped net parent: $net"}
    if {[regexp $name_match [get_property NAME $net]]} {lappend matching_nets $net}
  }
  if {[llength $matching_nets]==0} {print_error "No matching net found for '$cell' matching '$name_match'." $should_exist}
  return $matching_nets
}
proc get_cell_net {cell name} {
  set found {}
  foreach net [scoped_objects $cell nets] {
    if {[get_property NAME $net] eq $name} {lappend found $net}
  }
  if {[llength $found]!=1} {error "Expected exactly one net '$name' in '$cell'"}
  return [lindex $found 0]
}
}
puts "SCOPED_RAM_LOOKUP: upstream rewiring with parent-scoped object queries"
vortex::resolve_async_brams
