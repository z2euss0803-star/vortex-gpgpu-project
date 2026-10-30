# Exercise stderr preservation and real Tcl failure propagation without hardware.
if {$argc != 1} {error "Expected one exit code"}
puts stderr "XSCT_PROBE_STDERR archive progress"
puts "XSCT_PROBE_STDOUT exit=[lindex $argv 0]"
if {[lindex $argv 0] != 0} {error "INTENTIONAL_XSCT_PROBE_FAILURE"}
