// Shared by the real SoC monitor and its directed negative regression.
// The reserved fence line is legal only with a real flagged GPU request.
task automatic validate_ps_allocation(
  input reg [63:0] first_byte, last_byte,
  input reg [7:0] burst_len,
  input reg [2:0] beat_size,
  input int flush_issued, flush_accepted
);
  if (first_byte == 64'h10030000) begin
    if (last_byte !== 64'h10030040 || burst_len !== 8'd3 || beat_size !== 3'd4)
      $fatal(1,"LOOPBACK_FAIL FLUSH_BURST_SHAPE");
    if (flush_issued <= flush_accepted)
      $fatal(1,"LOOPBACK_FAIL FLUSH_WITHOUT_FLAGGED_REQUEST");
  end else if (!((first_byte>=64'h10000000 && last_byte<=64'h10000080) ||
                (first_byte>=64'h10010000 && last_byte<=64'h10010040) ||
                (first_byte>=64'h10022000 && last_byte<=64'h10022200) ||
                (first_byte>=64'h10020000 && last_byte<=64'h10020180) ||
                (first_byte>=64'h10021000 && last_byte<=64'h10021180)) ||
               last_byte <= first_byte || $isunknown({first_byte,last_byte}))
    $fatal(1,"LOOPBACK_FAIL GPU_PHASE_PS_READ_OUTSIDE_ALLOCATION first=%h end=%h",first_byte,last_byte);
endtask
