`timescale 1ns/1ps
// Single-lane RGBA8 sampler. Q16.16 coordinates; 4 KiB read-only direct map.
// Descriptor immutable while a request is active; reset invalidates all lines.
module gpgpu_tex_sampler (
 input wire clk,reset,
 input wire[31:0] cfg_base,cfg_width,cfg_height,cfg_pitch,cfg_mode,
 input wire req_valid, output wire req_ready,
 input wire[31:0] req_u,req_v,
 output wire rsp_valid,input wire rsp_ready,
 output reg[31:0] rsp_rgba,output reg rsp_error,
 output wire mem_req_valid,input wire mem_req_ready,
 output wire[25:0] mem_req_addr,
 input wire mem_rsp_valid,output wire mem_rsp_ready,input wire[511:0] mem_rsp_data
);
 localparam IDLE=0,COORD=1,MODULO=2,ADDR=3,LOOKUP=4,CACHE=5,
            REQUEST=6,WAIT_MEM=7,NEXT=8,BLEND=9,RESPONSE=10;
 reg[3:0] state;
 reg signed[17:0] x0,y0;
 wire signed[32:0] u_round=$signed({req_u[31],req_u})+33'sd32768;
 wire signed[32:0] v_round=$signed({req_v[31],req_v})+33'sd32768;
 reg[7:0] fx,fy;reg[1:0] corner;reg[31:0] texel[0:3];
 wire signed[17:0] cx=x0+$signed({17'd0,corner[0]});
 wire signed[17:0] cy=y0+$signed({17'd0,corner[1]});
 reg[17:0] mx,my;reg nx,ny;reg[4:0] bit_index;
 reg[13:0] rx,ry;reg[12:0] ix,iy;
 wire[13:0] rx_shift={rx[12:0],mx[bit_index]};
 wire[13:0] ry_shift={ry[12:0],my[bit_index]};
 wire[13:0] rx_next=rx_shift>={1'b0,cfg_width[12:0]} ? rx_shift-{1'b0,cfg_width[12:0]} : rx_shift;
 wire[13:0] ry_next=ry_shift>={1'b0,cfg_height[12:0]} ? ry_shift-{1'b0,cfg_height[12:0]} : ry_shift;
 reg[31:0] tex_addr;
 reg[63:0] line_valid;
 reg[19:0] line_tag[0:63];
 (* ram_style="block" *) reg[511:0] line_data[0:63];
 reg[511:0] lookup_data;reg lookup_hit;
 wire[5:0] cache_index=tex_addr[11:6];
 wire[3:0] word_index=tex_addr[5:2];
 assign req_ready=!reset && state==IDLE;
 assign rsp_valid=!reset && state==RESPONSE;
 assign mem_req_valid=!reset && state==REQUEST;
 assign mem_req_addr=tex_addr[31:6];
 assign mem_rsp_ready=!reset && state==WAIT_MEM;
 function automatic[7:0] blend_channel(input[7:0] a,b,c,d,input[7:0] x,y);
  reg[8:0] xx,yy,xx0,yy0;reg[31:0] sum;
  begin
   xx={1'b0,x};yy={1'b0,y};xx0=9'd256-xx;yy0=9'd256-yy;
   sum=a*xx0*yy0+b*xx*yy0+c*xx0*yy+d*xx*yy+32'd32768;
   blend_channel=sum[23:16];
  end
 endfunction
 integer ch;
 always @(posedge clk) begin
  if(reset) begin
   state<=IDLE;line_valid<=0;rsp_rgba<=0;rsp_error<=0;
   x0<=0;y0<=0;corner<=0;fx<=0;fy<=0;tex_addr<=0;
   mx<=0;my<=0;nx<=0;ny<=0;bit_index<=0;rx<=0;ry<=0;ix<=0;iy<=0;
   lookup_hit<=0;
  end else case(state)
   IDLE: if(req_valid) begin
    rsp_rgba<=0;rsp_error<=!cfg_mode[3];corner<=0;
    fx<=req_u[15:8];fy<=req_v[15:8];
    x0<=cfg_mode[0] ? $signed({{2{req_u[31]}},req_u[31:16]}) : $signed(u_round>>>16);
    y0<=cfg_mode[0] ? $signed({{2{req_v[31]}},req_v[31:16]}) : $signed(v_round>>>16);
    state<=cfg_mode[3] ? COORD : RESPONSE;
   end
   COORD: begin
    if(cfg_mode[1]) begin mx<=cx<0 ? -cx : cx;nx<=cx<0;end
    else begin mx<=cx<0 ? 0 : cx>=$signed({5'd0,cfg_width[12:0]}) ? cfg_width[12:0]-1'b1 : cx;nx<=0;end
    if(cfg_mode[2]) begin my<=cy<0 ? -cy : cy;ny<=cy<0;end
    else begin my<=cy<0 ? 0 : cy>=$signed({5'd0,cfg_height[12:0]}) ? cfg_height[12:0]-1'b1 : cy;ny<=0;end
    rx<=0;ry<=0;bit_index<=17;state<=MODULO;
   end
   MODULO: begin
    rx<=rx_next;ry<=ry_next;
    if(bit_index==0) begin
     ix<=nx && rx_next!=0 ? cfg_width[12:0]-rx_next[12:0] : rx_next[12:0];
     iy<=ny && ry_next!=0 ? cfg_height[12:0]-ry_next[12:0] : ry_next[12:0];
     state<=ADDR;
    end else bit_index<=bit_index-1'b1;
   end
   ADDR: begin tex_addr<=cfg_base+iy*cfg_pitch+{17'd0,ix,2'b00};state<=LOOKUP;end
   LOOKUP: begin
    lookup_data<=line_data[cache_index];
    lookup_hit<=line_valid[cache_index] && line_tag[cache_index]==tex_addr[31:12];state<=CACHE;
   end
   CACHE: if(lookup_hit) begin texel[corner]<=lookup_data[32*word_index+:32];state<=NEXT;end
          else state<=REQUEST;
   REQUEST: if(mem_req_ready) state<=WAIT_MEM;
   WAIT_MEM: if(mem_rsp_valid) begin
    line_data[cache_index]<=mem_rsp_data;line_tag[cache_index]<=tex_addr[31:12];line_valid[cache_index]<=1;
    texel[corner]<=mem_rsp_data[32*word_index+:32];state<=NEXT;
   end
   NEXT: if(!cfg_mode[0]) begin rsp_rgba<=texel[0];state<=RESPONSE;end
         else if(corner==3) state<=BLEND;
         else begin corner<=corner+1'b1;state<=COORD;end
   BLEND: begin
    for(ch=0;ch<4;ch=ch+1) rsp_rgba[ch*8+:8]<=blend_channel(texel[0][ch*8+:8],texel[1][ch*8+:8],texel[2][ch*8+:8],texel[3][ch*8+:8],fx,fy);
    state<=RESPONSE;
   end
   RESPONSE: if(rsp_ready) state<=IDLE;
   default: state<=IDLE;
  endcase
 end
endmodule
