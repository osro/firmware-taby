"""Execute board IMU setup and complete polling against a register-backed I2C fake.

Only the optional sensor section of board initialization is isolated from LCD/
RTOS setup; it is copied verbatim from production, never reimplemented here.
The fake holds a fixed sample when self-test is set, making a successful write
of the old config insufficient to pass the changing-gravity regression.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CODE = ROOT / 'firmware/targets/c6-lcd-1.47/main'


def function(source, name):
    start = source.index('bool ' + name + '(')
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "c6_board.h"
#include "c6_geometry.h"
#define I2C_ADDR_BIT_LEN_7 0
#define ADC_CHANNEL_0 0
#define pdMS_TO_TICKS(n) (n)
typedef void *i2c_master_dev_handle_t;
typedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;
static void *imu,*adc,*calibration;
static c6_board_status_t state;
static uint16_t candidate;
static int64_t candidate_since,next_sensor,next_battery;
static uint8_t regs[256];
static int16_t gravity[3];
static int fail_write=-1,fail_read=-1,mismatch=-1,write_count,read_count,rotation_changes;
static bool changed_config_while_enabled,enabled_before_config;
static int i2c_master_probe(void *bus,int address,int timeout){
    (void)bus;(void)timeout;assert(address==0x6b);return ESP_OK;
}
static int i2c_master_bus_add_device(void *bus,const i2c_device_config_t *cfg,void **device){
    (void)bus;assert(cfg->device_address==0x6b);*device=(void*)1;return ESP_OK;
}
static void vTaskDelay(int ms){assert(ms==20);}
static int i2c_master_transmit(void *device,const uint8_t *data,size_t n,int timeout){
    assert(device==imu && n==2 && timeout==20);
    int index=write_count++;
    if(index==fail_write)return -1;
    if(data[0]==0x60){memset(regs,0,sizeof(regs));regs[0]=5;return ESP_OK;}
    if((data[0]==2 || data[0]==3) && (regs[8]&1))changed_config_while_enabled=true;
    regs[data[0]]=data[1];
    if(data[0]==8 && (data[1]&1) && (regs[2]!=0x40 || regs[3]!=0x15))enabled_before_config=true;
    return ESP_OK;
}
static int i2c_master_transmit_receive(void *device,const uint8_t *reg,size_t tx,uint8_t *out,size_t n,int timeout){
    assert(device==imu && tx==1 && timeout==20);
    ++read_count;
    if(*reg==fail_read)return -1;
    if(*reg==0x35){
        assert(n==6);
        for(unsigned i=0;i<3;++i){
            /* Controlled stuck self-test fixture, not a physical self-test model. */
            int16_t value=(regs[3]&0x80)?(i==1?-8192:0):gravity[i];
            out[i*2]=(uint16_t)value&255;out[i*2+1]=(uint16_t)value>>8;
        }
    }else{
        assert(n==1);*out=regs[*reg];if(*reg==mismatch)*out^=1;
    }
    return ESP_OK;
}
static int adc_oneshot_read(void *a,int channel,int *raw){(void)a;(void)channel;(void)raw;abort();}
static int adc_cali_raw_to_voltage(void *a,int raw,int *mv){(void)a;(void)raw;(void)mv;abort();}
esp_err_t c6_board_set_rotation(uint16_t r){state.rotation=r;++rotation_changes;return ESP_OK;}
'''

SUPPORT = r'''
static void reset_fixture(void){
    memset(&state,0,sizeof(state));memset(regs,0,sizeof(regs));regs[0]=5;
    memset(gravity,0,sizeof(gravity));imu=NULL;
    candidate=0;candidate_since=next_sensor=next_battery=0;
    fail_write=fail_read=mismatch=-1;write_count=read_count=rotation_changes=0;
    changed_config_while_enabled=enabled_before_config=false;
}
static void settle(int16_t x,int16_t y,int16_t z,int64_t start,bool automatic){
    gravity[0]=x;gravity[1]=y;gravity[2]=z;
    for(int i=0;i<=12;++i)c6_board_poll(start+i*100000,automatic);
}
'''


class C6IMUTests(unittest.TestCase):
    def run_c(self, body):
        source = (CODE / 'c6_board.c').read_text()
        setup = source[source.index('    if(i2c_master_probe('):source.index('    adc_oneshot_unit_init_cfg_t')]
        program = HARNESS + '\nstatic void initialize(void){void *i2c=NULL;\n' + setup + '\n}\n'
        program += function(source, 'c6_board_poll') + SUPPORT + '\nint main(void){\n' + body + '\nreturn 0;}\n'
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / 'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n')
            (path / 'test.c').write_text(program)
            executable = path / 'test'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            f'-I{path}', f'-I{CODE}', str(path / 'test.c'),
                            str(CODE / 'c6_geometry.c'), '-o', str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_initialized_sensor_tracks_four_changing_gravity_positions(self):
        self.run_c(r'''
    reset_fixture();initialize();assert(state.imu_available);
    assert(state.imu_ctrl1==0x40 && state.imu_ctrl2==0x15 && state.imu_ctrl7==1);
    settle(8192,0,0,100000,true);
    assert(state.rotation==90 && state.x_mg==1000 && state.y_mg==0);
    settle(0,8192,0,1500000,true);assert(state.rotation==180 && state.y_mg==1000);
    settle(-8192,0,0,2900000,true);assert(state.rotation==270 && state.x_mg==-1000);
    settle(0,-8192,0,4300000,true);assert(state.rotation==0 && state.y_mg==-1000);
    assert(state.imu_samples==52 && state.imu_errors==0 && rotation_changes==4);
''')

    def test_configuration_is_disabled_until_ready_and_all_failed_writes_reject_sensor(self):
        self.run_c(r'''
    reset_fixture();initialize();assert(state.imu_available);
    assert(!changed_config_while_enabled && !enabled_before_config);
    int writes=write_count;assert(writes>0);
    for(int i=0;i<writes;++i){
        reset_fixture();fail_write=i;initialize();assert(!state.imu_available);
        settle(8192,0,0,100000,true);assert(state.imu_samples==0 && rotation_changes==0);
    }
''')

    def test_readback_failure_or_mismatch_rejects_sensor(self):
        self.run_c(r'''
    const int control[]={2,3,8};
    for(unsigned i=0;i<3;++i)for(int bad=0;bad<2;++bad){
        reset_fixture();if(bad)mismatch=control[i];else fail_read=control[i];
        initialize();assert(!state.imu_available);
        uint8_t actual[]={state.imu_ctrl1,state.imu_ctrl2,state.imu_ctrl7};
        const uint8_t expected[]={0x40,0x15,1};
        for(unsigned j=0;j<3;++j)
            assert(actual[j]==(j==i?(bad?(expected[j]^1):0):expected[j]));
        settle(8192,0,0,100000,true);assert(state.imu_samples==0 && rotation_changes==0);
    }
''')

    def test_transient_candidate_cannot_rotate_before_new_settling_window(self):
        self.run_c(r'''
    reset_fixture();initialize();assert(state.imu_available);
    gravity[0]=8192;assert(!c6_board_poll(100000,true));
    gravity[0]=0;gravity[2]=8192;assert(!c6_board_poll(800000,true));
    gravity[0]=8192;gravity[2]=0;assert(!c6_board_poll(1000000,true));
    assert(!c6_board_poll(2100000,true));assert(state.rotation==0);
    assert(c6_board_poll(2200000,true));assert(state.rotation==90);
    settle(8192,0,0,2300000,true);assert(rotation_changes==1);
''')

    def test_settling_retention_manual_mode_and_read_errors(self):
        self.run_c(r'''
    reset_fixture();initialize();assert(state.imu_available);
    gravity[0]=8192;
    assert(!c6_board_poll(100000,true));assert(!c6_board_poll(1299999,true));
    assert(state.rotation==0);assert(!c6_board_poll(1300000,true)); /* throttled */
    assert(c6_board_poll(1400000,true));assert(state.rotation==90);
    settle(0,0,8192,1600000,true);assert(state.rotation==90 && state.z_mg==1000);
    settle(5734,5734,0,3000000,true);assert(state.rotation==90);
    settle(8192,0,0,4400000,true);assert(state.rotation==90);
    settle(0,8192,0,5800000,false);assert(state.rotation==90 && state.y_mg==1000);
    uint32_t samples=state.imu_samples;int changes=rotation_changes;
    int16_t x=state.x_mg,y=state.y_mg,z=state.z_mg;
    fail_read=0x35;
    assert(!c6_board_poll(7200000,true));assert(!c6_board_poll(7250000,true));
    assert(!c6_board_poll(7300000,true));
    assert(state.imu_errors==2 && state.imu_samples==samples && rotation_changes==changes);
    assert(state.x_mg==x && state.y_mg==y && state.z_mg==z);
''')


if __name__ == '__main__':
    unittest.main()
