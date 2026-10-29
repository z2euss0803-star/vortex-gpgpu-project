// Software cache-ownership contract model, NOT an A53 cache microarchitecture.
// Words stay private until flush; external DMA updates stay hidden until invalidate.
reg [31:0] host_cache[longint unsigned];
bit host_dirty[longint unsigned];
int host_reads=0,host_writes=0,host_flushes=0,host_invalidates=0,host_barriers=0;
task automatic host_bus_impl(input int operation,input longint unsigned address,
                        input int unsigned value,output int unsigned reply);
  reg [31:0] data;
  reg [63:0] low,high,key,next_key;
  int exists_,next_exists_;
  reply=0;
  if(operation>=0 && operation<=5 && address[63:40]!=0)
    $fatal(1,"HOST_BRIDGE_ADDRESS_OVERFLOW address=%h",address);
  if(operation>=0 && operation<=3 && address[1:0]!=0)
    $fatal(1,"HOST_BRIDGE_UNALIGNED_WORD address=%h",address);
  case(operation)
    0: begin
      mmread(address[39:0],data);
      if($isunknown(data)) $fatal(1,"HOST_BRIDGE_MMIO_UNKNOWN address=%h",address);
      reply=data;
    end
    1: mmwrite(address[39:0],value);
    2,3: begin
      if(address<64'h10000000 || address>=64'h30000000)
        $fatal(1,"HOST_BRIDGE_PS_RANGE address=%h",address);
      if(operation==3) begin
        host_cache[address]=value;host_dirty[address]=1;host_writes++;
      end else begin
        if(!host_cache.exists(address)) begin
          psread(address[39:0],data);
          if($isunknown(data)) $fatal(1,"HOST_BRIDGE_PS_UNKNOWN address=%h",address);
          host_cache[address]=data;host_dirty[address]=0;
        end
        reply=host_cache[address];host_reads++;
      end
    end
    4,5: begin
      low=address & ~64'd63;high=(address+value+63)&~64'd63;
      if(value==0 || address<64'h10000000 || high>64'h30000000 || high<=low)
        $fatal(1,"HOST_BRIDGE_CACHE_RANGE address=%h bytes=%0d",address,value);
      exists_=host_cache.first(key);
      while(exists_) begin
        // Save the successor before deleting the current associative entry.
        // XSim 2020.1 stops next() traversal if its current key was deleted.
        next_key=key;next_exists_=host_cache.next(next_key);
        if(key>=low && key<high) begin
          if(operation==4 && host_dirty[key]) begin
            pswrite(key[39:0],host_cache[key]);host_dirty[key]=0;
          end
          if(operation==5) begin
            if(host_dirty[key]) $fatal(1,"HOST_BRIDGE_DIRTY_INVALIDATE address=%h",key);
            host_cache.delete(key);host_dirty.delete(key);
          end
        end
        key=next_key;exists_=next_exists_;
      end
      if(operation==4) host_flushes++;else host_invalidates++;
    end
    6: host_barriers++;
    7: phase=value;
    default: $fatal(1,"HOST_BRIDGE_INVALID_OPERATION %0d",operation);
  endcase
endtask

// XSim 2020.1 rejects associative-array first()/next() inside DPI exports.
// Keep the unchanged cache/VIP work in a regular SV process. The exported
// task blocks on scalar request/response state until the same work completes.
bit host_mailbox_busy=0;
bit host_request_toggle=0,host_response_toggle=0;
int host_request_operation;
longint unsigned host_request_address;
int unsigned host_request_value,host_response_value;
task automatic host_bus(input int operation,input longint unsigned address,
                        input int unsigned value,output int unsigned reply);
  if(host_mailbox_busy) $fatal(1,"HOST_BRIDGE_REENTRANT_REQUEST");
  host_mailbox_busy=1;
  host_request_operation=operation;
  host_request_address=address;
  host_request_value=value;
  host_request_toggle=~host_request_toggle;
  wait(host_response_toggle==host_request_toggle);
  reply=host_response_value;
  host_mailbox_busy=0;
endtask
initial begin : host_bus_worker
  forever begin
    wait(host_request_toggle!=host_response_toggle);
    host_bus_impl(host_request_operation,host_request_address,
                  host_request_value,host_response_value);
    host_response_toggle=host_request_toggle;
  end
end
