#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/poll.h>

#define WG_CMD_MAX_NR 		7 
#define WG_CMD_MAGIC 		'x'
#define WG_26_MODE			_IO(WG_CMD_MAGIC, 0x01)
#define WG_34_MODE			_IO(WG_CMD_MAGIC, 0x02)
#define WG_66_MODE			_IO(WG_CMD_MAGIC, 0x03)
#define WG_UNKNOWN_MODE 	_IO(WG_CMD_MAGIC, 0x07)


static void recive_data_convert(void);


static int major;
static dev_t devid;
static struct cdev wiegand_cdev;
static struct class *cls;
static unsigned char wiegand[66];  
static int bit_count;    //Global Bit Counter
static unsigned long barcode;
static unsigned long long barcode_66;
static DECLARE_WAIT_QUEUE_HEAD(read_waitq);//定义读等待队列头
static int convert_finish_flag = 0;
static struct timer_list refresh_timer;
static int flag_timeout = 0;
static int flag_recive_mode;


struct wiegand_io  {
	int pin_num;
	char *name;
	int flag_input;	// if input mode, flag set to 1
};

static struct wiegand_io wiegand_set[] = {
	{194, "WGN_IN_D0", 1},
	{195, "WGN_IN_D1", 1},
	{233, "WGN_OUT_D0", 0},
	{232, "WGN_OUT_D1", 0},
};


/* 定时器中断服务程序
  *  主要功能：处理数据，完成韦根数据转换
  *  				并清空计数值。
  */
static void refresh_timer_function(unsigned long data)
{
	flag_timeout = 1;
	if(bit_count== 26){
		flag_recive_mode = WG_26_MODE;
	}else if(bit_count== 34){
		flag_recive_mode = WG_34_MODE;
	}else if(bit_count== 66){
		flag_recive_mode = WG_66_MODE;
	}else{
		flag_recive_mode = WG_UNKNOWN_MODE;
	}
	recive_data_convert();
	bit_count = 0;	
}

static unsigned char even_parity_26(unsigned long wg_data)   
{   
	unsigned char i, even_val;  
	even_val = 0; 
	for(i=12;i<24;i++){
		if(((wg_data >> i) & 0x01) == 0x01){   
			even_val++;   
		}   
	}   
	if((even_val & 0x01) == 0x01)
		even_val = 1;   
	else 
		even_val = 0;   
	return even_val;      
}


static unsigned char odd_parity_26(unsigned long wg_data)   
{   
	unsigned char i, odd_val;  
	odd_val = 0; 
	for(i = 0; i < 12; i++){
		if(((wg_data >> i) & 0x01) == 0x01){   
			odd_val++;   
		}   
	}   
	if((odd_val & 0x01) == 0x01) 
		odd_val = 0;   
	else
		odd_val = 1;   
	return odd_val;   
} 

static unsigned char even_parity_34(unsigned long long wg_data)   
{   
	unsigned char i, even_val;  
	even_val = 0; 
	for(i = 16; i < 32; i++){
		if(((wg_data >> i) & 0x01) == 0x01){   
			 even_val++;   
		}   
	}   
	if((even_val & 0x01) == 0x01)
		even_val = 1;   
	else 
		even_val = 0;   
	return even_val; 
}   


static unsigned char odd_parity_34(unsigned long long wg_data)   
{   
	unsigned char i, odd_val;  
	odd_val = 0; 
	for(i = 0; i < 16; i++){
		if(((wg_data >> i) & 0x01) == 0x01){   
			odd_val++;   
		}   
	}   
	if((odd_val & 0x01 ) == 0x01) 
		odd_val = 0;   
	else
		odd_val = 1;   
	return odd_val;   
}

static unsigned char even_parity_66(unsigned long long wg_data)   
{   
	unsigned char i, even_val;  
	even_val = 0; 
	for(i = 32; i < 64; i++){
		if(((wg_data >> i) & 0x01) == 0x01){   
			 even_val++;   
		}   
	}   
	if((even_val & 0x01) == 0x01)
		even_val = 1;   
	else 
		even_val = 0;   
	return even_val; 
}   


static unsigned char odd_parity_66(unsigned long long wg_data)   
{   
	unsigned char i, odd_val;  
	odd_val = 0; 
	for(i = 0; i < 32; i++){
		if(((wg_data >> i) & 0x01) == 0x01){   
			odd_val++;   
		}   
	}   
	if((odd_val & 0x01 ) == 0x01) 
		odd_val = 0;   
	else
		odd_val = 1;   
	return odd_val;   
}


static unsigned char wiegand_26_to_barcode(unsigned long *data)
{
	int i,even,odd,hid,pid;
	//偶校验  
	even = 0;    	
	for(i = 1; i < 13;i++)    	{
		if(wiegand[i] == 1)		 
			even = (~even) & 0x01;  
	}
	
	if(even != wiegand[0]){
		bit_count = 0;	
		goto error;      	
	}		

	//寄校验     	 
	odd = 1;    	
	for(i = 13; i< 25;i++) {	    
		if(wiegand[i] == 1)		
			odd = (~odd)& 0x01;	          
	}   

	if(odd != wiegand[25]) {	
		bit_count = 0;	
		goto error;     	 
	}	

	//奇偶校验通过	
	//hid转换
	hid = 0;	
	for(i = 1 ;i<=12;i++){
		hid  |= (0x01 & wiegand[i]) << (12-i);
	}

	//pid转换
	pid = 0;	
	for(i = 13 ;i<25;i++){
		pid |= (0x01 & wiegand[i]) << (25-i-1);
	}

	bit_count = 0;	

	*data = (hid << 16) | (pid);

	return 0;
error:	
	printk("wiegand_26 Parity Efficacy Error!\n");	
	return -1;

}



static unsigned char wiegand_34_to_barcode(unsigned long *data)
{
	int i,even,odd,hid,pid;

	//偶校验  
	even = 0;    	
	for(i = 1; i < 17;i++)    	{
		if(wiegand[i] == 1)		 
			even = (~even) & 0x01;  
	}
	
	if(even != wiegand[0]){
		bit_count = 0;	
		goto error;      	
	}		

	//寄校验     	 
	odd = 1;    	
	for(i = 17; i< 33;i++) {	    
		if(wiegand[i] == 1)		
			odd = (~odd)& 0x01;	          
	}   

	if(odd != wiegand[33]) {	
		bit_count = 0;	
		goto error;     	 
	}	

	//奇偶校验通过	
	//hid转换
	hid = 0;	
	for(i = 1 ;i<=16;i++){
		hid  |= (0x01 & wiegand[i]) << (16-i);
	}

	//pid转换
	pid = 0;	
	for(i = 17 ;i<33; i++){
		pid |= (0x01 & wiegand[i]) << (33-i-1);
	}

	bit_count = 0;	

	*data = (hid << 16) | (pid);
	
	return 0;
error:	
	printk("wiegand_34 Parity Efficacy Error!\n");	
	return -1;

}


static unsigned char wiegand_66_to_barcode(unsigned long long *data)
{
	int i;
	unsigned long long hid,pid,even,odd;
	//奇校验  
	even = 0;    	
	for(i = 1; i < 33;i++)    	{
		if(wiegand[i] == 1)		 
			even = (~even) & 0x01;  
	}
	
	if(even != wiegand[0]){
		bit_count = 0;	
		goto error;      	
	}		

	//偶校验     	 
	odd = 1;    	
	for(i = 33; i< 65;i++) {	    
		if(wiegand[i] == 1)		
			odd = (~odd)& 0x01;	          
	}   

	if(odd != wiegand[65]) {	
		bit_count = 0;	
		goto error;     	 
	}	

	//奇偶校验通过	
	//hid转换
	hid = 0;	
	for(i = 1 ;i<=32;i++){
		hid  |= (0x01 & wiegand[i]) << (32-i);
	}

	//pid转换
	pid = 0;	
	for(i = 33 ;i<65;i++){
		pid |= (0x01 & wiegand[i]) << (65-i-1);
	}

	bit_count = 0;	

	*data = (hid << 32) | (pid);

	return 0;
error:	
	printk("Parity Efficacy Error!\n");	
	return -1;

}

static unsigned long barcode_to_wiegand_26(unsigned char *barcode, int size)
{
	unsigned long wiegand_code = 0;
	unsigned int tmp_l;
	unsigned int tmp_h = barcode[size-3];
	tmp_l = barcode[size-2] << 8 | barcode[size -1];
	wiegand_code = (tmp_h * 100000) + tmp_l;
	return wiegand_code;
}

static unsigned long long barcode_to_wiegand_34(unsigned char *barcode, int size)
{
	unsigned long long wiegand_code = 0;
	unsigned long long tmp_l;
	unsigned long long tmp_h = barcode[size-4] << 8 | barcode[size -3];
	tmp_l = barcode[size -2] << 8 | barcode[size -1];
	wiegand_code = (tmp_h * 100000) + tmp_l; 	
	return wiegand_code;
}

/*  处理接收得到的数据,完成转换
  *  并从队列中唤醒读等待休眠进程。
  */
static void recive_data_convert(void)
{
	switch(flag_recive_mode){
	case WG_26_MODE:
		wiegand_26_to_barcode(&barcode);
		convert_finish_flag = 1;
		wake_up_interruptible(&read_waitq);  
		break;
	case WG_34_MODE:
		wiegand_34_to_barcode(&barcode);
		convert_finish_flag = 1;
		wake_up_interruptible(&read_waitq); 
		break;		
	case WG_66_MODE:
		wiegand_66_to_barcode(&barcode_66);
		convert_finish_flag = 1;
		wake_up_interruptible(&read_waitq); 
		break;
	case WG_UNKNOWN_MODE:
		// nothing to do
		break;
	}
}

/* 
  * 韦根输入数据1  的输入中断函数 
  */
static irqreturn_t wiegand_irq0(int irq, void *dev_id) //   data 1
{
	disable_irq_nosync(gpio_to_irq(wiegand_set[0].pin_num));

	if(flag_timeout){
		flag_timeout = 0;
	}

	wiegand[bit_count] = 1;
	bit_count++;
	enable_irq(gpio_to_irq(wiegand_set[0].pin_num));
	mod_timer(&refresh_timer, jiffies+HZ/4);  // 250ms

	return IRQ_HANDLED;
}

/* 
  * 韦根输入数据0  的输入中断函数 
  */
static irqreturn_t wiegand_irq1(int irq, void *dev_id)  // data 0
{
	disable_irq_nosync(gpio_to_irq(wiegand_set[1].pin_num));

	if(flag_timeout){
		flag_timeout = 0;
	}

	wiegand[bit_count] = 0;
	bit_count++;
	enable_irq(gpio_to_irq(wiegand_set[1].pin_num));
	mod_timer(&refresh_timer, jiffies+HZ/4);  // 250ms

	return IRQ_HANDLED;
}




static void set_wiegand_data0(int val)
{
	if(val){
		gpio_set_value(wiegand_set[2].pin_num, 1);
	}else{
		gpio_set_value(wiegand_set[2].pin_num, 0);
	}
}

static void set_wiegand_data1(int val)
{
	if(val){
		gpio_set_value(wiegand_set[3].pin_num, 1);
	}else{
		gpio_set_value(wiegand_set[3].pin_num, 0);
	}
}


static void wiegand_write_bit(int val)
{
	if(!val){
		set_wiegand_data0(1);
		udelay(100);
		set_wiegand_data0(0);
		//printk("0 ");
	}else{
		set_wiegand_data1(1);
		udelay(100);
		set_wiegand_data1(0);
		//printk("1 ");
	}
}


static void wiegand_26_send(unsigned long wg_data)
{
	int i;
	set_wiegand_data0(1);
	set_wiegand_data1(1);
	udelay(10);

	//printk("wiegand_26_data:%ld\n",wg_data);
	//send even bit
	if(even_parity_26(wg_data) == 1)
		wiegand_write_bit(1);
	else 
		wiegand_write_bit(0);

	//send data 
	for(i = 23; i >= 0; i--){
		if(((wg_data >> i) & 0x01) == 0x01) 
			wiegand_write_bit(1);
		else
			wiegand_write_bit(0);
	}

	//send odd bit 
	if(odd_parity_26(wg_data)==1)
		wiegand_write_bit(1);
	else
		wiegand_write_bit(0);

	//resume
	set_wiegand_data0(1);
	set_wiegand_data1(1);
}


static void wiegand_34_send(unsigned long long wg_data)   
{ 
	int i;  
	set_wiegand_data0(1);
	set_wiegand_data1(1);
	udelay(10);

	//printk("wiegand_34_data:%llu\n",wg_data);
	//Even bit
	if(even_parity_34(wg_data) == 1)
		wiegand_write_bit(1);
	else 
		wiegand_write_bit(0);
	

	//send data
	for(i = 31; i >= 0; i--){
		if(((wg_data >> i) & 0x01) == 0x01) 
			wiegand_write_bit(1);
		else
			wiegand_write_bit(0);
	}
	
	//Odd bit
	if(odd_parity_34(wg_data) == 1)
		wiegand_write_bit(1);
	else
		wiegand_write_bit(0);

	//resume
	set_wiegand_data0(1);
	set_wiegand_data1(1);
}


static void wiegand_66_send(unsigned long long wg_data)   
{ 
	int i;  
	set_wiegand_data0(1);
	set_wiegand_data1(1);
	udelay(10);

	//printk("wiegand_66_data:%llu\n",wg_data);
	//Even bit
	if(even_parity_66(wg_data) == 1)
		wiegand_write_bit(1);
	else 
		wiegand_write_bit(0);
	

	//send data
	for(i = 63; i >= 0; i--){
		if(((wg_data >> i) & 0x01) == 0x01) 
			wiegand_write_bit(1);
		else
			wiegand_write_bit(0);
	}
	
	//Odd bit
	if(odd_parity_66(wg_data) == 1)
		wiegand_write_bit(1);
	else
		wiegand_write_bit(0);

	//resume
	set_wiegand_data0(1);
	set_wiegand_data1(1);
}

static int wiegand_open(struct inode *inode, struct file *file)
{
	printk("wiegand_open ok.\n");
	return 0;
}


/* 韦根ioctl  接口函数，上层应用通过ioctl(fd, cmd, &data)
  *  间接调用此函数。
  */
static long wiegand_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	unsigned char barcode[4];
	unsigned long wiegand_code_26;
	unsigned long long wiegand_code_34;
	unsigned long long wiegand_code_66;
 	if(_IOC_TYPE(cmd) != WG_CMD_MAGIC) 
		return - EINVAL;

 	if(_IOC_NR(cmd) > WG_CMD_MAX_NR) 
		return - EINVAL;

	switch(cmd){
	case WG_26_MODE:
		if(copy_from_user(barcode, (unsigned char*)arg, 3)){
			return -EFAULT;
		}
		wiegand_code_26 = barcode_to_wiegand_26(barcode,3);
		wiegand_26_send(wiegand_code_26);
		break;
	case WG_34_MODE:
		if(copy_from_user(barcode, (unsigned char*)arg, 4)){
			return -EFAULT;
		}
		wiegand_code_34 = barcode_to_wiegand_34(barcode,4);
		wiegand_34_send(wiegand_code_34);
		break;
	case WG_66_MODE:
		if(copy_from_user(&wiegand_code_66, (unsigned char*)arg, 8)){
			return -EFAULT;
		}
		//didn't known how to transform wiegand 66 data.
		wiegand_66_send(wiegand_code_66);
		break;
	default:
		err =  -1;
		break;
	}
	return err;
}

/* 
  * 韦根读函数，没有数据时，上层应用通过read(fd, buf, size)
  *  间接调用 会导致此函数休眠，直到有数据过来才能从队列中唤醒返回
  */
ssize_t wiegand_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	int err;
	//enable_irq(gpio_to_irq(wiegand_set[0].pin_num));
	//enable_irq(gpio_to_irq(wiegand_set[1].pin_num));

	wait_event_interruptible(read_waitq, convert_finish_flag);//数据未转换完成，在此休眠等待
	convert_finish_flag = 0;

	if(bit_count == 66){
		err = copy_to_user(buf, &barcode_66, sizeof(unsigned long long));
		if(err){
			printk("%s copy_to_user error(%d)\n", __func__, err);
			return -1;
		}
	}else{
		err = copy_to_user(buf, &barcode, sizeof(unsigned long));
		if(err){
			printk("%s copy_to_user error(%d)\n", __func__, err);
			return -1;
		}
	}

	bit_count = 0;

	//disable_irq_nosync(gpio_to_irq(wiegand_set[0].pin_num));
	//disable_irq_nosync(gpio_to_irq(wiegand_set[1].pin_num));
	return 0;
}	


static ssize_t wiegand_write (struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
	return 0;
}



/* 上层应用可通过poll  机制查询数据情况
  *  避免不必要的休眠等待
  */
static unsigned wiegand_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	poll_wait(file, &read_waitq, wait); //数据不可读，不会立即休眠

	if (convert_finish_flag)
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static struct file_operations wiegand_fops = {
	.owner = THIS_MODULE,
	.open  = wiegand_open,
	.unlocked_ioctl = wiegand_ioctl,
	.read = wiegand_read,
	.write = wiegand_write,
	.poll = wiegand_poll,
};



static int __init wiegand_init(void)
{
	int err, i;

	if (major) {
		devid = MKDEV(major, 0);
		register_chrdev_region(devid, 1, "wiegand");  
	} else {
		alloc_chrdev_region(&devid, 0, 1, "wiegand"); 
		major = MAJOR(devid);                     
	}
	
	cdev_init(&wiegand_cdev, &wiegand_fops);
	cdev_add(&wiegand_cdev, devid, 1);

	cls = class_create(THIS_MODULE, "wiegand");
	device_create(cls, NULL, devid, NULL, "wiegand"); 	/* /dev/wiegand */

	init_timer(&refresh_timer);
	refresh_timer.function = refresh_timer_function;
	add_timer(&refresh_timer);


	for(i = 0; i < ARRAY_SIZE(wiegand_set); i++){
		err = gpio_request(wiegand_set[i].pin_num,wiegand_set[i].name);
		if(err){
			printk("Cannot Request the gpio of  %d\n", wiegand_set[i].pin_num);
			goto out;
		}
		if(wiegand_set[i].flag_input){
			err = gpio_direction_input(wiegand_set[i].pin_num);
			if (err < 0) {
				printk("Cannot set the gpio to input mode. \n");
				gpio_free(wiegand_set[i].pin_num);
				goto out;
			}else{
				printk("set the gpio-%d to input mode. \n",wiegand_set[i].pin_num);
			}

		}else{
			err = gpio_direction_output(wiegand_set[i].pin_num, 1);
			if (err < 0) {
				printk("Cannot set the gpio to output mode. \n");
				gpio_free(wiegand_set[i].pin_num);
				goto out;
			}else{
				printk("set the gpio-%d to output mode. \n",wiegand_set[i].pin_num);
			}
		}
	}


	err = request_irq(gpio_to_irq(wiegand_set[0].pin_num), wiegand_irq0,
			IRQF_TRIGGER_FALLING, "WIEGAND_IN_D0", &wiegand_set[0]);
	if(err){
		printk("%s:%d request IRQ(%d),ret:%d failed!\n",__func__,__LINE__, gpio_to_irq(wiegand_set[0].pin_num),err);
		goto out;
	}

	err = request_irq(gpio_to_irq(wiegand_set[1].pin_num), wiegand_irq1,
			IRQF_TRIGGER_FALLING, "WIEGAND_IN_D1", &wiegand_set[1]);
	if(err){
		printk("%s:%d request IRQ(%d),ret:%d failed!\n",__func__, __LINE__, gpio_to_irq(wiegand_set[1].pin_num),err);
		free_irq(gpio_to_irq(wiegand_set[0].pin_num), &wiegand_set[0]);
		goto out;
	}

	//disable_irq_nosync(gpio_to_irq(wiegand_set[0].pin_num));
	//disable_irq_nosync(gpio_to_irq(wiegand_set[1].pin_num));



	return 0;
out:
	while(i--){
		gpio_free(wiegand_set[i].pin_num);
	}
	return -1;
	
}

static void __exit wiegand_exit(void)
{
	int i; 

	del_timer(&refresh_timer);
	for(i = 0; i < ARRAY_SIZE(wiegand_set); i++){
		if(wiegand_set[i].flag_input){
			free_irq(gpio_to_irq(wiegand_set[i].pin_num), &wiegand_set[i]);
		}
		gpio_free(wiegand_set[i].pin_num);
	}
	
	device_destroy(cls, devid);
	class_destroy(cls);

	cdev_del(&wiegand_cdev);
	unregister_chrdev_region(devid, 1);
}

module_init(wiegand_init);
module_exit(wiegand_exit);


MODULE_AUTHOR ("mr_xiaogui@163.com");
MODULE_DESCRIPTION("WIEGAND DRIVER");
MODULE_LICENSE("GPL");

