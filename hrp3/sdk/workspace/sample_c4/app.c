/**
 ******************************************************************************
 ** ファイル名 : app.c
 **
 ** 概要 : 二輪差動型ライントレースロボットのTOPPERS/HRP3用Cサンプルプログラム
 **
 ** 注記 : sample_c4 (sample_c3にBluetooth通信リモートスタート機能を追加)
 ******************************************************************************
 **/

#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "ev3api.h"
#include "app.h"
#include "etroboc_ext.h"
#include "common.h"
#include "matrix.h"
#include "extern.h"
#include "color.h"

#if defined(BUILD_MODULE)
    #include "module_cfg.h"
#else
    #include "kernel_cfg.h"
#endif

#define DEBUG

#if defined(DEBUG)
    #define _debug(x) (x)
#else
    #define _debug(x)
#endif

#if defined(MAKE_BT_DISABLE)
    static const int _bt_enabled = 0;
#else
    static const int _bt_enabled = 1;
#endif

/**
 * シミュレータかどうかの定数を定義します
 */
#if defined(MAKE_SIM)
    static const int _SIM = 1;
#elif defined(MAKE_EV3)
    static const int _SIM = 0;
#else
    static const int _SIM = 0;
#endif

/**
 * 左コース/右コース向けの設定を定義します
 * デフォルトは左コース(ラインの右エッジをトレース)です
 */
#if defined(MAKE_RIGHT)
    static const int _LEFT = 0;
    #define _EDGE -1
#else
    static const int _LEFT = 1;
    #define _EDGE 1
#endif

#define REFLECT_LOG_SIZE 255

extern LIST_ORDER_t list_order;
extern TARGET_REFLECT_t target_reflect_def[3];

/**
 * センサー、モーターの接続を定義します
 */
static const sensor_port_t
    touch_sensor    = EV3_PORT_1,
    color_sensor    = EV3_PORT_2,
    sonar_sensor    = EV3_PORT_3,
    gyro_sensor     = EV3_PORT_4;

static const motor_port_t
    left_motor      = EV3_PORT_C,
    right_motor     = EV3_PORT_B,
    center_motor    = EV3_PORT_D;

static int      bt_cmd = 0;     /* Bluetoothコマンド 1:リモートスタート */
//int flag_turn;

//int color_reflect;

//course種類 1-右コース 2-左コース
int course_type = LEFT;

//serach_mode =0 :通常走行モード
//int serach_mode=0;

/* sample_c2マクロ */
#define SONAR_ALERT_DISTANCE 30 /* 超音波センサによる障害物検知距離[cm] */
/* sample_c4マクロ */
//#define DEVICE_NAME     "ET0"  /* Bluetooth名 sdcard:\ev3rt\etc\rc.conf.ini LocalNameで設定 */
//#define PASS_KEY        "1234" /* パスキー    sdcard:\ev3rt\etc\rc.conf.ini PinCodeで設定 */
#define CMD_START         '1'    /* リモートスタートコマンド */

/* LCDフォントサイズ */
#define CALIB_FONT (EV3_FONT_SMALL)
#define CALIB_FONT_WIDTH (6/*TODO: magic number*/)
#define CALIB_FONT_HEIGHT (8/*TODO: magic number*/)

/* 関数プロトタイプ宣言 */
static int sonar_alert(void);
static void _syslog(int level, char* text);
static void _log(char* text);
//static void tail_control(signed int angle);
//static void backlash_cancel(signed char lpwm, signed char rpwm, int32_t *lenc, int32_t *renc);

/* メインタスク */
void main_task(intptr_t unused)
{
    signed char forward = 30; /* 前後進命令 */
    float turn;         /* 旋回命令 */
    int read_color;

    int order_pattern = 0;
    int order_red_pos = 0;

    /* LCD画面表示 */
    ev3_lcd_fill_rect(0, 0, EV3_LCD_WIDTH, EV3_LCD_HEIGHT, EV3_LCD_WHITE);

    _log("HackEV sample_c4");
    if (_LEFT)  _log("Left course:");
    else        _log("Right course:");

    /* センサー入力ポートの設定 */
    ev3_sensor_config(sonar_sensor, ULTRASONIC_SENSOR);
    ev3_sensor_config(color_sensor, COLOR_SENSOR);
    ev3_color_sensor_get_reflect(color_sensor); /* 反射率モード */
    
    ev3_sensor_config(touch_sensor, TOUCH_SENSOR);
    /* モーター出力ポートの設定 */
    ev3_motor_config(left_motor, LARGE_MOTOR);
    ev3_motor_config(right_motor, LARGE_MOTOR);
    ev3_motor_config(center_motor, LARGE_MOTOR);

    //テールモーター固定
    ev3_motor_stop(center_motor,true);
    
    //マトリクス攻略命令リスト初期化
    init_matrix_order();

    if (_bt_enabled)
    {
        /* Open Bluetooth file */
        bt = ev3_serial_open_file(EV3_SERIAL_BT);
        assert(bt != NULL);

        /* Bluetooth通信タスクの起動 */
        act_tsk(BT_TASK);
    }

    ev3_led_set_color(LED_ORANGE); /* 初期化完了通知 */

    _log("Go to the start, ready?");
    if (_SIM)   _log("Hit SPACE bar to start");
    else        _log("Tap Touch Sensor to start");

    if (_bt_enabled)
    {
        fprintf(bt, "Bluetooth Remote Start: Ready.\n");
        fprintf(bt, "1 : start\n");
        fprintf(bt, "2 : get light power\n");
        fprintf(bt, "3 : get RGB every second\n");
    }

    /* スタート待機 */
    while(1)
    {
        //tail_control(TAIL_ANGLE_STAND_UP); /* 完全停止用角度に制御 */

        if (bt_cmd == 1) {
            break; /* リモートスタート */
        } else if (bt_cmd == 2) {
            /* 光取得用のデバッグ処理 */
            rgb_raw_t dbg_raw;
            ev3_color_sensor_get_rgb_raw(color_sensor, &dbg_raw);
            LOG_D_DEBUG("R:%u, G:%u, B:%u\n", dbg_raw.r, dbg_raw.g, dbg_raw.b);
            LOG_D_DEBUG("Reflect:%d\n", ev3_color_sensor_get_reflect(color_sensor));
            bt_cmd = 0;
        } else if (bt_cmd == 3) {
            /* 1秒おきにRGB値取得/判定 */
            while (bt_cmd == 3) {
                rgb_raw_t dbg_raw;
                char color_msg[COLOR_CODE_UNKNOWN + 1][10] = {
                    "RED", "BULE", "GREEN", "YELLOW", "BLACK", "WHITE", "UNKNOWN"
                };
                int line_color = judge_color(&dbg_raw);

                LOG_D_DEBUG("R:%u, G:%u, B:%u, color:%s\n",
                            dbg_raw.r, dbg_raw.g, dbg_raw.b, color_msg[line_color]);

                tslp_tsk(1000 * 1000U);
            }
            bt_cmd = 0;
        }    

        if (ev3_touch_sensor_is_pressed(touch_sensor) == 1)
        {
            break; /* タッチセンサが押された */
        }

        if (ev3_button_is_pressed(LEFT_BUTTON) ){
            LOG_D_DEBUG("LEFT  BUTTON is pushed\n");
        }
        if (ev3_button_is_pressed(RIGHT_BUTTON) ){
            LOG_D_DEBUG("RIGHT BUTTON is pushed\n");
        }
        if (ev3_button_is_pressed(UP_BUTTON) ){
            LOG_D_DEBUG("UP    BUTTON is pushed\n");
        }
        if (ev3_button_is_pressed(DOWN_BUTTON) ){
            LOG_D_DEBUG("DOWN  BUTTON is pushed\n");
        }

        tslp_tsk(10 * 1000U); /* 10msecウェイト */
    }

    //モーターストップ
    ev3_motor_stop(left_motor, false);
    ev3_motor_stop(right_motor, false);

    /* 走行モーターエンコーダーリセット */
    ev3_motor_reset_counts(left_motor);
    ev3_motor_reset_counts(right_motor);

    ev3_led_set_color(LED_GREEN); /* スタート通知 */
    tslp_tsk(1000 * 1000U); /* 10msecウェイト */

    //コースパターンを設定する
    _log("select pattern.");
    LOG_D_DEBUG("select pattern.\n");

    while (1) {
        if (ev3_button_is_pressed(UP_BUTTON)) {
            //パターン1
            order_pattern = 0;
            break;
        } else if (ev3_button_is_pressed(DOWN_BUTTON)) {
            //パターン2
            order_pattern = 1;
            break;
        } else if (ev3_button_is_pressed(LEFT_BUTTON)) {
            //パターン3
            order_pattern = 2;
            break;
        } else if (ev3_button_is_pressed(RIGHT_BUTTON)) {
            //パターン4
            order_pattern = 3;
            break;
        } else if (ev3_button_is_pressed(ENTER_BUTTON)) {
            //パターン5
            order_pattern = 4;
            break;
        }
    }

    _log("select pattern OK.");
    LOG_D_DEBUG("selected pattern %d\n", order_pattern + 1);
    tslp_tsk(1000 * 1000U); /* 1000msecウェイト */

    //赤ブロックの位置を設定する
    //ダミーデータを設定されても確認しないので注意
    _log("select red block pos.");
    LOG_D_DEBUG("select red block pos.\n");

    while(1) {
        if (ev3_button_is_pressed(UP_BUTTON)) {
            //パターン1
            order_red_pos = MATRIX_COLOR_RED;
            break;
        } else if (ev3_button_is_pressed(DOWN_BUTTON)) {
            //パターン2
            order_red_pos = MATRIX_COLOR_BLUE;
            break;
        } else if (ev3_button_is_pressed(LEFT_BUTTON)) {
            //パターン3
            order_red_pos = MATRIX_COLOR_YELLOW;
            break;
        } else if (ev3_button_is_pressed(RIGHT_BUTTON)) {
            //パターン4
            order_red_pos = MATRIX_COLOR_GREEN;
            break;
        }
    }

    char debug_color_msg[MATRIX_COLOR_MAX][10] = {
        "red", "blue", "yellow", "green"
    };
    _log("select red block pos OK.");
    LOG_D_DEBUG("selected red block pos %s\n", debug_color_msg[order_red_pos]);

    /* ライントレース */

    /* コンフィグ 初期設定 */
    rgb_raw_t main_rgb;
    rgb_raw_t dbg_rgb;
    float sensor_reflect = 0;
   
    int count = 0;
    int first_counter = 0;
    int blue_count = 0;
    int is_blue = 0;
    int wait_flame = 45;
    int interval = -1;
    int chg_flag = 0;
    float blue_rate = 1.5;
    float pid[3] = {1.5,1.0,0.015};
 
    //最初のレイントレースでは、
    //右コースはラインの左、左コースはラインの右を白寄りでトレース
    TARGET_REFLECT_t target_reflect;
    target_reflect = change_target_reflect(COLOR_CODE_WHITE);
    int trace_pos = (course_type == RIGHT) ? LEFT : RIGHT;

    while(1)
    {
        if (isfound_red(&dbg_rgb) == COLOR_CODE_RED){
            //赤色を検出したのでライントレースを終了する
            LOG_D_DEBUG("red discoverd.\n");
            LOG_D_TEST("R:%u, G:%u, B:%u\n", dbg_rgb.r, dbg_rgb.g, dbg_rgb.b);

            break;
        }

        ev3_color_sensor_get_rgb_raw(color_sensor, &main_rgb);
        //sensor_reflect = ev3_color_sensor_get_reflect(color_sensor);

        //青かどうかの判定
        if((blue_rate * main_rgb.r < main_rgb.b ) && (blue_rate * main_rgb.g < main_rgb.b) && (is_blue == 0)
        && (main_rgb.r > THRE_R_OF_WHITE * 0.2 && main_rgb.g > THRE_G_OF_WHITE * 0.2 && main_rgb.b > THRE_B_OF_WHITE * 0.2)) {
            is_blue = 1;
            chg_flag = 1;
            LOG_D_DEBUG("isBlue, count: %d, blue_count; %d\n", count, blue_count);
        } else if(!((1.8 * main_rgb.r < main_rgb.b ) && (1.8 * main_rgb.g < main_rgb.b))
         && (main_rgb.r < THRE_R_OF_WHITE * 0.8 && main_rgb.g < THRE_G_OF_WHITE * 0.8 && main_rgb.b < THRE_B_OF_WHITE * 0.8)
         && (is_blue == 1)) {
            //5sec 青を検出しない
            is_blue = 0;
            blue_count++;
            LOG_D_DEBUG("isNotBlue, count: %d, blue_count; %d\n", count, blue_count);
        }
        //LOG_D_DEBUG("interval: %d\n", interval);

        if(first_counter < 60 ){
            first_counter++;
            forward = 30;
        } else if(first_counter == 60) {
            first_counter = 100;
            forward = 45;
        }

        if (blue_count == 1 && chg_flag == 1) {
            forward = 35;
            //反射基準値を切り替える -> 青を検出しやすくする
            pid[0] = 2.0;
            pid[1] = 1.0;
            pid[2] = 0.015;
            blue_rate = 1.8;
            target_reflect = change_target_reflect(COLOR_CODE_BLUE);
            chg_flag = 0;
        }

        if(blue_count == 3 && chg_flag == 1){
            sensor_reflect = ev3_color_sensor_get_reflect(color_sensor);
            //LOG_D_DEBUG("sensor_reflect: %d\ntarget_reflect.color: %d\ntarget_reflect.reflect: %d\n",
            //            sensor_reflect, target_reflect.color, target_reflect.reflect);
            //取得値が基準値よりも小さい = 黒(or青)にいるケースで切り替える想定
            if (true) {
                //トレース方向を切り替える
                trace_pos = changet_trace_pos(trace_pos);
                blue_count++;
                chg_flag = 0;
            }
            //LOG_D_DEBUG("右回り中");
        }

        if(blue_count == 5 && count < wait_flame){
            if(chg_flag == 1){
                trace_pos = changet_trace_pos(trace_pos);
                chg_flag = 0;
                LOG_D_DEBUG("aaa");
            }
            count++;
        }

        if(blue_count >= 5 && count == wait_flame){
            //反射基準値を切り替える
            LOG_D_DEBUG("bbb");
            trace_pos = changet_trace_pos(trace_pos);
            count++;
        }

        turn = culculate_turn(target_reflect.reflect, trace_pos,pid);

        ev3_motor_steer(
            left_motor,
            right_motor,
            forward,
            turn
        );

        tslp_tsk(2 * 1000U); /* 2msec周期起動 */
    }

    //ライントレース完了
    LOG_D_DEBUG("linetrace completed.\n");

    ev3_motor_stop(left_motor, false);
    ev3_motor_stop(right_motor, false);
    tslp_tsk(100 * 1000U); /* 100msec 停止*/

    //トレース位置、power を変更する
    //右コースなら黒線の左、左コースなら黒線の右をトレースしながら進む
    //色検出の精度を高めるため、黒よりを走行する
    trace_pos = (course_type == RIGHT) ? LEFT : RIGHT;
    target_reflect = change_target_reflect(COLOR_CODE_WHITE);
    forward = 15;

    //マトリクスに向かって移動する
    if (course_type == RIGHT) {
        turn_specified_degree(80, LEFT);
    } else {
        turn_specified_degree(80, RIGHT);
    }
    tslp_tsk(500 * 1000U); /* 500msec 停止*/

    //誤作動を防ぐため、一定距離進
    ev3_motor_rotate(right_motor, 150, 20, false);
    ev3_motor_rotate(left_motor , 150, 20, true);
    tslp_tsk(500 * 1000U); /* 500msec 停止*/

    ev3_motor_set_power(left_motor, 15);
    ev3_motor_set_power(right_motor, 15);

    while (1) {
        read_color = judge_color(&dbg_rgb);
        if (read_color == COLOR_CODE_BLACK) {
            LOG_D_DEBUG("arrived matrix. adjust position.\n");
            LOG_D_TEST("R:%u, G:%u, B:%u\n", dbg_rgb.r, dbg_rgb.g, dbg_rgb.b);
            //到着した
            break;
        } else if ((read_color == COLOR_CODE_BLUE) || (read_color == COLOR_CODE_RED)) {
            //ブロックサークルに到着したケース
            //黒線上に位置を修正する
            LOG_D_DEBUG("arrived block circle. modify over black.\n");
            LOG_D_TEST("R:%u, G:%u, B:%u\n", dbg_rgb.r, dbg_rgb.g, dbg_rgb.b);

            ev3_motor_rotate(right_motor, -200, 20, false);
            ev3_motor_rotate(left_motor, -200, 20, true);
            tslp_tsk(100 * 1000U);

            if (course_type == RIGHT) {
                turn_specified_degree(45, LEFT);
            } else {
                turn_specified_degree(45, RIGHT);
            }
            tslp_tsk(100 * 1000U);

            //もう一度黒線まで直進する
            ev3_motor_set_power(left_motor, 15);
            ev3_motor_set_power(right_motor, 15);
        }

        tslp_tsk(4 * 1000U); /* 4msec 停止*/
    }

    tslp_tsk(100 * 1000U); /* 100msec 停止*/

    //角度をただす
    //車軸-センサー間分(5cm)前進
    ev3_motor_rotate(right_motor, 50, 20, false);
    ev3_motor_rotate(left_motor , 50, 20, true);

    while(1){
        if (course_type == RIGHT) {
            ev3_motor_rotate(left_motor , 10, 20, false);
            ev3_motor_rotate(right_motor , -10, 20, true);
        } else {
            ev3_motor_rotate(left_motor , -10, 20, false);
            ev3_motor_rotate(right_motor , 10, 20, true);
        }

        read_color = judge_color(&dbg_rgb);
        if (read_color == COLOR_CODE_BLACK) {
            LOG_D_DEBUG("adjust position done. move to red block circle.\n");
            LOG_D_TEST("R:%u, G:%u, B:%u\n", dbg_rgb.r, dbg_rgb.g, dbg_rgb.b);
            //到着した
            break;
        } else if (read_color == COLOR_CODE_BLUE) {
            //青のブロックサークルに到着したので、
            //ライントレースしながら黒まで進む
            LOG_D_DEBUG("arrived blue block circle. linetrace start.\n");
            LOG_D_TEST("R:%u, G:%u, B:%u\n", dbg_rgb.r, dbg_rgb.g, dbg_rgb.b);

            while (1) {
                read_color = judge_color(&dbg_rgb);
                if (read_color == COLOR_CODE_BLACK) {
                    LOG_D_DEBUG("adjust positon. 90 degree rotate.\n");
                    LOG_D_TEST("R:%u, G:%u, B:%u\n", dbg_rgb.r, dbg_rgb.g, dbg_rgb.b);
                    if (course_type == RIGHT) {
                        turn_specified_degree(90, LEFT);
                    } else {
                        turn_specified_degree(90, RIGHT);
                    }

                    break;
                }

                turn = culculate_turn(target_reflect.reflect, trace_pos, pid);
                ev3_motor_steer(left_motor, right_motor, 10, turn);
                
                tslp_tsk(2 * 1000U); /* 2msec周期起動 */
            }
        }

        tslp_tsk(4 * 1000U); /* 4msec 停止*/
    }

    //赤のブロックサークルまで移動する
    while (1) {
        read_color = judge_color(&dbg_rgb);
        if (read_color == COLOR_CODE_BLUE) {
            sensor_reflect = ev3_color_sensor_get_reflect(color_sensor);
            //青は反射値が低いため、反射値も考慮する
            if (sensor_reflect < LIGHT_WHITE / 2) {
                //ブロックサークルを直進して進む
                LOG_D_DEBUG("blue block circle found.\n");
                LOG_D_TEST("reflect: %d", sensor_reflect);
                LOG_D_TEST("R:%u, G:%u, B:%u\n", dbg_rgb.r, dbg_rgb.g, dbg_rgb.b);
                //白よりに進むよう、power を調整
                ev3_motor_rotate(right_motor, 200, 20 + 1 * (trace_pos == LEFT), false);
                ev3_motor_rotate(left_motor , 200, 20 + 1 * (trace_pos == RIGHT), true);
            }
        } else if (read_color == COLOR_CODE_RED) {
            //移動完了、マトリクス攻略を行う
            LOG_D_DEBUG("arrived red block circle.\n");
            LOG_D_TEST("R:%u, G:%u, B:%u\n", dbg_rgb.r, dbg_rgb.g, dbg_rgb.b);
            break;
        } else {
            //念のため UNKOWN で更新する
            read_color = COLOR_CODE_UNKNOWN;
        }

        turn = culculate_turn(target_reflect.reflect, trace_pos, pid);

        ev3_motor_steer( left_motor, right_motor, forward, turn);
        
        tslp_tsk(2 * 1000U); /* 2msec周期起動 */
    }

    LOG_D_DEBUG("Start 'block de treasure hunter.'\n");

    //マトリクス攻略
    while(1)
    {
        if(course_type == RIGHT) {

            turn_90_degree_on_start_pos(2);

            matrix_move_sequence(list_order.right_matrix_order[order_pattern][order_red_pos].move_on_matrix_order);
            approach_to_goal_sequence(list_order.right_matrix_order[order_pattern][order_red_pos].move_to_goal_order);


        } else {

            turn_90_degree_on_start_pos(1);
            matrix_move_sequence(list_order.left_matrix_order[order_pattern][order_red_pos].move_on_matrix_order);
            approach_to_goal_sequence(list_order.left_matrix_order[order_pattern][order_red_pos].move_to_goal_order);
            
        }
        tslp_tsk(2 * 1000000U); /* 0.4msec周期起動 */
    }

    if (_bt_enabled)
    {
        ter_tsk(BT_TASK);
        fclose(bt);
    }

    ev3_motor_set_power(left_motor, 0);
    ev3_motor_set_power(right_motor, 0);

    ext_tsk();
}

//*****************************************************************************
// 関数名 : culculate_turn
// 引数   : target_reflect - 反射基準値
//          trace_pos      - トレースするエッジ
// 返り値 : 角度
// 概要   : 引数から PID制御を行い、角度を返す　
//*****************************************************************************
float culculate_turn(unsigned int target_reflect, int trace_pos,float pid[3]) {
    float turn = 0;
    float curb = 0;

    float sensor_reflect = 0;
    static float sensor_dt = 0;
    static float sensor_dt_pre = 0;

    float ie = 0;
    float de = 0;

    //制御定数
    const float T  = 0.002;
    const float Kp = pid[0];
    const float Ki = pid[1];
    const float Kd = pid[2];

    sensor_reflect = ev3_color_sensor_get_reflect(color_sensor);
    if(sensor_reflect > LIGHT_WHITE){
        sensor_reflect = LIGHT_WHITE;
    }

    //P制御
    sensor_dt_pre = sensor_dt;
    sensor_dt = (target_reflect - sensor_reflect);

    //I制御
    ie = ie + (sensor_dt + sensor_dt_pre)*T/2;

    //D制御
    de = (sensor_dt - sensor_dt_pre)/T;

    //曲がり角度の決定
    curb = Kp * sensor_dt + Ki * ie + Kd * de;

    if (curb > 100){
        curb = 80;
    }

    if(curb < -100){
        curb = -80;
    }

    if(trace_pos == LEFT) {
        turn = -curb;
    } else {
        turn = curb;
    }
    return turn;
}
//*****************************************************************************
// 関数名 : approach_to_goal_sequence
// 引数 :   マトリクス行動ルートを格納した配列, 配列サイズ
// 返り値 : 
// 概要 :　
//*****************************************************************************
void approach_to_goal_sequence(uint8_t order[ORDER_NUM_MAX]){

    int motor_degree=20;
    int motor_power=20;
    
    
    for(int i=0; i<ORDER_NUM_MAX; i++){

        switch (order[i]){
        case move_forward:
            LOG_D_DEBUG("approach_to_goal_sequence %d- move_forward\n",i);
            trace_node();
            break;
        case turn_right:
            LOG_D_DEBUG("approach_to_goal_sequence %d- turn_right\n",i);
            turn_90_degree(1);
            break;
        case turn_left:
            LOG_D_DEBUG("approach_to_goal_sequence %d- turn_left \n",i);
            turn_90_degree(2);
            break;
        case move_on_white_zone:
            
            while(1){
                if (ev3_color_sensor_get_reflect(color_sensor) >= (LIGHT_WHITE + LIGHT_BLACK)/2){
                    //白部分の上の時
                    ev3_motor_rotate(left_motor , motor_degree, motor_power, false);
                    ev3_motor_rotate(right_motor, motor_degree, motor_power, true);
                    LOG_D_DEBUG("trace_node [white]\n");

                }else{
                    //黒部分の上の時
                    break;
                }
                LOG_D_DEBUG("approach_to_goal_sequence %d- move_on_white_zone \n",i);

            }
            break;
        case move_to_goal:
            while(1){
                ev3_motor_rotate(left_motor , motor_degree, motor_power, false);
                ev3_motor_rotate(right_motor, motor_degree, motor_power, true);
                LOG_D_DEBUG("approach_to_goal_sequence %d- move_to_goal \n",i);
            }
            break;

        default:
            break;
        }
    }

}
//*****************************************************************************
// 関数名 : matrix_move_sequence
// 引数 :   マトリクス行動ルートを格納した配列
// 返り値 : 
// 概要 :　
//*****************************************************************************
void matrix_move_sequence(uint8_t order[ORDER_NUM_MAX]) {
    int i = 0;

    for(i = 0; i < ORDER_NUM_MAX; i++){
        switch (order[i]){
        case move_forward:
            LOG_D_DEBUG("matrix_move_sequence %d- move_forward\n",i);
            trace_node();
            break;
        case turn_right:
            LOG_D_DEBUG("matrix_move_sequence %d- turn_right\n",i);
            turn_90_degree(1);
            break;
        case turn_left:
            LOG_D_DEBUG("matrix_move_sequence %d- turn_left \n",i);
            turn_90_degree(2);
            break;

        case push_blue_block:
            LOG_D_DEBUG("matrix_move_sequence %d- push_blue_block\n",i);
            do_push_blue_block();
            break;

        case get_red_block:
            LOG_D_DEBUG("matrix_move_sequence %d- get_red_block\n",i);
            break;

        default:
            LOG_D_DEBUG("order: %d\n", order[i]);
            break;
        }
    }
}

//*****************************************************************************
// 関数名 : do_push_blue_block
// 引数 :   
// 返り値 : 
// 概要 :　
//*****************************************************************************
void do_push_blue_block(void) {

    int rotate_degree=180;

    ev3_motor_rotate(center_motor, 50 , 50, true);

    ev3_motor_rotate(right_motor, rotate_degree , 50, false);
    ev3_motor_rotate(left_motor , rotate_degree,  50, true);

    tslp_tsk(2 * 1000000U); /* 0.2sec停止 */

    ev3_motor_rotate(right_motor, -rotate_degree , 50, false);
    ev3_motor_rotate(left_motor , -rotate_degree,  50, true);

    ev3_motor_rotate(center_motor, -50 , 50, true);

}

//*****************************************************************************
// 関数名 : change_target_reflect
// 引数   : color_code - トレースする色
// 返り値 : 反射基準値を含む構造体
// 概要   : 引数で指定した色から、事前に定義した構造体を返す
//*****************************************************************************
TARGET_REFLECT_t change_target_reflect(int color_code) {
    int i = 0;

    for (i = 0; sizeof(target_reflect_def) / sizeof(target_reflect_def[0]); i++) {
        if (target_reflect_def[i].color == color_code) {
            LOG_D_DEBUG("target reflect changd. -> %d\n", target_reflect_def[i].reflect);
            return target_reflect_def[i];
        }
    }

    //デフォルトでは黒に近いラインをトレースするようにする
    return target_reflect_def[0];
}

//*****************************************************************************
// 関数名 : changet_trace_pos
// 引数   : now_trace_pos - 現在のトレース方向
// 返り値 : 新しいトレース方向
// 概要   : 現在の逆となる方向を返す
//*****************************************************************************
int changet_trace_pos(int now_trace_pos) {
    LOG_D_DEBUG("trace_pos changed. %s -> %s\n",
                now_trace_pos == RIGHT ? "RIGHT" : "LEFT",
                now_trace_pos == RIGHT ? "LEFT" : "RIGHT");
    return now_trace_pos == RIGHT ? LEFT : RIGHT;
}

//*****************************************************************************
// 関数名 : isfound_red
// 引数   : *dbg_rbg - 取得した値を格納(デバッグ用)
// 返り値 : カラーコード
// 概要   : 
//*****************************************************************************
int isfound_red(rgb_raw_t *dbg_rgb) {
    rgb_raw_t read_rgb;
    ev3_color_sensor_get_rgb_raw(color_sensor, &read_rgb);

    if (NULL != dbg_rgb) {
        memcpy(dbg_rgb, &read_rgb, sizeof(rgb_raw_t));
    }

    if ((read_rgb.r >= THRE_R_OF_RED) && (read_rgb.r > 1.5 * read_rgb.b) && (read_rgb.r > 1.5 * read_rgb.g)) {
        return COLOR_CODE_RED;
    }

    return COLOR_CODE_UNKNOWN;
}

int judge_color(rgb_raw_t *dbg_rgb) {
    int ret = COLOR_CODE_UNKNOWN;
    rgb_raw_t read_rgb;
    ev3_color_sensor_get_rgb_raw(color_sensor, &read_rgb);

    if (read_rgb.r > 100 && read_rgb.g > 100 && read_rgb.b > 100) {
        ret = COLOR_CODE_WHITE;
    } else if (read_rgb.r < 25 && read_rgb.g < 30 && read_rgb.b < 25) {
        ret = COLOR_CODE_BLACK;
    } else if (read_rgb.r > 100 && read_rgb.r > 1.5 * read_rgb.g && read_rgb.r > 1.5 * read_rgb.b) {
        ret = COLOR_CODE_RED;
    } else if (read_rgb.g > 60 && read_rgb.g > 2 * read_rgb.r && read_rgb.g > 1.7 * read_rgb.b) {
        ret = COLOR_CODE_GREEN;
    } else if (read_rgb.b > 100 && read_rgb.b > 3 * read_rgb.r && read_rgb.b > 2 * read_rgb.g) {
        ret = COLOR_CODE_BLUE;
    } else if (read_rgb.r > 100 && read_rgb.g > 100 &&
               read_rgb.r > 2 * read_rgb.b && read_rgb.g > 2 * read_rgb.b) {
        ret = COLOR_CODE_YELLOW;
    }

    if (NULL != dbg_rgb) {
        memcpy(dbg_rgb, &read_rgb, sizeof(rgb_raw_t));
    }

    return ret;
}

//*****************************************************************************
// 関数名 : ret_color_code
// 引数 :   rgb_raw_t *ret_rgb - 取得した値を格納(デバッグ用)
// 返り値 : COLOR_CODEの整数
// 概要 :　
//*****************************************************************************
int ret_color_code(rgb_raw_t *ret_rgb) {
    
    int ret;
    rgb_raw_t read_rgb;
    ev3_color_sensor_get_rgb_raw(color_sensor, &read_rgb);

    if(read_rgb.r>THRE_R_OF_WHITE && read_rgb.g>THRE_G_OF_WHITE && read_rgb.b>THRE_B_OF_WHITE){
        //白の時
        ret= COLOR_CODE_WHITE;
        //LOG_D_DEBUG("WHITE  r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else if(read_rgb.r<THRE_R_OF_BLUE && read_rgb.g<THRE_G_OF_BLUE && read_rgb.b>THRE_B_OF_BLUE){
        //青の時
        ret= COLOR_CODE_BLUE;
        //LOG_D_DEBUG("BLUE   r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else if(read_rgb.r>THRE_R_OF_YELLOW && read_rgb.g>THRE_G_OF_YELLOW && read_rgb.b<THRE_B_OF_YELLOW){
        //黄の時
        ret= COLOR_CODE_YELLOW;
        ///LOG_D_DEBUG("YELLOW r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else if(read_rgb.r>THRE_R_OF_RED && read_rgb.g<THRE_G_OF_RED && read_rgb.b<THRE_B_OF_RED){
        //赤の時
        ret= COLOR_CODE_RED;
        //LOG_D_DEBUG("RED    r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else if(read_rgb.r<THRE_R_OF_GREEN && read_rgb.g>THRE_G_OF_GREEN && read_rgb.b<THRE_B_OF_GREEN){
        //緑の時
        ret= COLOR_CODE_GREEN;
        //LOG_D_DEBUG("GREEN  r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else if(read_rgb.r < THRE_R_OF_BLACK && read_rgb.g < THRE_G_OF_BLACK && read_rgb.b < THRE_B_OF_BLACK){
        ret = COLOR_CODE_BLACK;
    } else {
        //不明の時
        ret= COLOR_CODE_UNKNOWN;
        //LOG_D_DEBUG("UNKNOWN r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);
    }

    if (NULL != ret_rgb) {
        memcpy(ret_rgb, &read_rgb, sizeof(rgb_raw_t));
    }

    return ret;


}

//*****************************************************************************
// 関数名 : motor_set_two_motor
// 引数 :   LeftMotorPower   :左モーターパワー
//          RightMotorPower  :右モーターパワー
// 返り値 : なし
// 概要 :　
//*****************************************************************************
void motor_set_two_motor(int LeftMotorPower, int RightMotorPower) {

    ev3_motor_set_power(EV3_PORT_C, LeftMotorPower);
    ev3_motor_set_power(EV3_PORT_B, RightMotorPower);

    
}

//*****************************************************************************
// 関数名 : turn_90_degree
// 引数 :   1-右旋回　2-左旋回
// 返り値 : 
// 概要 :　
//*****************************************************************************
void turn_90_degree(int flag_turn) {

    //車体　90度分旋回時　回転角度
    const int rotate_degree=125;

    if(flag_turn==1){
        //右旋回
        ev3_motor_rotate(right_motor, -rotate_degree , 20, false);
        ev3_motor_rotate(left_motor , rotate_degree, 20, true);

    }else if(flag_turn==2){
        //左旋回
        ev3_motor_rotate(left_motor , -rotate_degree , 20, false);
        ev3_motor_rotate(right_motor , rotate_degree, 20, true);

    }
    
}

//*****************************************************************************
// 関数名 : turn_90_degree_on_start_pos
// 引数 :   1-右旋回　2-左旋回
// 返り値 : 
// 概要 :　スタート位置の赤パターンを発見時に実行し、赤パターンを超えて、黒線を発見するまで回転する
//         マトリクス外周など黒線がない方向への回転には使えない
//*****************************************************************************
void turn_90_degree_on_start_pos(int flag_turn){

    //赤発見時　赤パターンを超える回転角
    const int degree_to_move_wheel_offset=75;
    int motor_power=20;


    //ブロックサークル間距離　移動
    ev3_motor_rotate(right_motor, degree_to_move_wheel_offset , 20, false);
    ev3_motor_rotate(left_motor , degree_to_move_wheel_offset , 20, true);


    //車体　90度分旋回時　回転角度
    const int rotate_degree=115;

    if(flag_turn==1){
        //右旋回
        ev3_motor_rotate(right_motor, -rotate_degree , 20, false);
        ev3_motor_rotate(left_motor , rotate_degree, 20, true);

    }else if(flag_turn==2){
        //左旋回
        ev3_motor_rotate(left_motor , -rotate_degree , 20, false);
        ev3_motor_rotate(right_motor , rotate_degree, 20, true);
    }

    //ブロックサークル間距離　移動
    ev3_motor_rotate(right_motor, degree_to_move_wheel_offset , 20, false);
    ev3_motor_rotate(left_motor , degree_to_move_wheel_offset , 20, true);
    
    int read_color;
    while (1) {
        if (flag_turn == 1) {
            ev3_motor_rotate(left_motor , 10, 20, false);
            ev3_motor_rotate(right_motor , -10, 20, true);
        } else {
            ev3_motor_rotate(left_motor , -10, 20, false);
            ev3_motor_rotate(right_motor , 10, 20, true);
        }

        read_color = judge_color(NULL);
        if (read_color == COLOR_CODE_BLACK) {
            //到着した
            break;
        }
    }
}

//未検証
void turn_specified_degree(int degree, int flag_turn) {
    //回転度数を各モータの回転角度に修正するための定数
    const double rate = 1.3;
    const int forward = 20;
    int rotate_degree = floor(degree * rate);

    if (RIGHT == flag_turn) {
        ev3_motor_rotate(right_motor, -rotate_degree, forward, false);
        ev3_motor_rotate(left_motor , rotate_degree, forward, true);
    } else if (LEFT == flag_turn) {
        ev3_motor_rotate(right_motor, rotate_degree, forward, false);
        ev3_motor_rotate(left_motor , -rotate_degree, forward, true);
    } else {
        LOG_D_ERROR("Bad Parameter. flag_turn: %d\n", flag_turn);
    }
}

//*****************************************************************************
// 関数名 : trace_node
// 引数 : 
// 返り値 : 
// 概要 :　
//*****************************************************************************
void trace_node(void) {

    int read_color;

    //15cm前進するときに必要な回転角
    const int degree_to_move_node=172;
    
    //センサ-車軸間文全身に必要な回転角
    const int degree_to_move_wheel_offset=57;
    int tog=0;
    int motor_degree=20;
    int motor_power=20;


    //ブロックサークル間距離　移動
    ev3_motor_rotate(right_motor, degree_to_move_node , 20, false);
    ev3_motor_rotate(left_motor , degree_to_move_node , 20, true);

    while(1){

        if (ev3_color_sensor_get_reflect(color_sensor) >= (LIGHT_WHITE + LIGHT_BLACK)/2){
            //白部分の上の時
            if(tog==0){
                ev3_motor_rotate(left_motor , motor_degree+10, motor_power, false);
                ev3_motor_rotate(right_motor, motor_degree, motor_power, true);
                tog=1;


            }else if(tog==1){
                ev3_motor_rotate(left_motor, motor_degree, motor_power, false);
                ev3_motor_rotate(right_motor, motor_degree+10, motor_power, true);
                tog=0;
            }

            LOG_D_DEBUG("trace_node [white]\n");

        }else{
            ev3_motor_rotate(left_motor, motor_degree, motor_power, true);
            ev3_motor_rotate(right_motor, motor_degree, motor_power, true);

            LOG_D_DEBUG("trace_node [black]\n");
        }
        read_color=judge_color(NULL);

        //色(赤、青、黄、緑)検出時
        if(read_color==COLOR_CODE_BLUE    ||read_color==COLOR_CODE_YELLOW 
            || read_color==COLOR_CODE_RED || read_color==COLOR_CODE_GREEN){

            //車軸-センサー間分(5cm)前進
            ev3_motor_rotate(right_motor, degree_to_move_wheel_offset , 30, false);
            ev3_motor_rotate(left_motor , degree_to_move_wheel_offset , 30, true);
            

            LOG_D_DEBUG("** COLOR FIND BREAK \n");
            break;
        }
    }


}

void init_matrix_order(void) {
    memset(&list_order, 0x00, sizeof(LIST_ORDER_t));
    memcpy(&list_order.left_matrix_order, &left_list_order, sizeof(left_list_order));
    memcpy(&list_order.right_matrix_order, &right_list_order, sizeof(right_list_order));
}

//*****************************************************************************
// 関数名 : motor_stop
// 引数 :   
// 返り値 : なし
// 概要 :　車体を停止する
//*****************************************************************************
void motor_stop() {
    ev3_motor_stop(EV3_PORT_B, true);
    ev3_motor_stop(EV3_PORT_C, true);
}

//*****************************************************************************
// 関数名 : sonar_alert
// 引数 : 無し
// 返り値 : 1(障害物あり)/0(障害物無し)
// 概要 : 超音波センサによる障害物検知
//*****************************************************************************
static int sonar_alert(void)
{
    static unsigned int counter = 0;
    static int alert = 0;

    signed int distance;

    if (++counter == 40/4) /* 約40msec周期毎に障害物検知  */
    {
        /*
         * 超音波センサによる距離測定周期は、超音波の減衰特性に依存します。
         * NXTの場合は、40msec周期程度が経験上の最短測定周期です。
         * EV3の場合は、要確認
         */
        distance = ev3_ultrasonic_sensor_get_distance(sonar_sensor);
        if ((distance <= SONAR_ALERT_DISTANCE) && (distance >= 0))
        {
            alert = 1; /* 障害物を検知 */
        }
        else
        {
            alert = 0; /* 障害物無し */
        }
        counter = 0;
    }

    return alert;
}

//*****************************************************************************
// 関数名 : bt_task
// 引数 : unused
// 返り値 : なし
// 概要 : Bluetooth通信によるリモートスタート。 Tera Termなどのターミナルソフトから、
//       ASCIIコードで1を送信すると、リモートスタートする。
//*****************************************************************************
void bt_task(intptr_t unused)
{
    while(1)
    {
        if (_bt_enabled)
        {
            uint8_t c = fgetc(bt); /* 受信 */
            switch(c)
            {
            case '1':
                bt_cmd = 1;
                break;
            case '2':
                bt_cmd = 2;
                break;
            case '3':
                bt_cmd = 3;
                break;
            default:
                break;
            }
            fputc(c, bt); /* エコーバック */
        }
    }
}

//*****************************************************************************
// 関数名 : _syslog
// 引数 :   int   level - SYSLOGレベル
//          char* text  - 出力文字列
// 返り値 : なし
// 概要 : SYSLOGレベルlebelのログメッセージtextを出力します。
//        SYSLOGレベルはRFC3164のレベル名をそのまま（ERRだけはERROR）
//        `LOG_WARNING`の様に定数で指定できます。
//*****************************************************************************
static void _syslog(int level, char* text) {
    static int _log_line = 0;
    if (_SIM)
    {
        syslog(level, text);
    }
    else
    {
        ev3_lcd_draw_string(text, 0, CALIB_FONT_HEIGHT*_log_line++);
    }
}

//*****************************************************************************
// 関数名 : _log
// 引数 :   char* text  - 出力文字列
// 返り値 : なし
// 概要 : SYSLOGレベルNOTICEのログメッセージtextを出力します。
//*****************************************************************************
static void _log(char *text) {
    _syslog(LOG_NOTICE, text);
}