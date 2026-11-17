import argparse,asyncio,json,sys,time,subprocess,secrets
from datetime import datetime,timezone
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from c6_verify import Session,require
from bleak import BleakScanner,BleakClient
ROOT=Path(__file__).resolve().parents[1]
SERVICE='54414259-0000-4000-a000-000000000001'
CMD='54414259-0000-4000-a000-000000000002'
EVENT='54414259-0000-4000-a000-000000000003'
STATE='54414259-0000-4000-a000-000000000004'
POWER='54414259-0000-4000-a000-000000000005'
async def main():
 p=argparse.ArgumentParser();p.add_argument('--port',required=True);p.add_argument('--skip-profile-restart',action='store_true',help='Preserve an initialized SD card while checking radios');p.add_argument('--output',type=Path,default=ROOT/'.local/c6-network-verification.json');args=p.parse_args()
 s=Session(args.port);original_brightness=None;temporary_ssid=None;r={'timestamp_utc':datetime.now(timezone.utc).isoformat()}
 try:
  require(s.query('PING','TABY:')=='PONG','USB ping')
  info=json.loads(s.query('INFO','TABY:INFO '));original_brightness=info['brightness_percent'];r['usb_initial']={k:v for k,v in info.items() if k not in ('device_id','wifi_ssid','ip')}
  r['initial_diag']=s.diag()
  require(s.query('TRANSPORT_MODE wifi','TABY:OK ')=='TRANSPORT_MODE','WiFi activation')
  setup=json.loads(s.query('SETUP_INFO','TABY:SETUP '))
  path=ROOT/'.local/c6-private-setup.json';path.write_text(json.dumps(setup));path.chmod(0o600)
  r['setup']={'ap_active':bool(setup['setup_ap']['host']),'protected':len(setup['setup_ap']['password'])>=8}
  r['wifi_diag']=s.diag()
  nearby=json.loads(s.query('WIFI_NEARBY','TABY:WIFI_NEARBY '));r['nearby_network_count']=len(nearby['networks'])
  require(s.query('TRANSPORT_MODE ble','TABY:OK ')=='TRANSPORT_MODE','BLE activation')
  await asyncio.sleep(2)
  public=json.loads(s.query('INFO','TABY:INFO '));r['radios']={k:public[k] for k in ('wifi_started','setup_ap_active','ble_started','ble_advertising','ble_connected')};r['both_diag']=s.diag()
  expected='Taby-'+info['device_id'][-8:]
  dev=await BleakScanner.find_device_by_filter(lambda d,a:d.name==expected and SERVICE in a.service_uuids,timeout=15)
  require(dev is not None,'Target BLE advertisement not found');r['ble_advertisement']=True
  for round in range(2):
   async with BleakClient(dev,timeout=15) as client:
    require(client.is_connected,'BLE connection');events=[]
    await client.start_notify(EVENT,lambda _,v:events.append(bytes(v).decode(errors='replace')))
    async def command(text,prefix):
     events.clear();await client.write_gatt_char(CMD,text.encode(),response=True)
     for _ in range(100):
      await asyncio.sleep(.05)
      if any(v.startswith(prefix) for v in events):return next(v for v in events if v.startswith(prefix))
     readable=bytes(await client.read_gatt_char(EVENT)).decode()
     require(readable.startswith(prefix),'BLE reply missing for '+text);return readable
    require(await command('PING','PONG')=='PONG','BLE PING')
    await command('BRIGHTNESS 61','BRIGHTNESS ')
    require(json.loads(s.query('BRIGHTNESS','TABY:BRIGHTNESS '))['percent']==61,'Cross transport setting')
    await command('UI/text:BLUETOOTH|Connected','OK ')
    await asyncio.sleep(.6)
    state=bytes(await client.read_gatt_char(STATE)).decode();require(state.startswith('UI_'),'BLE state cache')
    power=json.loads(bytes(await client.read_gatt_char(POWER)).decode());require('battery_percent' in power,'BLE power')
    await command('CLEAR','OK ')
    r.setdefault('ble_rounds',[]).append({'round':round,'ping':True,'cross_transport_brightness':True,'state_cache':True,'power_cache':True,'mtu':client.mtu_size})
   await asyncio.sleep(.7)
  s.query(f'BRIGHTNESS {original_brightness}','TABY:BRIGHTNESS ');s.query('TRANSPORT_MODE usb','TABY:OK ')
  profiles=json.loads(s.query('WIFI_NETWORKS','TABY:WIFI_NETWORKS '))['networks']
  if not profiles and not args.skip_profile_restart:
   temporary_ssid='Taby-Test-'+secrets.token_hex(6)
   payload=json.dumps({'ssid':temporary_ssid,'password':secrets.token_urlsafe(12)},separators=(',',':'))
   s.query('PROVISION '+payload,'TABY:OK ')
   s.connection.close()
   subprocess.run([sys.executable,str(ROOT/'tools/device.py'),'reset','--port',args.port],check=True,capture_output=True)
   s=Session(args.port);s.query('PING','TABY:')
   saved=json.loads(s.query('WIFI_NETWORKS','TABY:WIFI_NETWORKS '))['networks']
   require(len(saved)==1 and saved[0]['ssid']==temporary_ssid and not saved[0]['last_success'],'Network profile persistence')
   s.query('WIFI_FORGET '+temporary_ssid,'TABY:OK ');temporary_ssid=None
   require(not json.loads(s.query('WIFI_NETWORKS','TABY:WIFI_NETWORKS '))['networks'],'Temporary profile cleanup')
   r['profile_persistence_across_hardware_reset']=True
  else:r['profile_persistence_across_hardware_reset']='skipped_preserving_card' if args.skip_profile_restart else 'skipped_existing_profiles'

  r['final_diag']=s.diag();require(r['final_diag']['display_errors']==0 and r['final_diag']['decode_errors']==0,'Display errors with radios')
  r['passed']=True
 except BaseException as e:
  r.update(passed=False,error=type(e).__name__+': '+str(e));raise
 finally:
  try:
   if temporary_ssid:s.query('WIFI_FORGET '+temporary_ssid,'TABY:OK ')
   if original_brightness is not None:s.query(f'BRIGHTNESS {original_brightness}','TABY:BRIGHTNESS ')
   s.query('CLEAR','TABY:OK ')
  except Exception as e:r['restore_error']=type(e).__name__
  s.connection.close();args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(r,indent=2)+'\n')
asyncio.run(main())
