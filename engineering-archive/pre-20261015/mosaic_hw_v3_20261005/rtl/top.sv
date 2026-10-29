`timescale 1ns/1ps
// Generated from actual BD ports; DDR pin names match original board XDC.
module top (
  output wire  c0_ddr4_act_n,
  output wire [16:0] c0_ddr4_adr,
  output wire [1:0] c0_ddr4_ba,
  output wire [0:0] c0_ddr4_bg,
  output wire [0:0] c0_ddr4_ck_c,
  output wire [0:0] c0_ddr4_ck_t,
  output wire [0:0] c0_ddr4_cke,
  output wire [0:0] c0_ddr4_cs_n,
  inout wire [7:0] c0_ddr4_dm_dbi_n,
  inout wire [63:0] c0_ddr4_dq,
  inout wire [7:0] c0_ddr4_dqs_c,
  inout wire [7:0] c0_ddr4_dqs_t,
  output wire [0:0] c0_ddr4_odt,
  output wire  c0_ddr4_reset_n,
  input wire  c0_sys_clk_n,
  input wire  c0_sys_clk_p
);
  wire [48:0] HP_araddr;
  wire [1:0] HP_arburst;
  wire [3:0] HP_arcache;
  wire [5:0] HP_arid;
  wire [7:0] HP_arlen;
  wire HP_arlock;
  wire [2:0] HP_arprot;
  wire [3:0] HP_arqos;
  wire HP_arready;
  wire [2:0] HP_arsize;
  wire HP_aruser;
  wire HP_arvalid;
  wire [48:0] HP_awaddr;
  wire [1:0] HP_awburst;
  wire [3:0] HP_awcache;
  wire [5:0] HP_awid;
  wire [7:0] HP_awlen;
  wire HP_awlock;
  wire [2:0] HP_awprot;
  wire [3:0] HP_awqos;
  wire HP_awready;
  wire [2:0] HP_awsize;
  wire HP_awuser;
  wire HP_awvalid;
  wire [5:0] HP_bid;
  wire HP_bready;
  wire [1:0] HP_bresp;
  wire HP_bvalid;
  wire [511:0] HP_rdata;
  wire [5:0] HP_rid;
  wire HP_rlast;
  wire HP_rready;
  wire [1:0] HP_rresp;
  wire HP_rvalid;
  wire [511:0] HP_wdata;
  wire HP_wlast;
  wire HP_wready;
  wire [63:0] HP_wstrb;
  wire HP_wvalid;
  wire [39:0] HPM_araddr;
  wire [1:0] HPM_arburst;
  wire [3:0] HPM_arcache;
  wire [15:0] HPM_arid;
  wire [7:0] HPM_arlen;
  wire HPM_arlock;
  wire [2:0] HPM_arprot;
  wire [3:0] HPM_arqos;
  wire HPM_arready;
  wire [2:0] HPM_arsize;
  wire [15:0] HPM_aruser;
  wire HPM_arvalid;
  wire [39:0] HPM_awaddr;
  wire [1:0] HPM_awburst;
  wire [3:0] HPM_awcache;
  wire [15:0] HPM_awid;
  wire [7:0] HPM_awlen;
  wire HPM_awlock;
  wire [2:0] HPM_awprot;
  wire [3:0] HPM_awqos;
  wire HPM_awready;
  wire [2:0] HPM_awsize;
  wire [15:0] HPM_awuser;
  wire HPM_awvalid;
  wire [15:0] HPM_bid;
  wire HPM_bready;
  wire [1:0] HPM_bresp;
  wire HPM_bvalid;
  wire [31:0] HPM_rdata;
  wire [15:0] HPM_rid;
  wire HPM_rlast;
  wire HPM_rready;
  wire [1:0] HPM_rresp;
  wire HPM_rvalid;
  wire [31:0] HPM_wdata;
  wire HPM_wlast;
  wire HPM_wready;
  wire [3:0] HPM_wstrb;
  wire HPM_wvalid;
  wire pl_clk0;
  wire [0:0] axi_resetn;
  wire calib_complete;
  (* ASYNC_REG = "TRUE" *) reg [1:0] calib_sync;
  always @(posedge pl_clk0 or negedge axi_resetn[0]) begin
    if (!axi_resetn[0]) calib_sync <= 2'b00;
    else calib_sync <= {calib_sync[0], calib_complete};
  end
  wire memory_ready = calib_sync[1];
  ps_bd_wrapper ps (
    .HPM_araddr(HPM_araddr),
    .HPM_arburst(HPM_arburst),
    .HPM_arcache(HPM_arcache),
    .HPM_arid(HPM_arid),
    .HPM_arlen(HPM_arlen),
    .HPM_arlock(HPM_arlock),
    .HPM_arprot(HPM_arprot),
    .HPM_arqos(HPM_arqos),
    .HPM_arready(HPM_arready),
    .HPM_arsize(HPM_arsize),
    .HPM_aruser(HPM_aruser),
    .HPM_arvalid(HPM_arvalid),
    .HPM_awaddr(HPM_awaddr),
    .HPM_awburst(HPM_awburst),
    .HPM_awcache(HPM_awcache),
    .HPM_awid(HPM_awid),
    .HPM_awlen(HPM_awlen),
    .HPM_awlock(HPM_awlock),
    .HPM_awprot(HPM_awprot),
    .HPM_awqos(HPM_awqos),
    .HPM_awready(HPM_awready),
    .HPM_awsize(HPM_awsize),
    .HPM_awuser(HPM_awuser),
    .HPM_awvalid(HPM_awvalid),
    .HPM_bid(HPM_bid),
    .HPM_bready(HPM_bready),
    .HPM_bresp(HPM_bresp),
    .HPM_bvalid(HPM_bvalid),
    .HPM_rdata(HPM_rdata),
    .HPM_rid(HPM_rid),
    .HPM_rlast(HPM_rlast),
    .HPM_rready(HPM_rready),
    .HPM_rresp(HPM_rresp),
    .HPM_rvalid(HPM_rvalid),
    .HPM_wdata(HPM_wdata),
    .HPM_wlast(HPM_wlast),
    .HPM_wready(HPM_wready),
    .HPM_wstrb(HPM_wstrb),
    .HPM_wvalid(HPM_wvalid),
    .HP_araddr(HP_araddr),
    .HP_arburst(HP_arburst),
    .HP_arcache(HP_arcache),
    .HP_arid(HP_arid),
    .HP_arlen(HP_arlen),
    .HP_arlock(HP_arlock),
    .HP_arprot(HP_arprot),
    .HP_arqos(HP_arqos),
    .HP_arready(HP_arready),
    .HP_arsize(HP_arsize),
    .HP_aruser(HP_aruser),
    .HP_arvalid(HP_arvalid),
    .HP_awaddr(HP_awaddr),
    .HP_awburst(HP_awburst),
    .HP_awcache(HP_awcache),
    .HP_awid(HP_awid),
    .HP_awlen(HP_awlen),
    .HP_awlock(HP_awlock),
    .HP_awprot(HP_awprot),
    .HP_awqos(HP_awqos),
    .HP_awready(HP_awready),
    .HP_awsize(HP_awsize),
    .HP_awuser(HP_awuser),
    .HP_awvalid(HP_awvalid),
    .HP_bid(HP_bid),
    .HP_bready(HP_bready),
    .HP_bresp(HP_bresp),
    .HP_bvalid(HP_bvalid),
    .HP_rdata(HP_rdata),
    .HP_rid(HP_rid),
    .HP_rlast(HP_rlast),
    .HP_rready(HP_rready),
    .HP_rresp(HP_rresp),
    .HP_rvalid(HP_rvalid),
    .HP_wdata(HP_wdata),
    .HP_wlast(HP_wlast),
    .HP_wready(HP_wready),
    .HP_wstrb(HP_wstrb),
    .HP_wvalid(HP_wvalid),
    .axi_clk(pl_clk0),
    .axi_resetn(axi_resetn),
    .c0_ddr4_act_n(c0_ddr4_act_n),
    .c0_ddr4_adr(c0_ddr4_adr),
    .c0_ddr4_ba(c0_ddr4_ba),
    .c0_ddr4_bg(c0_ddr4_bg),
    .c0_ddr4_ck_c(c0_ddr4_ck_c),
    .c0_ddr4_ck_t(c0_ddr4_ck_t),
    .c0_ddr4_cke(c0_ddr4_cke),
    .c0_ddr4_cs_n(c0_ddr4_cs_n),
    .c0_ddr4_dm_n(c0_ddr4_dm_dbi_n),
    .c0_ddr4_dq(c0_ddr4_dq),
    .c0_ddr4_dqs_c(c0_ddr4_dqs_c),
    .c0_ddr4_dqs_t(c0_ddr4_dqs_t),
    .c0_ddr4_odt(c0_ddr4_odt),
    .c0_ddr4_reset_n(c0_ddr4_reset_n),
    .c0_sys_clk_clk_n(c0_sys_clk_n),
    .c0_sys_clk_clk_p(c0_sys_clk_p),
    .calib_complete(calib_complete),
    .memory_ready(memory_ready),
    .pl_clk0(pl_clk0)
  );
  gpgpu_subsystem pl (
    .clk(pl_clk0), .resetn(axi_resetn[0]), .memory_ready(memory_ready),
    .HPM_araddr(HPM_araddr),
    .HPM_arburst(HPM_arburst),
    .HPM_arcache(HPM_arcache),
    .HPM_arid(HPM_arid),
    .HPM_arlen(HPM_arlen),
    .HPM_arlock(HPM_arlock),
    .HPM_arprot(HPM_arprot),
    .HPM_arqos(HPM_arqos),
    .HPM_arready(HPM_arready),
    .HPM_arsize(HPM_arsize),
    .HPM_aruser(HPM_aruser),
    .HPM_arvalid(HPM_arvalid),
    .HPM_awaddr(HPM_awaddr),
    .HPM_awburst(HPM_awburst),
    .HPM_awcache(HPM_awcache),
    .HPM_awid(HPM_awid),
    .HPM_awlen(HPM_awlen),
    .HPM_awlock(HPM_awlock),
    .HPM_awprot(HPM_awprot),
    .HPM_awqos(HPM_awqos),
    .HPM_awready(HPM_awready),
    .HPM_awsize(HPM_awsize),
    .HPM_awuser(HPM_awuser),
    .HPM_awvalid(HPM_awvalid),
    .HPM_bid(HPM_bid),
    .HPM_bready(HPM_bready),
    .HPM_bresp(HPM_bresp),
    .HPM_bvalid(HPM_bvalid),
    .HPM_rdata(HPM_rdata),
    .HPM_rid(HPM_rid),
    .HPM_rlast(HPM_rlast),
    .HPM_rready(HPM_rready),
    .HPM_rresp(HPM_rresp),
    .HPM_rvalid(HPM_rvalid),
    .HPM_wdata(HPM_wdata),
    .HPM_wlast(HPM_wlast),
    .HPM_wready(HPM_wready),
    .HPM_wstrb(HPM_wstrb),
    .HPM_wvalid(HPM_wvalid),
    .HP_araddr(HP_araddr),
    .HP_arburst(HP_arburst),
    .HP_arcache(HP_arcache),
    .HP_arid(HP_arid),
    .HP_arlen(HP_arlen),
    .HP_arlock(HP_arlock),
    .HP_arprot(HP_arprot),
    .HP_arqos(HP_arqos),
    .HP_arready(HP_arready),
    .HP_arsize(HP_arsize),
    .HP_aruser(HP_aruser),
    .HP_arvalid(HP_arvalid),
    .HP_awaddr(HP_awaddr),
    .HP_awburst(HP_awburst),
    .HP_awcache(HP_awcache),
    .HP_awid(HP_awid),
    .HP_awlen(HP_awlen),
    .HP_awlock(HP_awlock),
    .HP_awprot(HP_awprot),
    .HP_awqos(HP_awqos),
    .HP_awready(HP_awready),
    .HP_awsize(HP_awsize),
    .HP_awuser(HP_awuser),
    .HP_awvalid(HP_awvalid),
    .HP_bid(HP_bid),
    .HP_bready(HP_bready),
    .HP_bresp(HP_bresp),
    .HP_bvalid(HP_bvalid),
    .HP_rdata(HP_rdata),
    .HP_rid(HP_rid),
    .HP_rlast(HP_rlast),
    .HP_rready(HP_rready),
    .HP_rresp(HP_rresp),
    .HP_rvalid(HP_rvalid),
    .HP_wdata(HP_wdata),
    .HP_wlast(HP_wlast),
    .HP_wready(HP_wready),
    .HP_wstrb(HP_wstrb),
    .HP_wvalid(HP_wvalid)
  );
endmodule
