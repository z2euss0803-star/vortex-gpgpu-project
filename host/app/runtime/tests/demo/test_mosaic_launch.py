"""Host execution of the actual SDK scheduler, not GPU math or RTL simulation."""
import pathlib,tempfile,subprocess,unittest,shutil
ROOT=pathlib.Path(__file__).resolve().parents[2]
class Launch(unittest.TestCase):
    @unittest.skipUnless(shutil.which('gcc'),'actual host C compiler required')
    def test_sdk_places_one_owner_warp_on_each_core(self):
        with tempfile.TemporaryDirectory(dir=str(ROOT/'build/demo')) as d:
            d=pathlib.Path(d)
            (d/'vx_intrinsics.h').write_text('''#ifndef HOST_VX_INTRINSICS_STUB
#define HOST_VX_INTRINSICS_STUB
#include <stdint.h>
extern uintptr_t scratch; extern unsigned core,warp,lane;
#define csr_read(c) scratch
#define csr_write(c,v) (scratch=(uintptr_t)(v))
static inline unsigned vx_num_cores(void){return 4;}
static inline unsigned vx_num_warps(void){return 4;}
static inline unsigned vx_num_threads(void){return 4;}
static inline unsigned vx_core_id(void){return core;}
static inline unsigned vx_warp_id(void){return warp;}
static inline unsigned vx_thread_id(void){return lane;}
static inline void vx_tmc(int m){(void)m;}
static inline void vx_tmc_one(void){}
static inline void vx_tmc_zero(void){}
static inline void vx_wspawn(unsigned n,void(*f)(void)){unsigned saved=warp;for(warp=1;warp<n;warp++)f();warp=saved;}
#endif
''')
            (d/'vx_print.h').write_text('#include <stdio.h>\n#define vx_printf printf\n')
            (d/'check.c').write_text('''#include <assert.h>
#include <vx_spawn.h>
#include "launch.h"
uintptr_t scratch;unsigned core,warp,lane;static unsigned visits[4][4];
static void cb(void *p){(void)p;if(!mosaic_owner_warp(warp))return;assert(blockIdx.x==core&&threadIdx.x==lane);visits[core][lane]++;}
int main(int argc,char **argv){(void)argv;unsigned grid=MOSAIC_LAUNCH_GRID,block=argc>1?4:MOSAIC_LAUNCH_BLOCK;for(core=0;core<4;core++)for(lane=0;lane<4;lane++){warp=0;assert(!vx_spawn_threads(1,&grid,&block,cb,0));}for(unsigned c=0;c<4;c++)for(unsigned l=0;l<4;l++)assert(visits[c][l]==1);puts("ACTUAL_SDK_FOUR_OWNER_WARPS_HOST_SCHEDULE_NOT_GPU");}
''')
            sdk=ROOT/'vendor/hunet/sdk-linux-crt/kernel'
            cmd=['gcc','-std=c11','-O2','-D__thread=','-I'+str(d),'-I'+str(ROOT/'device/mosaic_s8'),'-I'+str(sdk/'include'),str(d/'check.c'),str(sdk/'src/vx_spawn.c'),'-o',str(d/'check')]
            q=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);self.assertEqual(q.returncode,0,q.stdout.decode())
            q=subprocess.run([str(d/'check')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);self.assertEqual(q.returncode,0,q.stdout.decode())
            self.assertIn(b'ACTUAL_SDK_FOUR_OWNER_WARPS',q.stdout)
            q=subprocess.run([str(d/'check'),'old-block4'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);self.assertNotEqual(q.returncode,0)
