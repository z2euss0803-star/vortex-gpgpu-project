puts "THREAD_DEFAULT general=[get_param general.maxThreads] synth=[get_param synth.maxThreads]"
set_param general.maxThreads 2
set_param synth.maxThreads 2
if {[get_param general.maxThreads] != 2 || [get_param synth.maxThreads] != 2} {error "Thread limit readback failed"}
puts "THREAD_LIMIT_PROBE_PASS general=2 synth=2"
exit
