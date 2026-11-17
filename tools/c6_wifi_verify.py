"""Verify local C6 HTTP routes without changing saved Wi-Fi credentials."""
import argparse
from datetime import datetime, timezone
import ipaddress
import json
from pathlib import Path
import socket
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request,urlopen

ROOT=Path(__file__).resolve().parents[1]
def require(value,message):
 if not value:raise AssertionError(message)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',required=True);p.add_argument('--output',type=Path,default=ROOT/'.local/c6-wifi-verification.json');args=p.parse_args()
 address=ipaddress.ip_address(args.host);require(address.version==4 and address.is_private,'Expected the local board IPv4 address')
 base='http://'+str(address);report={'timestamp_utc':datetime.now(timezone.utc).isoformat(),'checks':[]}
 def get(path,data=None):
  req=Request(base+path,data=data,headers={'Content-Type':'application/json'} if data is not None else {})
  try:
   with urlopen(req,timeout=12) as r:return r.status,r.read(8192).decode(),r.headers
  except HTTPError as e:return e.code,e.read(8192).decode(),e.headers
 def command(text):return get('/cmd?'+urlencode({'c':text}))
 try:
  code,body,_=get('/ping');require(code==200 and body=='!','HTTP ping');report['checks'].append('ping')
  code,body,headers=get('/v1/health');info=json.loads(body);require(code==200 and info['hardware_target']=='c6-lcd-1.47' and info['connected'],'HTTP health/connection')
  require(headers.get('Cache-Control')=='no-store','Health cache policy');report['checks'].append('health')
  report['asset_storage']=info['asset_storage'];report['animation_count']=info['animation_count']
  for route in ('/state','/touch','/v1/reusable/choice-signal'):
   code,body,_=get(route);require(code==200 and body.strip(),'Read endpoint failed');report['checks'].append(route)
  code,body,_=command('ANIMATIONS');require(code==200 and body.count('TABY:ANIMATION ')==info['animation_count'] and body.endswith('TABY:ANIMATIONS_END'),'Queued animation catalog')
  report['catalog_response_bytes']=len(body.encode());report['checks'].append('full_queued_catalog')
  code,body,_=command('PING');require(code==200 and body=='TABY:PONG','HTTP command dispatch')
  for path in ('/cmd?c=%00','/cmd?c=PING%0aCLEAR','/cmd?c=PING&c=CLEAR','/cmd?c=%zz'):
   code,_,_=get(path);require(code==400,'Malformed request accepted')
  report['checks'].append('malformed_uri_rejection')
  _,before,_=command('WIFI_NETWORKS')
  for data in (json.dumps({'ssid':'','password':''},indent=2).encode(),b'{"ssid":"one\\u0000two","password":"12345678"}',b'['*9+b'0'+b']'*9):
   code,body,_=get('/provision',data);require(code==400 and body.startswith('TABY:ERR provision_'),'Invalid provision body/whitespace handling')
  _,after,_=command('WIFI_NETWORKS');require(before==after,'Invalid request changed network profiles');report['checks'].append('pretty_json_and_invalid_provision_preserve_profiles')
  hostname='taby-'+info['device_id'][-8:]+'.local'
  try:report['mdns_resolves_to_board']=socket.gethostbyname(hostname)==str(address)
  except OSError:report['mdns_resolves_to_board']=False
  report['passed']=True
 except Exception as e:report.update(passed=False,error=str(e));raise
 finally:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
