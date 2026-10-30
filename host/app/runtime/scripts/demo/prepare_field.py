"""Produce proposed checks only. No SSH, serial, physical-media or reset API."""
import argparse,json,pathlib
from scripts.demo.verify_release import verify_release
def prepare_field(release, output):
    verified=verify_release(release);states=verified['states'];out=pathlib.Path(output);out.mkdir(parents=True,exist_ok=False)
    if verified['state']=='VERIFIED_SOURCE_BOUND_IMAGE_NOT_BOARD':
        result=dict(state='READY_FOR_FUTURE_REVIEW_NOT_SD_WRITTEN',missing=[],board='NOT_RUN',sd='NOT_WRITTEN',proposed_only=True,states=states)
        (out/'readiness.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        (out/'FIELD_CHECKLIST.md').write_text('# 后续现场清单（仅文本）\n\n交付与镜像已离线审计，不代表板端、DP或性能通过。此脚本没有连接网络、操作介质或启动计算。\n\n1. 按有效会话授权识别SD、备份当前三件套及恢复清单，写后回读核验；不格式化、不改分区。\n2. 用户启动后核对build-id、地址、可信串口SSH公钥及完整运行时绑定。\n3. 单独复验历史SUBMIT_ENTER错误，再执行S8整数用例；旧失败与127273原始周期保留。\n4. Mosaic先验证正式1280×960输入、逐层原阈值和INT16执行证据；不得用256缩略输入代替。\n5. HUNet新CPU先显式diagnostics=true诊断并独立审计，pending numeric-gate不允许默认纯推理；不继承旧CPU现场凭据。\n6. DP先色条、实际发布事件、退出和状态恢复，再验证Mosaic及HUNet切换；提交成功不等于显示成功。\n7. 获有效现场授权后才运行计算和正式重复计时，分别保留GPU原始周期、Linux分项、CPU、传输及误差。首个新增错误封存，不自动复位或重试。\n',encoding='utf-8')
        return result
    missing=['Mosaic T05/T12 numeric acceptance','profile6/program12 and serial adapter integration','full bound RV32/AArch64/kernel cross acceptance','audited matching BOOT.BIN/boot.scr/image.ub','specific SD and board authorization']
    result=dict(state='NOT_READY_FOR_SD',missing=missing,board='NOT_RUN',sd='NOT_WRITTEN',proposed_only=True,states=states)
    (out/'readiness.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    text='# 未来现场清单（仅文本，当前不可写卡）\n\n当前交付为软件部分成果，没有可部署三件套；SD/开发板/DP均 NOT_RUN。此脚本未连接网络或打开物理介质。\n\n'
    text+='未满足条件：\n\n'+''.join('- '+v+'\n' for v in missing)
    text+='\n未来顺序：确认三件套身份与加载区 → 核对实际媒体并备份/恢复 → 获写卡授权 → 用户启动并核对系统/SSH身份 → 原缺SUBMIT_ENTER专项复验 → exact INT16 → DP色条及事件 → 两应用与切换 → 分项性能。每步分别报告；任一新故障封存，不能把提交当显示完成。\n'
    (out/'FIELD_CHECKLIST.md').write_text(text,encoding='utf-8');return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--release',required=True);p.add_argument('--out',required=True);a=p.parse_args();print(json.dumps(prepare_field(a.release,a.out),sort_keys=True))
