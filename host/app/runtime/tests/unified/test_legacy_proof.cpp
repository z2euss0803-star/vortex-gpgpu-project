#include "scheduler_fixture.hpp"
int main(){
 Scheduler s;auto req=request("opaque",1,WorkKind::LEGACY_EXCLUSIVE);value(s.submit(req),"legacy aggregate admitted");auto w=value(s.next_ready(),"legacy active");auto forged=outcome(w);
 forged.legacy.reset();Completion invented;invented.cookie=11;invented.sequence=22;forged.completion=invented;forged.acknowledged=true;
 require(!s.complete(forged),"legacy aggregate must reject invented v2 sequence and ACK");require(s.stats().active_gpu==1,"invalid retirement cannot release exclusive barrier");
 auto proof=forged;proof.completion.reset();proof.acknowledged=false;proof.legacy.emplace();proof.legacy->device_generation=1;proof.legacy->profile_id=4;proof.legacy->aggregate_complete=true;proof.legacy->quiescent=true;proof.legacy->result_sha=digest(8);proof.legacy->info_sha=digest(9);
 auto undrained=proof;undrained.legacy->quiescent=false;require(!s.complete(undrained),"undrained aggregate stays active");auto unbound=proof;unbound.legacy->result_sha={};require(!s.complete(unbound),"original result digest required");ok(s.complete(proof),"independent legacy retirement without v2 completion");require(!s.stats().active_gpu,"retired barrier released");auto saved=value(s.take_completion(),"legacy proof retained");require(saved.legacy&&saved.legacy->result_sha==digest(8)&&!saved.completion&&!saved.acknowledged,"no invented sequence in outcome");ok(s.shutdown(),"shutdown");puts("U10_LEGACY_INDEPENDENT_AGGREGATE_PROOF_NO_V2_SEQUENCE_PASS_NOT_BOARD");
}
