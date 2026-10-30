#define main original_graph_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test_graph.cpp"
#pragma GCC diagnostic pop
#undef main
int main(int argc,char **argv){
 assert(argc==4);fixture old(argv[1],argv[2],argv[3]);old.ctx.diagnostics=true;
 assert(!mosaic_run_graph(&old.ctx)&&old.callbacks==16);
 for(unsigned batch: {4u,16u,64u,256u}){
  fixture split(argv[1],argv[2],argv[3]);split.ctx.diagnostics=true;split.ctx.batch_items=batch;
  assert(!mosaic_run_graph(&split.ctx)&&split.callbacks==16);
  assert(split.ctx.tiles==old.ctx.tiles&&split.ctx.quantized_elements==old.ctx.quantized_elements);
  assert(!memcmp(split.fp[0].data(),old.fp[0].data(),288*4));
  assert(split.quant.size()==old.quant.size()&&!memcmp(split.quant.data(),old.quant.data(),split.quant.size()*2));
 }
 puts("MOSAIC_16_LAYER_REAL_HOST_MATH_BATCH_PROPAGATION_PASS_NOT_BOARD_GPU_RAW_CYCLES_NULL");return 0;
}
