/* drivers/power/rk30_adc_battery.c
 *
 * battery detect driver for the rk30 
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/wakelock.h>

#include <linux/gpio.h>
#include "../../../arch/arm/mach-tegra/board.h"


#include <linux/thermal.h>
#include <linux/extcon.h>
#include <linux/mfd/palmas.h>


#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

static int rk30_battery_dbg_level = 0;
module_param_named(dbg_level, rk30_battery_dbg_level, int, 0644);
#define pr_bat( args...) \
	do { \
		if (rk30_battery_dbg_level) { \
			pr_info(args); \
		} \
	} while (0)


/*******************以下参数可以修改******************************/
#define	TIMER_MS_COUNTS		 1000	//定时器的长度ms
//以下参数需要根据实际测试调整
//#define	SLOPE_SECOND_COUNTS	               15	//统计电压斜率的时间间隔s
/*#define	DISCHARGE_MIN_SECOND	           60	//最快放电电1%时间
#define	CHARGE_MIN_SECOND	               45	//最快充电电1%时间
#define	CHARGE_MID_SECOND	               90	//普通充电电1%时间
#define	CHARGE_MAX_SECOND	               250	//最长充电电1%时间*/
//#define	SLOPE_SECOND_COUNTS	               20	//统计电压斜率的时间间隔s
#define	SLOPE_SECOND_COUNTS	               10	//统计电压斜率的时间间隔s
#define	DISCHARGE_MIN_SECOND	           80	//最快放电电1%时间
#define	CHARGE_MIN_SECOND	               90	//最快充电电1%时间
#define	CHARGE_MID_SECOND	               180	//普通充电电1%时间
#define	CHARGE_MAX_SECOND	               400	//最长充电电1%时间

#define   CHARGE_FULL_DELAY_TIMES          10          //充电满检测防抖时间
#define   USBCHARGE_IDENTIFY_TIMES        5           //插入USB混流，pc识别检测时间

#define	NUM_VOLTAGE_SAMPLE	                       ((SLOPE_SECOND_COUNTS * 1000) / TIMER_MS_COUNTS)	 
#define	NUM_DISCHARGE_MIN_SAMPLE	         ((DISCHARGE_MIN_SECOND * 1000) / TIMER_MS_COUNTS)	 
#define	NUM_CHARGE_MIN_SAMPLE	         ((CHARGE_MIN_SECOND * 1000) / TIMER_MS_COUNTS)	    
#define	NUM_CHARGE_MID_SAMPLE	         ((CHARGE_MID_SECOND * 1000) / TIMER_MS_COUNTS)	     
#define	NUM_CHARGE_MAX_SAMPLE	         ((CHARGE_MAX_SECOND * 1000) / TIMER_MS_COUNTS)	  
#define   NUM_CHARGE_FULL_DELAY_TIMES         ((CHARGE_FULL_DELAY_TIMES * 1000) / TIMER_MS_COUNTS)	//充电满状态持续时间长度
#define   NUM_USBCHARGE_IDENTIFY_TIMES      ((USBCHARGE_IDENTIFY_TIMES * 1000) / TIMER_MS_COUNTS)	//充电满状态持续时间长度

#define   CHARGE_IS_OK                    1
#define   INVALID_CHARGE_CHECK               -1

#if defined(CONFIG_ARCH_RK3066B)

#define  BAT_DEFINE_VALUE	                                     1800
#elif defined(CONFIG_ARCH_RK2928)
#define  BAT_DEFINE_VALUE                                                3300
#else
#define  BAT_DEFINE_VALUE	                                     1800


#endif

#define BATT_FILENAME "/data/bat_last_capacity.dat"
//#define BAT_TZ "thermal_zone7"
#define BAT_TZ  "battery-voltage"
#define EXTCON_NAME	"tegra-udc"
#define EXTCON_NAME2 "palmas-extcon"


enum
{
	NONE=0,
	AC,
	USB,
};


static struct wake_lock batt_wake_lock;
static struct thermal_zone_device *bat_tz;


struct batt_vol_cal{
	u32 disp_cal;
	u32 dis_charge_vol;
	u32 charge_vol;
};

#define CONFIG_BATTERY_RK30_VOL3V8	1


#ifdef CONFIG_BATTERY_RK30_VOL3V8

#define BATT_MAX_VOL_VALUE                             4120               	//满电时的电池电压	 
#define BATT_ZERO_VOL_VALUE                            3400              	//关机时的电池电压
#define BATT_NOMAL_VOL_VALUE                         3800               
//divider resistance 
#define BAT_PULL_UP_R                                         200
#if defined(CONFIG_ARCH_RK3066B)
#define BAT_PULL_DOWN_R                                    100
#else
#define BAT_PULL_DOWN_R                                    120
#endif
static struct batt_vol_cal  batt_table[] = {
	{0,3605,3720},{1,3610,3750},{2,3614,3770},{3,3618,3790},{4,3622,3800},{5,3626,3810},
	{6,3630,3820},{7,3634,3830},{8,3638,3840},{9,3642,3845},{10,3646,3850},
	{11,3650,3860},{13,3654,3870},{15,3658,3880},{17,3664,3890},{19,3670,3900},
	{21,3676,3910},{23,3682,3920},{25,3688,3930},{27,3694,3940},{29,3700,3950},
	{31,3706,3960},{34,3716,3970},{37,3726,3980},{40,3736,3990},{43,3746,4000},
	{46,3756,4010},{49,3766,4020},{52,3776,4030},{55,3786,4040},{58,3796,4050},
	{61,3806,4060},{64,3816,4070},{67,3826,4080},{70,3836,4090},{73,3846,4100},
	{76,3860,4110},{79,3890,4115},{82,3920,4120},{85,3950,4125},{88,3980,4130},
	{91,4010,4132},{93,4030,4134},{95,4050,4136},{97,4070,4138},{99,4090,4139},{100,4100,4140},
};
#else
#define BATT_MAX_VOL_VALUE                              8284              	//Full charge voltage
#define BATT_ZERO_VOL_VALUE                             6800            	// power down voltage 
#define BATT_NOMAL_VOL_VALUE                          7600                

//定义ADC采样分压电阻，以实际值为准，单位K

#define BAT_PULL_UP_R                                         300 
#define BAT_PULL_DOWN_R                                    100

static struct batt_vol_cal  batt_table[] = {
	{0,6800,7400},    {1,6840,7440},    {2,6880,7480},     {3,6950,7450},       {5,7010,7510},    {7,7050,7550},
	{9,7080,7580},    {11,7104,7604},   {13,7140,7640},   {15,7160,7660},      {17,7220,7720},
	{19,7260,7760},  {21,7280,7780},   {23,7304,7802},   {25,7324,7824},      {27,7344,7844},
	{29,7360,7860},  {31,7374,7874},   {33,7386,7886},   {35,7398,7898},      {37,7410,7910},//500
	{39,7420,7920},  {41,7424,7928},   {43,7436,7947},   {45,7444,7944},      {47,7450,7958}, //508
	{49,7460,7965},  {51,7468,7975},   {53, 7476,7990},  {55,7482,8000},      {57,7492,8005}, // 5 14
	{59,7500,8011},  {61,7510,8033},   {63,7528,8044},   {65,7548,8055},      {67,7560,8066},//506
	{69,7600,8070},  {71,7618,8075},   {73,7634,8080},   {75,7654,8085},      {77,7690,8100}, //400
	{79,7900,8180},  {81,7920,8210},   {83,7964,8211},   {85,8000,8214},      {87,8002,8218},//290
	{89,8012, 8220}, {91,8022,8235},   {93,8110,8260},   {95,8140,8290},       {97,8170,8300},  {100,8200 ,8310},//110

};
#endif


struct batt_adc_gain{
	u32 adc_val;
	u32 gain;
};

static struct batt_adc_gain gain_table[] = {
	{1048,184}, {1064,200}, {1097,224}, {1105,233}, {1126,249}, {1140,258}, {1157,270}, {1172,278}, {1190,290},
	{1228,302}, {1229,310},

};
#define GAIN_NUM  ARRAY_SIZE(gain_table)


#define BATT_NUM  ARRAY_SIZE(batt_table)

#define adc_to_voltage(adc_val)                           ((adc_val * BAT_DEFINE_VALUE * (BAT_PULL_UP_R + BAT_PULL_DOWN_R)) / (1024 * BAT_PULL_DOWN_R))

/********************************************************************************/

extern int dwc_vbus_status(void);
extern int get_msc_connect_flag(void);

void turn_on_working_status_led(void);


struct rk30_adc_battery_data {
	int irq;
	
	//struct timer_list       timer;
	struct workqueue_struct *wq;
	struct delayed_work 	    delay_work;
	struct work_struct 	    dcwakeup_work;
	struct work_struct                   lowerpower_work;
	bool                    resume;
	
	struct rk30_adc_battery_platform_data *pdata;

	int                     full_times;
	
	struct adc_client       *client; 
	struct thermal_zone_device	*battery_tz;
	struct extcon_dev 		*edev;
	struct extcon_dev 		*usbdev;
	int                     adc_val;
	int                     adc_samples[NUM_VOLTAGE_SAMPLE+2];
	
	int                     bat_status;
	int                     bat_status_cnt;
	int                     bat_health;
	int                     bat_present;
	int                     bat_voltage;
	int                     bat_capacity;
	int                     bat_change;
	
	int                     old_charge_level;
	int                    *pSamples;
	int                     gBatCapacityDisChargeCnt;
	int                     gBatCapacityChargeCnt;
	int 	          		capacitytmp;
	int                     poweron_check;
	int                     suspend_capacity;

	int                     status_lock;
	int						charger;
	int						gBatCapacityWorking;
	int						gBatVoltageWorking;
	int 					charger_plug_in_check;
	int                     gBatVoltageChargeCnt;
	struct timer_list 		timer;
};
static struct rk30_adc_battery_data *gBatteryData;

static void rk30_adc_battery_check(struct rk30_adc_battery_data *bat);
enum {
	BATTERY_STATUS          = 0,
	BATTERY_HEALTH          = 1,
	BATTERY_PRESENT         = 2,
	BATTERY_CAPACITY        = 3,
	BATTERY_AC_ONLINE       = 4,
	BATTERY_STATUS_CHANGED	= 5,
	AC_STATUS_CHANGED   	= 6,
	BATTERY_INT_STATUS	    = 7,
	BATTERY_INT_ENABLE	    = 8,
};

typedef enum {
	CHARGER_BATTERY = 0,
	CHARGER_USB,
	CHARGER_AC
} charger_type_t;



static ssize_t
vbat_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	//struct thermal_zone_device *tz = container_of(dev, struct thermal_zone_device, device);
	
	//	to_thermal_zone(dev);
	long temperature;
	int ret;

	ret = thermal_zone_get_temp(bat_tz, &temperature);

	if (ret)
		return ret;

	return sprintf(buf, "%ld\n", temperature);
}

static ssize_t debug_enable_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", rk30_battery_dbg_level);
}

static ssize_t debug_enable_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned long data;

	if (strict_strtoul(buf, 10, &data))
		return -EINVAL;

	if ((data == 0) || (data == 1))
	{
		rk30_battery_dbg_level = data;
	}

	return count;
}

static DEVICE_ATTR(jxd_bat, 0440, vbat_show, NULL);
static DEVICE_ATTR(debug, 0660, debug_enable_show, debug_enable_store);

static void adc_val_read(struct thermal_zone_device	*battery_tz)
{
	long adc_val;
	int gain = 0;
	int i;
	int val;

	struct batt_adc_gain *p;
	p = gain_table;

	thermal_zone_get_temp(battery_tz, &adc_val);
	
	val = adc_val - 303;// 303 ==>  the temp_offset of  platform device "gadc_thermal_vbat"

	if(val >= (p[GAIN_NUM - 1].adc_val)){
		gain = p[GAIN_NUM - 1].gain;
	}	
	else
	{
		if(val <= (p[0].adc_val))
		{
			gain = p[0].gain;;
		}
		else
		{
			for(i = 0; i < GAIN_NUM - 1; i++)
			{
				if(((p[i].adc_val) <= val) && (val < (p[i+1].adc_val))){
						gain = p[i].gain + ((val - p[i].adc_val)*(p[i+1].gain - p[i].gain)) / (p[i+1].adc_val - p[i].adc_val);
						break;
				}
			}
		} 
	}

	gBatteryData->adc_val = adc_val * 2 + gain;

	//gBatteryData->adc_val = adc_val * 2 + 290; //scaler = 2,  1218 = 0.372v,327 = 0.4v
}


static int rk30_adc_battery_load_capacity(void)
{
	u32 p;
	int ret;	
	
	ret = load_battery_capacity_by_read_reg(&p);
	if(ret < 0){
		pr_bat("adc_battery_load_capacity: read register failed,ret=%d\n",ret);
		return -1;
	}

	return p;
	/*char value[4];
	int* p = (int *)value;
	int ret;


	long fd = sys_open((const char __user *)BATT_FILENAME,O_RDONLY,0);

	if(fd < 0){
		pr_bat("rk30_adc_battery_load_capacity: open file /data/bat_last_capacity.dat failed,ret=%d\n",ret);
		return -1;
	}

	sys_read(fd,(char __user *)value,4);
	sys_close(fd);
	printk("load capacity: capacity=%d , value=%s\n",*p,value);

	return (*p);*/
}

static int rk30_adc_battery_put_capacity(int loadcapacity)
{
	int ret;

	ret = put_battery_capacity_by_write_reg(loadcapacity);
	if(ret < 0){
		pr_bat("adc_battery_put_capacity: write register failed,ret=%d\n",ret);
		return -1;
	}

	return 0;
	/*
	char value[4];
	int* p = (int *)value;
	long fd = sys_open(BATT_FILENAME,O_CREAT | O_RDWR,0);

	if(fd < 0){
		pr_bat("rk30_adc_battery_put_capacity: open file /data/bat_last_capacity.dat failed\n");
		return;
	}
	
	*p = loadcapacity;
	sys_write(fd, (const char __user *)value, 4);
	printk("save capacity: capacity=%d , value=%s\n",loadcapacity,value);

	sys_close(fd);*/
}

static void rk30_adc_battery_charge_enable(struct rk30_adc_battery_data *bat)
{
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;

	if (pdata->charge_set_pin != INVALID_GPIO){
		gpio_direction_output(pdata->charge_set_pin, pdata->charge_set_level);
	}

	if (pdata->ctrl_charge_led_pin != INVALID_GPIO){
		gpio_direction_output(pdata->ctrl_charge_led_pin, 1);
	}

	if (pdata->ctrl_working_led_pin != INVALID_GPIO){
		gpio_direction_output(pdata->ctrl_working_led_pin, 0);
	}
}

static void rk30_adc_battery_charge_disable(struct rk30_adc_battery_data *bat)
{
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;

	if (pdata->charge_set_pin != INVALID_GPIO){
		gpio_direction_output(pdata->charge_set_pin, 1 - pdata->charge_set_level);
	}

	if (pdata->ctrl_charge_led_pin != INVALID_GPIO){
		gpio_direction_output(pdata->ctrl_charge_led_pin, 0);
	}

	if (pdata->ctrl_working_led_pin != INVALID_GPIO){
		gpio_direction_output(pdata->ctrl_working_led_pin, 1);
	}
}

//extern int suspend_flag;
static int rk30_adc_battery_get_charge_level(struct rk30_adc_battery_data *bat)
{
	int charge_on = 0;
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;

#if defined (CONFIG_BATTERY_RK30_AC_CHARGE)
	if (pdata->dc_det_pin != INVALID_GPIO){
		if (gpio_get_value (pdata->dc_det_pin) == pdata->dc_det_level){
			charge_on = 1;
		}
	}
	else{
		if(pdata->is_dc_charging)
			charge_on =pdata->is_dc_charging();

	}
	
		
#endif
#if 1
	if (pdata->spport_usb_charging)  //is usb charging 
		if(pdata->is_usb_charging)
			charge_on = pdata->is_usb_charging();

#endif

	if(bat->edev)
	{
		if (true == extcon_get_cable_state(bat->edev, "TA"))
			charge_on = AC;
		else if(true == extcon_get_cable_state(bat->usbdev, "USB"))//avoid ac adapter can't detect normally,by yunboa 20150608
			charge_on = USB;

		if(charge_on != NONE)
		{
			if(bat->charger != charge_on)
			{
				mod_timer(&bat->timer,jiffies + 10 * HZ);
			}
		}
		bat->charger = charge_on;
	}

	return charge_on;
}
static int  is_charge_ok(struct rk30_adc_battery_data *bat)
{
	int charge_is_ok = 0;
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;

	if((pdata->charge_ok_pin == INVALID_GPIO)&& ( pdata->charging_ok == NULL))
		return -1;
	
	if (pdata->charge_ok_pin != INVALID_GPIO){		
		if (gpio_get_value(pdata->charge_ok_pin) == pdata->charge_ok_level)
		{
			charge_is_ok =1;
		}
	}else if( pdata->charging_ok)
		{	
		charge_is_ok = pdata->charging_ok();
		}
	return charge_is_ok;


}

//int old_charge_level;
static int rk30_adc_battery_status_samples(struct rk30_adc_battery_data *bat)
{
	int charge_level;
//	struct rk30_adc_battery_platform_data *pdata = bat->pdata;
	charge_level = rk30_adc_battery_get_charge_level(bat);

	//检测充电状态变化情况
	if (charge_level != bat->old_charge_level){
		bat->old_charge_level = charge_level;
		bat->bat_change  = 1;
		
		if(charge_level) {            
			rk30_adc_battery_charge_enable(bat);
		}
		else{
			rk30_adc_battery_charge_disable(bat);
		}
		bat->bat_status_cnt = 0;        //状态变化开始计数
	}

	if(charge_level == 0){   
	//discharge
		bat->full_times = 0;
		bat->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	}
	else{
	//CHARGE	
		if( is_charge_ok(bat)  ==  INVALID_CHARGE_CHECK){
		//if (pdata->charge_ok_pin == INVALID_GPIO){  //no charge_ok_pin

			if (bat->bat_capacity == 100){
				if (bat->bat_status != POWER_SUPPLY_STATUS_FULL){
					bat->bat_status = POWER_SUPPLY_STATUS_FULL;
					bat->bat_change  = 1;
					//turn_on_working_status_led();
				}
			}
			else{
				bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
			}

			turn_on_working_status_led();

		}
		else{  // pin of charge_ok_pin
			if (is_charge_ok(bat) != CHARGE_IS_OK ){

				bat->full_times = 0;
				bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
			}
			else{
	//检测到充电满电平标志
				bat->full_times++;

				if (bat->full_times >= NUM_CHARGE_FULL_DELAY_TIMES) {
					bat->full_times = NUM_CHARGE_FULL_DELAY_TIMES + 1;
				}

				if ((bat->full_times >= NUM_CHARGE_FULL_DELAY_TIMES) && (bat->bat_capacity >= 95)){
					if (bat->bat_status != POWER_SUPPLY_STATUS_FULL){
						bat->bat_status = POWER_SUPPLY_STATUS_FULL;
						bat->bat_capacity = 100;
						bat->bat_change  = 1;
					}
				}
				else{
					bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;
				}
			}
		}
	}

	return charge_level;
}
//#define adc_to_voltage(adc_val)                           ((adc_val * BAT_DEFINE_VALUE * (BAT_PULL_UP_R + BAT_PULL_DOWN_R)) / (1024 * BAT_PULL_DOWN_R))

static int rk_adc_voltage(int value)
{
	int voltage;

	int ref_voltage; //reference_voltage
	int pullup_res;
	int pulldown_res;

	ref_voltage = gBatteryData ->pdata->reference_voltage;
	pullup_res = gBatteryData ->pdata->pull_up_res;
	pulldown_res = gBatteryData ->pdata->pull_down_res;

	if(ref_voltage && pullup_res && pulldown_res){
		
		voltage =  ((value * ref_voltage * (pullup_res + pulldown_res)) / (4095 * pulldown_res));
		
	}else{
		voltage = adc_to_voltage(value); 	
	}
		
		
	return voltage;

}

static int *pSamples;
static void rk30_adc_battery_voltage_samples(struct rk30_adc_battery_data *bat)
{
	int value;
	int i,*pStart = bat->adc_samples, num = 0;
	int level = rk30_adc_battery_get_charge_level(bat);


	adc_val_read(bat->battery_tz);
	value = bat->adc_val;
	//adc_async_read(bat->client);  yunboa 	

	*pSamples++ = rk_adc_voltage(value);

	bat->bat_status_cnt++;
	if (bat->bat_status_cnt > NUM_VOLTAGE_SAMPLE)  bat->bat_status_cnt = NUM_VOLTAGE_SAMPLE + 1;

	num = pSamples - pStart;
	
	if (num >= NUM_VOLTAGE_SAMPLE){
		pSamples = pStart;
		num = NUM_VOLTAGE_SAMPLE;
		
	}

	value = 0;
	for (i = 0; i < num; i++){
		value += bat->adc_samples[i];
	}
	bat->bat_voltage = value / num;

	/*消除毛刺电压*/
	//if(1 == level){
	if(level != 0){ //  AC=1,USB=2,none=0, sometimes ac can't recognise,so can't use  1==level
		if(bat->bat_voltage >= batt_table[BATT_NUM-1].charge_vol+ 5)
			bat->bat_voltage = batt_table[BATT_NUM-1].charge_vol  + 5;
		//else if((level != USB) && (bat->bat_voltage <= batt_table[0].charge_vol  - 5))
		//	bat->bat_voltage =  batt_table[0].charge_vol - 5;
	}
	else{
		if(bat->bat_voltage >= batt_table[BATT_NUM-1].dis_charge_vol+ 5)
			bat->bat_voltage = batt_table[BATT_NUM-1].dis_charge_vol  + 5;
		else if(bat->bat_voltage <= batt_table[0].dis_charge_vol  - 5)
			bat->bat_voltage =  batt_table[0].dis_charge_vol - 5;

	}

	//printk("rk30_adc_battery_voltage_samples:level = %d , bat_voltage = %d\n",level,bat->bat_voltage);

}
static int rk30_adc_battery_voltage_to_capacity(struct rk30_adc_battery_data *bat, int BatVoltage)
{
	int i = 0;
	int capacity = 0;

	struct batt_vol_cal *p;
	p = batt_table;

	//if (rk30_adc_battery_get_charge_level(bat) == AC){  //charge
	if (rk30_adc_battery_get_charge_level(bat)){  //charge
		if(BatVoltage >= (p[BATT_NUM - 1].charge_vol)){
			capacity = 99;
		}	
		else{
			if(BatVoltage <= (p[0].charge_vol)){
				capacity = 0;
			}
			else{
				for(i = 0; i < BATT_NUM - 1; i++){

					if(((p[i].charge_vol) <= BatVoltage) && (BatVoltage < (p[i+1].charge_vol))){
						capacity = p[i].disp_cal + ((BatVoltage - p[i].charge_vol) *  (p[i+1].disp_cal -p[i].disp_cal ))/ (p[i+1].charge_vol- p[i].charge_vol);
						break;
					}
				}
			}  
		}

	}
	/*else if(rk30_adc_battery_get_charge_level(bat) == USB)
	{
		if(BatVoltage >= (p[BATT_NUM - 1].charge_vol-30)){
			capacity = 99;
		}	
		else{
			if(BatVoltage <= (p[0].charge_vol-30)){
				capacity = 0;
			}
			else{
				for(i = 0; i < BATT_NUM - 1; i++){

					if(((p[i].charge_vol-30) <= BatVoltage) && (BatVoltage < (p[i+1].charge_vol-30))){
						capacity = p[i].disp_cal + ((BatVoltage - (p[i].charge_vol-30)) *  (p[i+1].disp_cal -p[i].disp_cal ))/ ((p[i+1].charge_vol-30)- (p[i].charge_vol-30));
						break;
					}
				}
			}  
		}
	}*/
	else{  //discharge
		if(BatVoltage >= (p[BATT_NUM - 1].dis_charge_vol)){
			capacity = 100;
		}	
		else{
			if(BatVoltage <= (p[0].dis_charge_vol)){
				capacity = 0;
			}
			else{
				for(i = 0; i < BATT_NUM - 1; i++){
					if(((p[i].dis_charge_vol) <= BatVoltage) && (BatVoltage < (p[i+1].dis_charge_vol))){
						capacity =  p[i].disp_cal+ ((BatVoltage - p[i].dis_charge_vol) * (p[i+1].disp_cal -p[i].disp_cal ) )/ (p[i+1].dis_charge_vol- p[i].dis_charge_vol) ;
						break;
					}
				}
			}  

		}


	}
    return capacity;
}

static void rk30_battery_charging_capacity_check(struct rk30_adc_battery_data *bat)
{
	int i;
	int level,oldlevel;
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;
	//printk("%s--%d:\n",__FUNCTION__,__LINE__);

	//bat->old_charge_level = -1;

	pSamples = bat->adc_samples;

	//adc_sync_read(bat->client);                             //start adc sample yunboa
	adc_val_read(bat->battery_tz);
	level = oldlevel = rk30_adc_battery_status_samples(bat);//init charge status

	bat->full_times = 0;
	for (i = 0; i < NUM_VOLTAGE_SAMPLE; i++){                //0.3 s
		mdelay(1);
		rk30_adc_battery_voltage_samples(bat);              //get voltage
		//level = rk30_adc_battery_status_samples(bat);       //check charge status
		level = rk30_adc_battery_get_charge_level(bat);

		if (oldlevel != level){
			oldlevel = level;                               //if charge status changed, reset sample
			i = 0;
		}
		if(rk30_battery_dbg_level)
			printk("battery_charging_capacity_check ,level=%d,adb_samples[%d]=%d\n",level,i,bat->adc_samples[i]);
	}

	//bat->gBatCapacityWorking = rk30_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);  //init bat_capacity
	bat->gBatVoltageWorking = bat->bat_voltage;
	if(rk30_battery_dbg_level)
		printk("battery_charging_capacity_check: gBatVoltageWorking = %d\n",bat->gBatVoltageWorking);

}

static void rk30_adc_battery_capacity_samples(struct rk30_adc_battery_data *bat)
{
	int capacity = 0;
//	struct rk30_adc_battery_platform_data *pdata = bat->pdata;
	int timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE;
	int timer_of_discharge_sample = DISCHARGE_MIN_SECOND;

	//充放电状态变化后，Buffer填满之前，不更新
	if (bat->bat_status_cnt < NUM_VOLTAGE_SAMPLE)  {
		bat->gBatCapacityDisChargeCnt = 0;
		bat->gBatCapacityChargeCnt    = 0;
		return;
	}
	
	capacity = rk30_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);
	if (rk30_adc_battery_get_charge_level(bat)){
		if (capacity > bat->bat_capacity){
			if(capacity > bat->bat_capacity + 10 )
			        timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE -10;  //5s
			else if(capacity > bat->bat_capacity + 7 )
			        timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE -5; //10s
			        else if(capacity > bat->bat_capacity + 3 )
			                timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE - 2; // 13
			//实际采样到的容量比显示的容量大，逐级上升
			if (++(bat->gBatCapacityChargeCnt) >= timer_of_charge_sample){
				bat->gBatCapacityChargeCnt = 0;
				if (bat->bat_capacity < 99){
					bat->bat_capacity++;
					bat->bat_change  = 1;
				}
			}
			bat->gBatCapacityDisChargeCnt = 0;
		}
		else{  //   实际的容量比采样比 显示的容量小
		            //bat->gBatCapacityDisChargeCnt = 0;
		            (bat->gBatCapacityChargeCnt)++;
			pr_bat("------------- is_charge_ok(bat)=%d\n", is_charge_ok(bat));
           			if( is_charge_ok(bat) != INVALID_CHARGE_CHECK){
			//if (pdata->charge_ok_pin != INVALID_GPIO){
				//if (gpio_get_value(pdata->charge_ok_pin) == pdata->charge_ok_level){
				if( is_charge_ok(bat) == CHARGE_IS_OK){
					if(capacity > bat->bat_capacity + 10 )
					        timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE -13;  //  2s
					else if(capacity > bat->bat_capacity + 7 )
					        timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE -10; //10s
					else if(capacity > bat->bat_capacity + 2 )
					        timer_of_charge_sample = NUM_CHARGE_MIN_SAMPLE -8; //7s					
				//检测到电池充满标志，同时长时间内充电电压无变化，开始启动计时充电，快速上升容量
					if (bat->gBatCapacityChargeCnt >= timer_of_charge_sample){
						bat->gBatCapacityChargeCnt = 0;
						if (bat->bat_capacity < 99){
							bat->bat_capacity++;
							bat->bat_change  = 1;

						}
					}
				}
				else{

					if ((bat->bat_capacity >= 90) &&((bat->gBatCapacityChargeCnt) > NUM_CHARGE_MAX_SAMPLE))
					{
						bat->gBatCapacityChargeCnt = (NUM_CHARGE_MAX_SAMPLE - NUM_CHARGE_MID_SAMPLE);

						if (bat->bat_capacity < 99){
							bat->bat_capacity++;
							bat->bat_change  = 1;

						}
					}
	
				}


			}							
			else{
			//没有充电满检测脚，长时间内电压无变化，定时器模拟充电
				if (capacity > bat->capacitytmp){
				//过程中如果电压有增长，定时器复位，防止定时器模拟充电比实际充电快
					bat->gBatCapacityChargeCnt = 0;
					bat->gBatCapacityDisChargeCnt = 0;
				}
				else{

					if ((bat->bat_capacity >= 85) &&(bat->gBatCapacityChargeCnt > NUM_CHARGE_MAX_SAMPLE)){
						bat->gBatCapacityChargeCnt = (NUM_CHARGE_MAX_SAMPLE - NUM_CHARGE_MID_SAMPLE);

						if (bat->bat_capacity <= 99){
							bat->bat_capacity++;
							bat->bat_change  = 1;
						}
					}

					//if(bat->bat_voltage < bat->gBatVoltageWorking)
					if(rk_adc_voltage(gBatteryData->adc_val) < bat->gBatVoltageWorking)
					{
						bat->gBatVoltageChargeCnt = 0;
						//printk("###################yunboa: Warning charge current < working consume current, gBatVoltageWorking = %d #######################\n",bat->gBatVoltageWorking);					
						if (capacity < bat->bat_capacity){
							if(capacity + 10 > bat->bat_capacity  )
							        timer_of_discharge_sample = DISCHARGE_MIN_SECOND -10;  //5s
							else if(capacity  + 7 > bat->bat_capacity )
							        timer_of_discharge_sample = DISCHARGE_MIN_SECOND -5; //10s
							        else if(capacity  + 3> bat->bat_capacity )
							                timer_of_discharge_sample = DISCHARGE_MIN_SECOND - 2; // 13

							if (++(bat->gBatCapacityDisChargeCnt) >= timer_of_discharge_sample){
								bat->gBatCapacityDisChargeCnt = 0;
								if (bat->bat_capacity > 0){
									bat->bat_capacity--;
									bat->bat_change  = 1;
									bat->gBatCapacityChargeCnt = 0;
									rk30_battery_charging_capacity_check(bat);
								}
							}
						}
						else{
							bat->gBatCapacityDisChargeCnt = 0;
						}
					}
					else
					{
						//if(bat->bat_voltage > bat->gBatVoltageWorking)
						if(rk_adc_voltage(gBatteryData->adc_val) > bat->gBatVoltageWorking)
						{
							if (++(bat->gBatVoltageChargeCnt) >= 10)
							{
								bat->gBatVoltageChargeCnt = 0;
								mod_timer(&bat->timer,jiffies + 10 * HZ);
							}

							bat->gBatCapacityDisChargeCnt = 0;
						}
					}

				}

			}
		}
	}    
	else{
	//放电时,只允许电压下降
		if (capacity < bat->bat_capacity){
			if(capacity + 10 > bat->bat_capacity  )
			        timer_of_discharge_sample = DISCHARGE_MIN_SECOND -10;  //5s
			else if(capacity  + 7 > bat->bat_capacity )
			        timer_of_discharge_sample = DISCHARGE_MIN_SECOND -5; //10s
			        else if(capacity  + 3> bat->bat_capacity )
			                timer_of_discharge_sample = DISCHARGE_MIN_SECOND - 2; // 13

			if (++(bat->gBatCapacityDisChargeCnt) >= timer_of_discharge_sample){
				bat->gBatCapacityDisChargeCnt = 0;
				if (bat->bat_capacity > 0){
					bat->bat_capacity-- ;
					bat->bat_change  = 1;
				}
			}
		}
		else{
			bat->gBatCapacityDisChargeCnt = 0;
		}
		bat->gBatCapacityChargeCnt = 0;
		bat->gBatVoltageChargeCnt = 0;
	}
		bat->capacitytmp = capacity;
}

//static int poweron_check = 0;
//static void rk30_adc_battery_poweron_capacity_check(void)
static int rk30_adc_battery_poweron_capacity_check(void)
{

	int new_capacity, old_capacity;

	new_capacity = gBatteryData->bat_capacity;
	old_capacity = rk30_adc_battery_load_capacity();

	pr_bat("------------old_capacity--- =%d\n",old_capacity);
/*
	if(old_capacity == -1)
		return old_capacity;*/
	
	if ((old_capacity <= 0) || (old_capacity >= 100)){
		old_capacity = new_capacity;
	}    

	if (gBatteryData->bat_status == POWER_SUPPLY_STATUS_FULL){
		if (new_capacity > 80){
			gBatteryData->bat_capacity = 100;
		}
	}
	else if (gBatteryData->bat_status != POWER_SUPPLY_STATUS_NOT_CHARGING){
	//chargeing state
	//问题：
//	//1）长时间关机放置后，开机后读取的容量远远大于实际容量怎么办？
//	//2）如果不这样做，短时间关机再开机，前后容量不一致又该怎么办？
//	//3）一下那种方式合适？
	//gBatteryData->bat_capacity = new_capacity;
		gBatteryData->bat_capacity = (new_capacity > old_capacity) ? new_capacity : old_capacity;
	}else{

		if(new_capacity > old_capacity + 50 )
			gBatteryData->bat_capacity = new_capacity;
		else
			gBatteryData->bat_capacity = (new_capacity < (old_capacity-10)) ? new_capacity : old_capacity;  //avoid the value of capacity increase 
	}


	pr_bat("capacity = %d, new_capacity = %d, old_capacity = %d\n",gBatteryData->bat_capacity, new_capacity, old_capacity);

	gBatteryData->bat_change = 1;
	return 0;
}

#if defined(CONFIG_BATTERY_RK30_USB_CHARGE)
static int rk30_adc_battery_get_usb_property(struct power_supply *psy, 
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	charger_type_t charger;
	charger =  CHARGER_USB;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_USB)
		//	val->intval = get_msc_connect_flag();
		if (gBatteryData->pdata->spport_usb_charging)  //is usb charging 
			if(gBatteryData->pdata->is_usb_charging)
				val->intval = gBatteryData ->pdata->is_usb_charging();

		printk("%s:%d\n",__FUNCTION__,val->intval);
		break;

	default:
		return -EINVAL;
	}
	
	return 0;

}

static enum power_supply_property rk30_adc_battery_usb_props[] = {
    
	POWER_SUPPLY_PROP_ONLINE,
};

static struct power_supply rk30_usb_supply = 
{
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,

	.get_property   = rk30_adc_battery_get_usb_property,

	.properties     = rk30_adc_battery_usb_props,
	.num_properties = ARRAY_SIZE(rk30_adc_battery_usb_props),
};
#endif

#if defined(CONFIG_BATTERY_RK30_AC_CHARGE)
static irqreturn_t rk30_adc_battery_dc_wakeup(int irq, void *dev_id)
{   
	queue_work(gBatteryData->wq, &gBatteryData->dcwakeup_work);
	return IRQ_HANDLED;
}


static int rk30_adc_battery_get_ac_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	int ret = 0;
	charger_type_t charger;
	charger =  CHARGER_USB;
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (psy->type == POWER_SUPPLY_TYPE_MAINS)
		{
			if (rk30_adc_battery_get_charge_level(gBatteryData))
			{
				val->intval = 1;
				}
			else
				{
				val->intval = 0;	
				}
		}
		DBG("%s:%d\n",__FUNCTION__,val->intval);
		break;
		
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static enum power_supply_property rk30_adc_battery_ac_props[] = 
{
	POWER_SUPPLY_PROP_ONLINE,
};

static struct power_supply rk30_ac_supply = 
{
	.name = "ac",
	.type = POWER_SUPPLY_TYPE_MAINS,

	.get_property   = rk30_adc_battery_get_ac_property,

	.properties     = rk30_adc_battery_ac_props,
	.num_properties = ARRAY_SIZE(rk30_adc_battery_ac_props),
};

static void rk30_adc_battery_dcdet_delaywork(struct work_struct *work)
{
	int ret;
	struct rk30_adc_battery_platform_data *pdata;
	int irq;
	int irq_flag;
	
	pdata    = gBatteryData->pdata;
	irq        = gpio_to_irq(pdata->dc_det_pin);
	irq_flag = gpio_get_value (pdata->dc_det_pin) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;

	rk28_send_wakeup_key(); // wake up the system

	free_irq(irq, NULL);
	ret = request_irq(irq, rk30_adc_battery_dc_wakeup, irq_flag, "ac_charge_irq", NULL);// reinitialize the DC irq 
	if (ret) {
		free_irq(irq, NULL);
	}

	power_supply_changed(&rk30_ac_supply);

	gBatteryData->bat_status_cnt = 0;        //the state of battery is change

}


#endif

static int rk30_adc_battery_get_status(struct rk30_adc_battery_data *bat)
{
	return (bat->bat_status);
}

static int rk30_adc_battery_get_health(struct rk30_adc_battery_data *bat)
{
	return POWER_SUPPLY_HEALTH_GOOD;
}

static int rk30_adc_battery_get_present(struct rk30_adc_battery_data *bat)
{
	return (bat->bat_voltage < BATT_MAX_VOL_VALUE) ? 0 : 1;
}

static int rk30_adc_battery_get_voltage(struct rk30_adc_battery_data *bat)
{
	return (bat->bat_voltage );
}

static int rk30_adc_battery_get_capacity(struct rk30_adc_battery_data *bat)
{
	return (bat->bat_capacity);
}

static int rk30_adc_battery_get_property(struct power_supply *psy,
				 enum power_supply_property psp,
				 union power_supply_propval *val)
{		
	int ret = 0;

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = rk30_adc_battery_get_status(gBatteryData);
			DBG("gBatStatus=%d\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = rk30_adc_battery_get_health(gBatteryData);
			DBG("gBatHealth=%d\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = rk30_adc_battery_get_present(gBatteryData);
			DBG("gBatPresent=%d\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val ->intval = rk30_adc_battery_get_voltage(gBatteryData);
			DBG("gBatVoltage=%d\n",val->intval);
			break;
		//	case POWER_SUPPLY_PROP_CURRENT_NOW:
		//		val->intval = 1100;
		//		break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = rk30_adc_battery_get_capacity(gBatteryData);
			DBG("gBatCapacity=%d%%\n",val->intval);
			break;
		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;	
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
			val->intval = BATT_MAX_VOL_VALUE;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
			val->intval = BATT_ZERO_VOL_VALUE;
			break;
		default:
			ret = -EINVAL;
			break;
	}

	return ret;
}

static enum power_supply_property rk30_adc_battery_props[] = {

	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
//	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
};

static struct power_supply rk30_battery_supply = 
{
	.name = "jxd_battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,

	.get_property   = rk30_adc_battery_get_property,

	.properties     = rk30_adc_battery_props,
	.num_properties = ARRAY_SIZE(rk30_adc_battery_props),
};

#ifdef CONFIG_PM
static void rk30_adc_battery_resume_check(void)
{
	int i;
	int level,oldlevel;
	int new_capacity, old_capacity;
	struct rk30_adc_battery_data *bat = gBatteryData;

	//bat->old_charge_level = -1; 20151201, annotate because of system wake up by WAKE18, 4-0058 and lcd is not on,but green led lighted up
	pSamples = bat->adc_samples;

//	adc_sync_read(bat->client);                             //start adc sample  yunboa
	adc_val_read(bat->battery_tz);

	level = oldlevel = rk30_adc_battery_status_samples(bat);//init charge status

	for (i = 0; i < NUM_VOLTAGE_SAMPLE; i++) {               //0.3 s   
	
		mdelay(1);
		rk30_adc_battery_voltage_samples(bat);              //get voltage
		level = rk30_adc_battery_status_samples(bat);       //check charge status
		if (oldlevel != level){		
		    oldlevel = level;                               //if charge status changed, reset sample
		    i = 0;
		}        
	}
	new_capacity = rk30_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);
	old_capacity =gBatteryData->suspend_capacity;

	//printk("rk30_adc_battery_resume_check: new_capacity=%d , old_capacity=%d\n",new_capacity,old_capacity);
	if (bat->bat_status != POWER_SUPPLY_STATUS_NOT_CHARGING){
	//chargeing state
		bat->bat_capacity = (new_capacity > old_capacity) ? new_capacity : old_capacity;
	}
	else{
		bat->bat_capacity = (new_capacity < (old_capacity-10)) ? new_capacity : old_capacity;  // aviod the value of capacity increase    dicharge
	}

}

static int rk30_adc_battery_suspend(struct platform_device *dev, pm_message_t state)
{
	int irq;
	gBatteryData->suspend_capacity = gBatteryData->bat_capacity;
	cancel_delayed_work(&gBatteryData->delay_work);
	
	if( gBatteryData->pdata->batt_low_pin != INVALID_GPIO){
		
		irq = gpio_to_irq(gBatteryData->pdata->batt_low_pin);
		enable_irq(irq);
	    	enable_irq_wake(irq);
    }

	return 0;
}

static int rk30_adc_battery_resume(struct platform_device *dev)
{
	int irq;
	gBatteryData->resume = true;
	gBatteryData->gBatCapacityDisChargeCnt = 0;//avoid battery capacity change immediately, by yunboa 20150608
	queue_delayed_work(gBatteryData->wq, &gBatteryData->delay_work, msecs_to_jiffies(100));
	if( gBatteryData->pdata->batt_low_pin != INVALID_GPIO){
		
		irq = gpio_to_irq(gBatteryData->pdata->batt_low_pin);
	    	disable_irq_wake(irq);
		disable_irq(irq);
    	}

	return 0;
}
#else
#define rk30_adc_battery_suspend NULL
#define rk30_adc_battery_resume NULL
#endif

unsigned long AdcTestCnt = 0;
static void rk30_adc_battery_timer_work(struct work_struct *work)
{
	int ret;
	long temperature;
		
#ifdef CONFIG_PM
	if (gBatteryData->resume) {
		rk30_adc_battery_resume_check();
		gBatteryData->resume = false;
	}
#endif

	rk30_adc_battery_status_samples(gBatteryData);

	if (gBatteryData->poweron_check){   
		gBatteryData->poweron_check = 0;
		//Power on Battery detect
		rk30_adc_battery_check(gBatteryData);
		rk30_adc_battery_poweron_capacity_check();
	}

	if(gBatteryData->charger_plug_in_check)
	{
		gBatteryData->charger_plug_in_check = 0;
		rk30_battery_charging_capacity_check(gBatteryData);
	}


	rk30_adc_battery_voltage_samples(gBatteryData);
	rk30_adc_battery_capacity_samples(gBatteryData);

	if( 0 == gBatteryData ->pdata ->charging_sleep){
		if( 1 == rk30_adc_battery_get_charge_level(gBatteryData)){  // charge
			if(0 == gBatteryData->status_lock ){			
				wake_lock(&batt_wake_lock);  //lock
				gBatteryData->status_lock = 1; 
			}
		}
		else{
			if(1 == gBatteryData->status_lock ){			
				wake_unlock(&batt_wake_lock);  //unlock
				gBatteryData->status_lock = 0; 
			}

		}
	}
	
	
	/*update battery parameter after adc and capacity has been changed*/
	if(gBatteryData->bat_change){
		gBatteryData->bat_change = 0;
		printk("s818_adc_battery_timer_work save capacity,capacity=%d\n",gBatteryData->bat_capacity);
		rk30_adc_battery_put_capacity(gBatteryData->bat_capacity);
		power_supply_changed(&rk30_battery_supply);
#if  defined (CONFIG_BATTERY_RK30_AC_CHARGE)
		if (gBatteryData->pdata->dc_det_pin == INVALID_GPIO){
			power_supply_changed(&rk30_ac_supply);
		}
#endif
	}

	if (rk30_battery_dbg_level){
		if (++AdcTestCnt >= 2)
			{
			AdcTestCnt = 0;

			pr_bat("Status = %d, RealAdcVal = %d, RealVol = %d,gBatVol = %d, gBatCap = %d, RealCapacity = %d, dischargecnt = %d, chargecnt = %d ,chargingvolcnt = %d \n", 
			gBatteryData->bat_status, gBatteryData->adc_val, rk_adc_voltage(gBatteryData->adc_val), 
			gBatteryData->bat_voltage, gBatteryData->bat_capacity, gBatteryData->capacitytmp, gBatteryData->gBatCapacityDisChargeCnt,gBatteryData->gBatCapacityChargeCnt,gBatteryData->gBatVoltageChargeCnt);

		}
	}
	queue_delayed_work(gBatteryData->wq, &gBatteryData->delay_work, msecs_to_jiffies(TIMER_MS_COUNTS));

}


static int rk30_adc_battery_io_init(struct rk30_adc_battery_platform_data *pdata)
{
	int ret = 0;
	
	if (pdata->io_init) {
		pdata->io_init();
		return 0;
	}
	
	//charge control pin
	if (pdata->charge_set_pin != INVALID_GPIO){
	    	ret = gpio_request(pdata->charge_set_pin, NULL);
	    	if (ret) {
	    		printk("failed to request dc_det gpio\n");
	    		goto error;
		    	}
	    	gpio_direction_output(pdata->charge_set_pin, 1 - pdata->charge_set_level);
	}
	
	//dc charge detect pin
	if (pdata->dc_det_pin != INVALID_GPIO){
	    	ret = gpio_request(pdata->dc_det_pin, NULL);
	    	if (ret) {
	    		printk("failed to request dc_det gpio\n");
	    		goto error;
	    	}
	
	    	//gpio_pull_updown(pdata->dc_det_pin, GPIOPullUp);//important
	    	ret = gpio_direction_input(pdata->dc_det_pin);
	    	if (ret) {
	    		printk("failed to set gpio dc_det input\n");
	    		goto error;
	    	}
	}
	
	//charge ok detect
	if (pdata->charge_ok_pin != INVALID_GPIO){
 		ret = gpio_request(pdata->charge_ok_pin, NULL);
	    	if (ret) {
	    		printk("failed to request charge_ok gpio\n");
	    		goto error;
	    	}
	
	    	//gpio_pull_updown(pdata->charge_ok_pin, GPIOPullUp);//important
	    	ret = gpio_direction_input(pdata->charge_ok_pin);
	    	if (ret) {
	    		printk("failed to set gpio charge_ok input\n");
	    		goto error;
	    	}
	}
	//batt low pin
	if( pdata->batt_low_pin != INVALID_GPIO){
 		ret = gpio_request(pdata->batt_low_pin, NULL);
	    	if (ret) {
	    		printk("failed to request batt_low_pin gpio\n");
	    		goto error;
	    	}
	
	    	//gpio_pull_updown(pdata->batt_low_pin, GPIOPullUp); 
	    	ret = gpio_direction_input(pdata->batt_low_pin);
	    	if (ret) {
	    		printk("failed to set gpio batt_low_pin input\n");
	    		goto error;
	    	}
	}

	//led control , charging:red light, working:green light.
	if( pdata->ctrl_charge_led_pin != INVALID_GPIO){
		ret = gpio_request(pdata->ctrl_charge_led_pin, NULL);
			if (ret) {
				printk("failed to request ctrl_charge_led_pin gpio\n");
				goto error;
			}
			gpio_direction_output(pdata->ctrl_charge_led_pin, 1);
	}

	if( pdata->ctrl_working_led_pin != INVALID_GPIO){
		ret = gpio_request(pdata->ctrl_working_led_pin, NULL);
			if (ret) {
				printk("failed to request ctrl_working_led_pin gpio\n");
				goto error;
			}
			gpio_direction_output(pdata->ctrl_working_led_pin, 0);
	}

	return 0;
error:
	return -1;
}

extern void kernel_power_off(void);
static void rk30_adc_battery_check(struct rk30_adc_battery_data *bat)
{
	int i;
	int level,oldlevel;
	struct rk30_adc_battery_platform_data *pdata = bat->pdata;
	//printk("%s--%d:\n",__FUNCTION__,__LINE__);

	bat->old_charge_level = -1;
	bat->capacitytmp = 0;
	bat->suspend_capacity = 0;
	
	pSamples = bat->adc_samples;

	//adc_sync_read(bat->client);                             //start adc sample yunboa
	adc_val_read(bat->battery_tz);
	level = oldlevel = rk30_adc_battery_status_samples(bat);//init charge status

	bat->full_times = 0;
	for (i = 0; i < NUM_VOLTAGE_SAMPLE; i++){                //0.1 s
		mdelay(1);
		rk30_adc_battery_voltage_samples(bat);              //get voltage
		//level = rk30_adc_battery_status_samples(bat);       //check charge status
		level = rk30_adc_battery_get_charge_level(bat);

		if (oldlevel != level){
			oldlevel = level;                               //if charge status changed, reset sample
			i = 0;
		}
		printk("s818_adc_battery_check ,level=%d,adb_samples[%d]=%d\n",level,i,bat->adc_samples[i]);
	}

	bat->bat_capacity = rk30_adc_battery_voltage_to_capacity(bat, bat->bat_voltage);  //init bat_capacity
	printk("s818_adc_battery_check ,bat_voltage =%d,bat_capacity=%d\n",bat->bat_voltage,bat->bat_capacity);
	printk("s818_adc_battery_check ,TA:%s,USB:%s,usb of USB:%s\n",extcon_get_cable_state(bat->edev, "TA")?"TRUE":"FALSE",
			extcon_get_cable_state(bat->edev, "USB")?"TRUE":"FALSE",
			extcon_get_cable_state(bat->usbdev, "USB")?"TRUE":"FALSE");
	
	bat->bat_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	if (rk30_adc_battery_get_charge_level(bat)){
		bat->bat_status = POWER_SUPPLY_STATUS_CHARGING;

		if (pdata->charge_ok_pin != INVALID_GPIO){
			if (gpio_get_value(pdata->charge_ok_pin) == pdata->charge_ok_level){
				bat->bat_status = POWER_SUPPLY_STATUS_FULL;
				bat->bat_capacity = 100;
			}
		}
	}


#if 0
#if 1
	rk30_adc_battery_poweron_capacity_check();
#else
	gBatteryData->poweron_check = 1;
#endif
	gBatteryData->poweron_check = 0;
#endif
/*******************************************
//开机采样到的电压和上次关机保存电压相差较大，怎么处理？
if (bat->bat_capacity > old_capacity)
{
if ((bat->bat_capacity - old_capacity) > 20)
{

}
}
else if (bat->bat_capacity < old_capacity)
{
if ((old_capacity > bat->bat_capacity) > 20)
{

}
}
*********************************************/
	if (bat->bat_capacity == 0) bat->bat_capacity = 1;


	if(1==gBatteryData -> pdata->low_voltage_protection)

		if ((bat->bat_voltage <= batt_table[0].dis_charge_vol)&&(bat->bat_status != POWER_SUPPLY_STATUS_CHARGING)){
			
			printk("low voltage ,power off \n");
			kernel_power_off();
		}

}

static void rk30_adc_battery_callback(struct adc_client *client, void *param, int result)
{
#if 0
	struct rk30_adc_battery_data  *info = container_of(client, struct rk30_adc_battery_data,
		client);
	info->adc_val = result;
#endif
	if (result < 0){
		pr_bat("adc_battery_callback    resule < 0 , the value ");
		return;
	}
	else{
		gBatteryData->adc_val = result;
		pr_bat("result = %d, gBatteryData->adc_val = %d\n", result, gBatteryData->adc_val );
	}
	return;
}

#if 1
static void rk30_adc_battery_lowerpower_delaywork(struct work_struct *work)
{
	int irq;
	if( gBatteryData->pdata->batt_low_pin != INVALID_GPIO){
		irq = gpio_to_irq(gBatteryData->pdata->batt_low_pin);
		disable_irq(irq);
	}

	printk("lowerpower\n");
 	tegra_send_wakeup_key(); // wake up the system	
	return;
}


static irqreturn_t rk30_adc_battery_low_wakeup(int irq,void *dev_id)
{
	queue_work(gBatteryData->wq, &gBatteryData->lowerpower_work);
	return IRQ_HANDLED;
}

#endif

static void charger_change_timer(unsigned long _data)
{
	struct rk30_adc_battery_data *bat =  (struct rk30_adc_battery_data*)_data;

	bat->charger_plug_in_check = 1;
	bat->gBatVoltageChargeCnt = 0;
}

void turn_off_all_led(void)
{
	if(gBatteryData)
	{
		if(gBatteryData->charger == 0)//If there is not charging, we will control the led.
		{
			if (gBatteryData->pdata->ctrl_charge_led_pin != INVALID_GPIO){
				gpio_direction_output(gBatteryData->pdata->ctrl_charge_led_pin, 0);
			}

			if (gBatteryData->pdata->ctrl_working_led_pin != INVALID_GPIO){
				gpio_direction_output(gBatteryData->pdata->ctrl_working_led_pin, 0);
			}
		}
	}
}

void turn_on_working_status_led(void)
{
	if(gBatteryData)
	{
		if((gBatteryData->charger == 0) || (gBatteryData->bat_capacity >= 90)) //If there is not charging or charging ok, we will control the led.
		{
			if (gBatteryData->pdata->ctrl_charge_led_pin != INVALID_GPIO){
				if(gpio_get_value(gBatteryData->pdata->ctrl_charge_led_pin)!=0)
					gpio_direction_output(gBatteryData->pdata->ctrl_charge_led_pin, 0);
			}

			if (gBatteryData->pdata->ctrl_working_led_pin != INVALID_GPIO){
				if(gpio_get_value(gBatteryData->pdata->ctrl_working_led_pin)!=1)
					gpio_direction_output(gBatteryData->pdata->ctrl_working_led_pin, 1);
			}
		}
	}
}

static int rk30_adc_battery_probe(struct platform_device *pdev)
{
	int    ret;
	int    irq;
	int    irq_flag;
	long   adc_val;
	struct adc_client                   *client;
	struct rk30_adc_battery_data          *data;
	struct rk30_adc_battery_platform_data *pdata = pdev->dev.platform_data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		ret = -ENOMEM;
		goto err_data_alloc_failed;
	}
	
	memset(data, 0, sizeof(struct rk30_adc_battery_data));
	gBatteryData = data;
	platform_set_drvdata(pdev, data);

   	data->pdata = pdata;
	data->status_lock = 0; 	
	ret = rk30_adc_battery_io_init(pdata);
	 if (ret) {
	 	goto err_io_init;
	}

    data->battery_tz = thermal_zone_device_find_by_name(BAT_TZ);
	bat_tz = data->battery_tz; 
	memset(data->adc_samples, 0, sizeof(int)*(NUM_VOLTAGE_SAMPLE + 2));

	data->edev = extcon_get_extcon_dev(EXTCON_NAME);
	data->usbdev = extcon_get_extcon_dev(EXTCON_NAME2);

	 //register adc for battery sample
	/* if(0 == pdata->adc_channel)
		client = adc_register(0, rk30_adc_battery_callback, NULL);  //pdata->adc_channel = ani0
	else
		client = adc_register(pdata->adc_channel, rk30_adc_battery_callback, NULL);  
	if(!client)
		goto err_adc_register_failed;*/
	    
	 //variable init
	//data->client  = client;
	//data->adc_val = adc_sync_read(client);
	adc_val_read(data->battery_tz);
	data->adc_val = gBatteryData->adc_val; //yunboa
	data->charger = NONE;

	ret = power_supply_register(&pdev->dev, &rk30_battery_supply);
	if (ret){
		printk(KERN_INFO "fail to battery power_supply_register\n");
		goto err_battery_failed;
	}

	ret = device_create_file(&pdev->dev,&dev_attr_jxd_bat);
	if(ret < 0) {
		printk(KERN_ALERT"Failed to create attribute jxd_bat.");

	}

	ret = device_create_file(&pdev->dev,&dev_attr_debug);
	if(ret < 0) {
		printk(KERN_ALERT"Failed to create attribute debug.");

	}

#if defined (CONFIG_BATTERY_RK30_USB_CHARGE)
	ret = power_supply_register(&pdev->dev, &rk30_usb_supply);
	if (ret){
		printk(KERN_INFO "fail to usb power_supply_register\n");
		goto err_usb_failed;
	}
#endif
 	wake_lock_init(&batt_wake_lock, WAKE_LOCK_SUSPEND, "batt_lock");	

	data->wq = create_singlethread_workqueue("adc_battd");
	INIT_DELAYED_WORK(&data->delay_work, rk30_adc_battery_timer_work);

	setup_timer(&data->timer,charger_change_timer, (unsigned long)data);

	//Power on Battery detect
	//rk30_adc_battery_check(data);
	if(1 == pdata->save_capacity ){
		//queue_delayed_work(data->wq, &data->delay_work, msecs_to_jiffies(TIMER_MS_COUNTS*10));
		queue_delayed_work(data->wq, &data->delay_work, msecs_to_jiffies(TIMER_MS_COUNTS*4));
		gBatteryData->poweron_check = 1;
	}else{
		queue_delayed_work(data->wq, &data->delay_work, msecs_to_jiffies(TIMER_MS_COUNTS));
		gBatteryData->poweron_check = 0;
	}

#if  defined (CONFIG_BATTERY_RK30_AC_CHARGE)
	ret = power_supply_register(&pdev->dev, &rk30_ac_supply);
	if (ret) {
		printk(KERN_INFO "fail to ac power_supply_register\n");
		goto err_ac_failed;
	}
	//init dc dectet irq & delay work
	if (pdata->dc_det_pin != INVALID_GPIO){
		INIT_WORK(&data->dcwakeup_work, rk30_adc_battery_dcdet_delaywork);
		
		irq = gpio_to_irq(pdata->dc_det_pin);	        
		irq_flag = gpio_get_value (pdata->dc_det_pin) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	    	ret = request_irq(irq, rk30_adc_battery_dc_wakeup, irq_flag, "ac_charge_irq", NULL);
	    	if (ret) {
	    		printk("failed to request dc det irq\n");
	    		goto err_dcirq_failed;
	    	}
	    	enable_irq_wake(irq);  
    	
	}
#endif

#if 1
	// batt low irq lowerpower_work
	if( pdata->batt_low_pin != INVALID_GPIO){
		INIT_WORK(&data->lowerpower_work, rk30_adc_battery_lowerpower_delaywork);
		
		irq = gpio_to_irq(pdata->batt_low_pin);
	    	ret = request_irq(irq, rk30_adc_battery_low_wakeup, IRQF_TRIGGER_LOW, "batt_low_irq", NULL);

	    	if (ret) {
	    		printk("failed to request batt_low_irq irq\n");
	    		goto err_lowpowerirq_failed;
	    	}
		disable_irq(irq);
    	
    	}
#endif

	printk(KERN_INFO "s818_adc_battery: driver initialized\n");
	
	return 0;
	
#if defined (CONFIG_BATTERY_RK30_USB_CHARGE)
err_usb_failed:
	power_supply_unregister(&rk30_usb_supply);
#endif

err_ac_failed:
#if defined (CONFIG_BATTERY_RK30_AC_CHARGE)
	power_supply_unregister(&rk30_ac_supply);
#endif

err_battery_failed:
	power_supply_unregister(&rk30_battery_supply);
    
err_dcirq_failed:
	free_irq(gpio_to_irq(pdata->dc_det_pin), data);
#if 1
 err_lowpowerirq_failed:
	free_irq(gpio_to_irq(pdata->batt_low_pin), data);
#endif
err_adc_register_failed:
err_io_init:    
err_data_alloc_failed:
	kfree(data);

	printk("s818_adc_battery: error!\n");
    
	return ret;
}

static int rk30_adc_battery_remove(struct platform_device *pdev)
{
	struct rk30_adc_battery_data *data = platform_get_drvdata(pdev);
	struct rk30_adc_battery_platform_data *pdata = pdev->dev.platform_data;

	cancel_delayed_work(&gBatteryData->delay_work);	
#if defined(CONFIG_BATTERY_RK30_USB_CHARGE)
	power_supply_unregister(&rk30_usb_supply);
#endif
#if defined(CONFIG_BATTERY_RK30_AC_CHARGE)
	power_supply_unregister(&rk30_ac_supply);
#endif
	power_supply_unregister(&rk30_battery_supply);

	free_irq(gpio_to_irq(pdata->dc_det_pin), data);

	kfree(data);
	
	return 0;
}

static struct platform_driver rk30_adc_battery_driver = {
	.probe		= rk30_adc_battery_probe,
	.remove		= rk30_adc_battery_remove,
	.suspend		= rk30_adc_battery_suspend,
	.resume		= rk30_adc_battery_resume,
	.driver = {
		.name = "rk30-battery",
		.owner	= THIS_MODULE,
	}
};

static int __init rk30_adc_battery_init(void)
{
	return platform_driver_register(&rk30_adc_battery_driver);
}

static void __exit rk30_adc_battery_exit(void)
{
	platform_driver_unregister(&rk30_adc_battery_driver);
}

//subsys_initcall(rk30_adc_battery_init);//subsys_initcall(rk30_adc_battery_init);
module_init(rk30_adc_battery_init);//subsys_initcall(rk30_adc_battery_init);

module_exit(rk30_adc_battery_exit);

MODULE_DESCRIPTION("Battery detect driver for the rk30");
MODULE_AUTHOR("luowei lw@rock-chips.com");
MODULE_LICENSE("GPL");
