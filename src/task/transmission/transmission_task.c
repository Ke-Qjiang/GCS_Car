/*
* Change Logs:
* Date            Author          Notes
* 2023-10-09      ChenSihan     first version
* 2023-12-09      YangShuo     USB虚拟串口
*/

#include "transmission_task.h"
#include "drv_gpio.h"

#define DBG_TAG   "rm.task"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>
#define HEART_BEAT 500 //ms
/* -------------------------------- 线程间通讯话题相关 ------------------------------- */
static struct gimbal_cmd_msg gim_cmd;
static struct ins_msg ins_data;
static struct gimbal_fdb_msg gim_fdb;
static struct trans_fdb_msg trans_fdb;
/*------------------------------传输数据相关 --------------------------------- */
#define RECV_BUFFER_SIZE 64  // 接收环形缓冲区大小
rt_uint8_t r_buffer[RECV_BUFFER_SIZE];  // 接收环形缓冲区
rt_uint8_t *r_buffer_point; //用于清除环形缓冲区buffer的指针
struct rt_ringbuffer receive_buffer ; // 环形缓冲区对象控制块指针
rt_uint8_t buf[31] = {0};
RpyTypeDef rpy_tx_data={
        .HEAD = 0XFF,
        .D_ADDR = MAINFLOD,
        .ID = GIMBAL,
        .LEN = FRAME_RPY_LEN,
        .DATA={0},
        .SC = 0,
        .AC = 0,
};
RpyTypeDef rpy_rx_data; //接收解析结构体
static rt_uint32_t heart_dt;
/* ---------------------------------usb虚拟串口数据相关 --------------------------------- */
static rt_device_t serial_port = RT_NULL;
/* -------------------------------- 线程间通讯话题相关 ------------------------------- */
static publisher_t *pub_trans;
static subscriber_t *sub_cmd,*sub_ins,*sub_gim;
static void trans_sub_pull(void);
static void trans_pub_push(void);
static void trans_sub_init(void);
static void trans_pub_init(void);

/**
 * @brief trans 线程中所有订阅者初始化（如有其它数据需求可在其中添加）
 */
static void trans_sub_init(void)
{
    sub_cmd = sub_register("gim_cmd", sizeof(struct gimbal_cmd_msg));
    sub_ins = sub_register("ins_msg", sizeof(struct ins_msg));
    sub_gim = sub_register("gim_fdb", sizeof(struct gimbal_fdb_msg));

}

/**
 * @brief trans 线程中所有订阅者获取更新话题（如有其它数据需求可在其中添加）
 */
static void trans_sub_pull(void)
{
    sub_get_msg(sub_cmd, &gim_cmd);
    sub_get_msg(sub_ins, &ins_data);
    sub_get_msg(sub_gim, &gim_fdb);
}

/**
 * @brief cmd 线程中所有发布者初始化
 */
static void trans_pub_init(void)
{
    pub_trans = pub_register("trans_fdb",sizeof(struct trans_fdb_msg));
}

/**
 * @brief cmd 线程中所有发布者推送更新话题
 */
static void trans_pub_push(void)
{
    pub_push_msg(pub_trans,&trans_fdb);
}

/* --------------------------------- 通讯线程入口 --------------------------------- */
static float trans_dt;


#include "drv_gpio.h"

struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;  /* 初始化配置参数 */



void transmission_task_entry(void* argument)
{
    static int trans_start;
    static float heart_start;
    /*订阅数据初始化*/
    trans_sub_init();
    /*发布数据初始化*/
    trans_pub_init();
    /* step1：查找名为 "vcom" 的虚拟串口设备*/

    serial_port = rt_device_find("uart2");
    /* step2：打开串口设备。以中断接收及轮询发送模式打开串口设备*/
    if (serial_port)
        rt_device_open(serial_port,  RT_DEVICE_FLAG_DMA_RX );
    else
        LOG_I("VS open failed");

    char str[] = "hello RT-Thread!\r\n";
    /*环形缓冲区初始化*/
    rt_ringbuffer_init(&receive_buffer, r_buffer, RECV_BUFFER_SIZE);
    /*清除buffer的指针赋地址*/
    r_buffer_point=r_buffer;
    /* 设置接收回调函数 */
    rt_device_set_rx_indicate(serial_port, usb_input);
    LOG_I("Transmission Task "
          ""
          "Start");
    while (1)
    {


//        rt_pin_mode(GET_PIN(A,2), PIN_MODE_OUTPUT);
//        rt_pin_write(GET_PIN(A,2), PIN_HIGH);
//        LOG_I("LED_G_HIGH");
        rt_thread_mdelay(500);
//        rt_pin_write(GET_PIN(A,2), PIN_LOW);
//        LOG_I("LED_G_LOW");
//        rt_thread_mdelay(500);

        trans_start = rt_tick_get();
//        while(1) {
//            LOG_I("%d",rt_tick_get());
//            rt_thread_mdelay(1000);
//
//        }
        LOG_I("%d",rt_tick_get());
        uint8_t time_test = trans_start;
        LOG_I("%d",rt_tick_get());
        rt_thread_mdelay(1000);
        LOG_I("%d",rt_tick_get());
//        uint8_t time_test = trans_start;
        rt_device_write(serial_port, 0, &time_test, (sizeof(time_test) - 1));

        /*订阅数据更新*/
        trans_sub_pull();
/*--------------------------------------------------具体需要发送的数据--------------------------------- */
        if((dwt_get_time_ms()-heart_dt)>=HEART_BEAT)
        {
//            rt_device_control(serial_port, RT_DEVICE_CTRL_RESUME, (void *) RT_DEVICE_FLAG_INT_TX);
        }
        Send_to_pc(rpy_tx_data);
/*--------------------------------------------------具体需要发送的数据---------------------------------*/
        /* 发布数据更新 */
        trans_pub_push();
        /* 用于调试监测线程调度使用 */
        trans_dt = dwt_get_time_ms() - trans_start;
        if (trans_dt > 1)
                LOG_E("Transmission Task is being DELAY! dt = [%f]", &trans_dt);
        rt_thread_mdelay(1);
    }
}

void Send_to_pc(RpyTypeDef data_r)
{
    /*填充数据*/
    pack_Rpy(&data_r, (gim_fdb.yaw_offset_angle - ins_data.yaw), (ins_data.pitch - gim_fdb.pit_offset_angle), ins_data.roll);
    Check_Rpy(&data_r);

    rt_device_write(serial_port, 0, (uint8_t*)&"Hello,World!", sizeof("Hello,World!"));
}

void pack_Rpy(RpyTypeDef *frame, float yaw, float pitch,float roll)
{
    int8_t rpy_tx_buffer[FRAME_RPY_LEN] = {0} ;
    int32_t rpy_data = 0;
    uint32_t *gimbal_rpy = (uint32_t *)&rpy_data;

    rpy_tx_buffer[0] = 0;
    rpy_data = yaw * 1000;
    rpy_tx_buffer[1] = *gimbal_rpy;
    rpy_tx_buffer[2] = *gimbal_rpy >> 8;
    rpy_tx_buffer[3] = *gimbal_rpy >> 16;
    rpy_tx_buffer[4] = *gimbal_rpy >> 24;
    rpy_data = pitch * 1000;
    rpy_tx_buffer[5] = *gimbal_rpy;
    rpy_tx_buffer[6] = *gimbal_rpy >> 8;
    rpy_tx_buffer[7] = *gimbal_rpy >> 16;
    rpy_tx_buffer[8] = *gimbal_rpy >> 24;
    rpy_data = roll *1000;
    rpy_tx_buffer[9] = *gimbal_rpy;
    rpy_tx_buffer[10] = *gimbal_rpy >> 8;
    rpy_tx_buffer[11] = *gimbal_rpy >> 16;
    rpy_tx_buffer[12] = *gimbal_rpy >> 24;

    memcpy(&frame->DATA[0], rpy_tx_buffer,13);

    frame->LEN = FRAME_RPY_LEN;
}

void Check_Rpy(RpyTypeDef *frame)
{
    uint8_t sum = 0;
    uint8_t add = 0;

    sum += frame->HEAD;
    sum += frame->D_ADDR;
    sum += frame->ID;
    sum += frame->LEN;
    add += sum;

    for (int i = 0; i < frame->LEN; i++)
    {
        sum += frame->DATA[i];
        add += sum;
    }

    frame->SC = sum & 0xFF;
    frame->AC = add & 0xFF;
}

// 串口接收到数据后产生中断，调用此回调函数
static rt_err_t usb_input(rt_device_t dev, rt_size_t size)
{
    memset(buf, 0, sizeof(buf));

    // 从串口读取数据并保存到环形接收缓冲区
    rt_uint32_t rx_length;
    rx_length = rt_device_read(serial_port, 0, buf, size);

        // 将接收到的数据放入环形缓冲区
        rt_ringbuffer_put_force(&receive_buffer, buf, rx_length);
    LOG_W((uint8_t*)&buf);

    rt_uint8_t frame_rx[sizeof(RpyTypeDef)]={0};
    rt_ringbuffer_get(&receive_buffer, frame_rx, sizeof(frame_rx));
    LOG_W((uint8_t*)&frame_rx);
    if(*(uint8_t*)frame_rx==0xFF)
    {
        memcpy(&rpy_rx_data,&frame_rx,sizeof(rpy_rx_data));
        switch (rpy_rx_data.ID) {
            case CHASSIS_CTRL:{
                trans_fdb.linear_x = (*(int32_t *) &rpy_rx_data.DATA[0] / 10000.0);
                trans_fdb.linear_y = (*(int32_t *) &rpy_rx_data.DATA[4] / 10000.0);
                trans_fdb.linear_z = (*(int32_t *) &rpy_rx_data.DATA[8] / 10000.0);
                trans_fdb.angular_x = (*(int32_t *) &rpy_rx_data.DATA[12] / 10000.0);
                trans_fdb.angular_y = (*(int32_t *) &rpy_rx_data.DATA[16] / 10000.0);
                trans_fdb.angular_z = (*(int32_t *) &rpy_rx_data.DATA[20] / 10000.0);
            }break;
            case GIMBAL:{
                if (rpy_rx_data.DATA[0]) {//相对角度控制
                    trans_fdb.yaw = - (*(int32_t *) &rpy_rx_data.DATA[1] / 1000.0);
                    trans_fdb.pitch = (*(int32_t *) &rpy_rx_data.DATA[5] / 1000.0);
                }
                else{//绝对角度控制
                    trans_fdb.yaw = - (*(int32_t *) &rpy_rx_data.DATA[1] / 1000.0);
                    trans_fdb.pitch = (*(int32_t *) &rpy_rx_data.DATA[5] / 1000.0);
                }
            }break;
            case HEARTBEAT:{
                trans_fdb.heartbeat = (*(uint8_t *) &rpy_rx_data.DATA[0]);
                heart_dt=dwt_get_time_ms();
            }break;
        }
        memset(&rpy_rx_data, 0, sizeof(rpy_rx_data));
    }
    return RT_EOK;
}
/*
void Getdata()
{
    rt_uint8_t frame_rx[sizeof(RpyTypeDef)]={0};
    rt_ringbuffer_get(&receive_buffer, frame_rx, sizeof(frame_rx));
    if(*(uint8_t*)frame_rx==0xFF)
    {
        memcpy(&rpy_rx_data,&frame_rx,sizeof(rpy_rx_data));
        switch (rpy_rx_data.ID) {
            case CHASSIS_CTRL:{
                trans_fdb.liner_x = (*(int32_t *) &rpy_rx_data.DATA[0] / 1000.0);
                trans_fdb.liner_y = (*(int32_t *) &rpy_rx_data.DATA[4] / 1000.0);
                trans_fdb.liner_z = (*(int32_t *) &rpy_rx_data.DATA[8] / 1000.0);
                //trans_fdb.angler_x = (*(int32_t *) &rpy_rx_data.DATA[12] / 1000.0);
                //trans_fdb.angler_y = (*(int32_t *) &rpy_rx_data.DATA[16] / 1000.0);
                //trans_fdb.angler_z = (*(int32_t *) &rpy_rx_data.DATA[20] / 1000.0);
            }break;

            case GIMBAL:{
                if (rpy_rx_data.DATA[0]) {//相对角度控制
                    trans_fdb.yaw = - (*(int32_t *) &rpy_rx_data.DATA[1] / 1000.0);
                    trans_fdb.pitch = (*(int32_t *) &rpy_rx_data.DATA[5] / 1000.0);
                }
                else{//绝对角度控制
                    trans_fdb.yaw = - (*(int32_t *) &rpy_rx_data.DATA[1] / 1000.0);
                    trans_fdb.pitch = (*(int32_t *) &rpy_rx_data.DATA[5] / 1000.0);
                }
            }break;
        }
        memset(&rpy_rx_data, 0, sizeof(rpy_rx_data));
    }
}*/