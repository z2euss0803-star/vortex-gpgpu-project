# XSCT 2020.1's Windows launcher can return zero after Tcl failure. Emit an
# invocation-specific completion result in addition to its native exit code.
if {$argc < 2} {error "Expected completion token and source script"}
set guard_token [lindex $argv 0]
set guard_source [lindex $argv 1]
set argv [lrange $argv 2 end]
set argc [llength $argv]
set guard_code [catch {source $guard_source} guard_result guard_options]
if {$guard_code != 0} {
  puts stderr "XSCT_GUARDED_ERROR: $guard_result"
  if {[dict exists $guard_options -errorinfo]} {puts stderr [dict get $guard_options -errorinfo]}
}
puts "XSCT_GUARDED_RESULT $guard_token $guard_code"
exit [expr {$guard_code == 0 ? 0 : 1}]
