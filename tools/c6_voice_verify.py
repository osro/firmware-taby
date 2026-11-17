"""Check lightweight voice timing and presentation ownership on one C6 USB session."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
from c6_verify import Session,require

ROOT=Path(__file__).resolve().parents[1]
LETTERS='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_'

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',required=True);p.add_argument('--output',type=Path,default=ROOT/'.local/c6-voice-verification.json');args=p.parse_args()
 s=Session(args.port);report={'timestamp_utc':datetime.now(timezone.utc).isoformat(),'visual_verification':'requires_owner_feedback'}
 try:
  require(s.query('PING','TABY:')=='PONG','USB ping')
  s.query('TRANSPORT_MODE wifi','TABY:OK ');s.query('TRANSPORT_MODE ble','TABY:OK ')
  report['initial']=s.diag();report['visemes']=[]
  for viseme in range(15):
   run=(LETTERS[viseme]+'_A')*60
   require(s.query(f'VOICE_MOTION {100+viseme} 0 0 {run}','TABY:OK ')=='VOICE_MOTION','Voice acknowledgement')
   time.sleep(.16);d=s.diag()
   require(d['voice_active'] and d['viseme']==viseme and d['jaw_open']==100,'Voice snapshot mismatch')
   require(d['voice_frames']>report['initial']['voice_frames'],'Voice rendering stalled')
   report['visemes'].append({'viseme':viseme,'rendered':True})
  require(s.query('VOICE_MOTION 500 0 120000 K_A','TABY:ERR ')=='voice_motion_invalid','Overrun accepted')
  require(s.diag()['voice_active'],'Invalid command erased valid voice')
  s.query('VOICE_MOTION 999 stop','TABY:OK ');require(s.diag()['voice_active'],'Wrong-talk stop cancelled voice')
  s.query('VOICE_MOTION 114 stop','TABY:OK ');require(not s.diag()['voice_active'],'Matching stop failed')
  s.query('VOICE_MOTION 700 0 119967 K_A','TABY:OK ');time.sleep(.1)
  report['full_timeline']=s.diag()
  require(report['full_timeline']['voice_active'],'Maximum voice timeline failed')
  s.query('VOICE_MOTION 700 stop','TABY:OK ')
  report['after_timeline_stop']=s.diag()
  require(report['after_timeline_stop']['free_heap']>=report['full_timeline']['free_heap']+8192,'Voice timeline memory was not released')
  s.query('VOICE_MOTION 600 0 0 '+'K_A'*15,'TABY:OK ');time.sleep(.7)
  require(not s.diag()['voice_active'] and s.diag()['state']=='IDLE','Natural completion failed')
  s.query('VOICE_MOTION 601 0 0 '+'K_A'*60,'TABY:OK ')
  s.query('UI/text:VOICE TEST|Screen card replaces speech','TABY:OK ')
  require(not s.diag()['voice_active'] and s.diag()['state'].startswith('UI_'),'Card failed to cancel voice')
  s.query('VOICE_MOTION 603 0 0 '+'K_A'*60,'TABY:OK ')
  s.query('UI/timer:FOCUS|1|1||run|1','TABY:OK ')
  timer=s.diag()
  require(not timer['voice_active'] and timer['state'].startswith('UI_'),'Timed card failed to cancel voice')
  time.sleep(1.3);resumed=s.diag()
  require(not resumed['voice_active'] and resumed['state']=='IDLE' and resumed['animation']=='idle_01_loop','Timed card failed to restore idle')
  require(resumed['rendered_frames']>timer['rendered_frames'],'Animation stalled after timed card expiry')
  report['voice_timed_card_expiry']={'card':timer,'resumed':resumed,'passed':True}
  s.query('VOICE_MOTION 602 0 0 '+'K_A'*60,'TABY:OK ')
  s.query('idle_01_loop','TABY:OK ');require(not s.diag()['voice_active'],'Animation failed to cancel voice')
  s.query('CLEAR','TABY:OK ');s.query('TRANSPORT_MODE usb','TABY:OK ')
  report['final']=s.diag()
  require(all(report['final'][k]==0 for k in ('decode_errors','display_errors','touch_errors')),'Rendering/input error')
  report['passed']=True
 except Exception as e:report.update(passed=False,error=str(e));raise
 finally:
  s.connection.close();args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
