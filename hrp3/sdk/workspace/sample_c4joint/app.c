/**
 ******************************************************************************
 ** ファイル名 : app.c
 **
 ** 概要 : 二輪差動型ライントレースロボットのTOPPERS/HRP3用Cサンプルプログラム
 **
 ** 注記 : sample_c4 (sample_c3にBluetooth通信リモートスタート機能を追加)
 ******************************************************************************
 **/

#include "ev3api.h"
#include "app.h"
#include "etroboc_ext.h"
#include "common.h"
#include <stdbool.h>

/*マトリクス上動作命令
move_forward     :マトリクスのノードをトレースする
turn_right       :90°右旋回する
turn_left        :90°左旋回する
push_blue_block  :青ブロックを押し出す
get_red_block    :赤ブロックを保持する

*/
enum matrix_move_order{
    move_forward=1,
    turn_right=2,
    turn_left=3,
    push_blue_block=4,
    get_red_block=5
};
/*
マトリクス移動命令
パターン4赤ブロック緑上の時
*/
int matrix_order[]={move_forward,
                    move_forward,
                    push_blue_block,
                    turn_right,
                    move_forward,
                    turn_right,
                    move_forward,
                    turn_left,
                    move_forward,
                    push_blue_block,
                    turn_left,
                    move_forward,
                    turn_right,
                    move_forward,
                    turn_left,
                    move_forward,
                    get_red_block
                    };
int matrix_order_size= sizeof(matrix_order)/sizeof(matrix_order[0]);

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
    right_motor     = EV3_PORT_B;
    centar_motor     = EV3_PORT_D;
    
int ret_color_code(void);

static int      bt_cmd = 0;     /* Bluetoothコマンド 1:リモートスタート */
//static FILE     *bt = NULL;     /* Bluetoothファイルハンドル */
int flag_turn;

int color_reflect;

//course種類 1-左コース 2-右コース
int course_type=1;


#define REFLECT_LOG_SIZE 255



//serach_mode =0 :通常走行モード
//
int serach_mode=0;

//反射データを記録するための配列
uint8_t reflect_log[REFLECT_LOG_SIZE];
//記録位置のポインタ
int reflect_ptr=0;

/* 下記のマクロは個体/環境に合わせて変更する必要があります */
/* sample_c1マクロ */
#define LIGHT_WHITE  23         /* 白色の光センサ値 */
#define LIGHT_BLACK  0          /* 黒色の光センサ値 */
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

/* 色彩センサ　閾値 */
/*青　*/
#define THRE_B_OF_BLUE 40
#define THRE_G_OF_BLUE 60
#define THRE_R_OF_BLUE 15

/**/
#define THRE_B_OF_RED 40
#define THRE_G_OF_RED 50
#define THRE_R_OF_RED 60

/**/
#define THRE_B_OF_GREEN 40
#define THRE_G_OF_GREEN 40
#define THRE_R_OF_GREEN 20

/**/
#define THRE_B_OF_YELLOW 30
#define THRE_G_OF_YELLOW 45
#define THRE_R_OF_YELLOW 45

/**/
#define THRE_B_OF_BLACK 10
#define THRE_G_OF_BLACK 10
#define THRE_R_OF_BLACK 10

/**/
#define THRE_B_OF_WHITE 80
#define THRE_G_OF_WHITE 80
#define THRE_R_OF_WHITE 80

#define COLOR_CODE_RED    1
#define COLOR_CODE_BLUE   2
#define COLOR_CODE_GREEN  3
#define COLOR_CODE_YELLOW 4
#define COLOR_CODE_BLACK  5
#define COLOR_CODE_WHITE  6
#define COLOR_CODE_UNKNOWN  -1






/* 関数プロトタイプ宣言 */
static int sonar_alert(void);
static void _syslog(int level, char* text);
static void _log(char* text);
//static void tail_control(signed int angle);
//static void backlash_cancel(signed char lpwm, signed char rpwm, int32_t *lenc, int32_t *renc);



/* メインタスク */
void main_task(intptr_t unused)
{
    
    
    signed char forward;      /* 前後進命令 */
    signed char turn;         /* 旋回命令 */
    int tmp_r, tmp_g,tmp_b;

    //signed char pwm_L, pwm_R; /* 左右モーターPWM出力 */

    int read_color;


    //配列初期化
    for(int i=0;i<REFLECT_LOG_SIZE;i++){
        reflect_log[i]=0;


    }

    //線検知フラグ
    int detected_flag =0;
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
    ev3_motor_config(centar_motor, LARGE_MOTOR);

    //テールモーター固定
    ev3_motor_stop(centar_motor,true);

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
        fprintf(bt, "Bluetooth Remote Start: Ready.\n", EV3_SERIAL_BT);
        fprintf(bt, "send '1' to start\n", EV3_SERIAL_BT);
    }
    

    /* スタート待機 */
    while(1)
    {
        //tail_control(TAIL_ANGLE_STAND_UP); /* 完全停止用角度に制御 */

        if (bt_cmd == 1)
        {
            break; /* リモートスタート */
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

    /* 走行モーターエンコーダーリセット */
    ev3_motor_reset_counts(left_motor);
    ev3_motor_reset_counts(right_motor);

    ev3_led_set_color(LED_GREEN); /* スタート通知 */

    /**
    * Main loop
    */
    while(1)
    {
        if (ev3_button_is_pressed(BACK_BUTTON)) break;

        
        //if (sonar_alert() == 1) /* 障害物検知 */
        //{
        //    forward = turn = 0; /* 障害物を検知したら停止 */
        //}
        if(0){

        }
        else
        {
            




            matrix_move_sequence(matrix_order, matrix_order_size);
            //ret_color_code();

            //trace_node();
            //turn_90_degree(1);





        }
        
        tslp_tsk(2 * 1000000U); /* 0.4msec周期起動 */

    }

    if (_bt_enabled)
    {
        ter_tsk(BT_TASK);
        fclose(bt);
    }


    ext_tsk();
}
//*****************************************************************************
// 関数名 : matrix_move_sequence
// 引数 :   マトリクス行動ルートを格納した配列, 配列サイズ
// 返り値 : 
// 概要 :　
//*****************************************************************************
void matrix_move_sequence(int *order, int size){
    
    
    for(int i=0; size >i; i++){

        

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
void do_push_blue_block(){

    int rotate_degree=180;

    ev3_motor_rotate(centar_motor, 50 , 50, true);

    ev3_motor_rotate(right_motor, rotate_degree , 50, false);
    ev3_motor_rotate(left_motor , rotate_degree,  50, true);

    tslp_tsk(2 * 1000000U); /* 0.2sec停止 */

    ev3_motor_rotate(right_motor, -rotate_degree , 50, false);
    ev3_motor_rotate(left_motor , -rotate_degree,  50, true);

    ev3_motor_rotate(centar_motor, -50 , 50, true);

}


//*****************************************************************************
// 関数名 : ret_color_code
// 引数 :   L
// 返り値 : COLOR_CODEの整数
// 概要 :　
//*****************************************************************************
int ret_color_code(){
    
    int ret;
    rgb_raw_t read_rgb;
    ev3_color_sensor_get_rgb_raw(color_sensor, &read_rgb);


    
    if(read_rgb.r>THRE_R_OF_WHITE && read_rgb.g>THRE_G_OF_WHITE && read_rgb.b>THRE_B_OF_WHITE){
        //白の時
        ret= COLOR_CODE_WHITE;
        LOG_D_DEBUG("WHITE  r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else if(read_rgb.r<THRE_R_OF_BLUE && read_rgb.g<THRE_G_OF_BLUE && read_rgb.b>THRE_B_OF_BLUE){
        //青の時
        ret= COLOR_CODE_BLUE;
        LOG_D_DEBUG("BLUE   r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else if(read_rgb.r>THRE_R_OF_YELLOW && read_rgb.g>THRE_G_OF_YELLOW && read_rgb.b<THRE_B_OF_YELLOW){
        //黄の時
        ret= COLOR_CODE_YELLOW;
        LOG_D_DEBUG("YELLOW r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else if(read_rgb.r>THRE_R_OF_RED && read_rgb.g<THRE_G_OF_RED && read_rgb.b<THRE_B_OF_RED){
        //赤の時
        ret= COLOR_CODE_RED;
        LOG_D_DEBUG("RED    r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else if(read_rgb.r<THRE_R_OF_GREEN && read_rgb.g>THRE_G_OF_GREEN && read_rgb.b<THRE_B_OF_GREEN){
        //緑の時
        ret= COLOR_CODE_GREEN;
        LOG_D_DEBUG("GREEN  r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }else{
        //不明の時
        ret= COLOR_CODE_UNKNOWN;
        LOG_D_DEBUG("UNKNOWN r= %u g= %u b=%u \n",read_rgb.r, read_rgb.g, read_rgb.b);

    }

    return ret;


}
//*****************************************************************************
// 関数名 :move_to_matrix_start_pos
// 引数   :   1-左コース 2-右コース
// 返り値 : なし
// 概要   :　ライントレース最終部分の赤読み取り時に実行される
//           
//*****************************************************************************
void move_to_matrix_start_pos(int course_flag){

    if(course_type==1){

        turn_70_degree(1);


    }else if(course_type==2){

        turn_70_degree(2);


    }


}


//*****************************************************************************
// 関数名 : motor_set_two_motor
// 引数 :   LeftMotorPower   :左モーターパワー
//          RightMotorPower  :右モーターパワー
// 返り値 : なし
// 概要 :　
//*****************************************************************************
void motor_set_two_motor(int LeftMotorPower, int RightMotorPower){

    ev3_motor_set_power(EV3_PORT_C, LeftMotorPower);
    ev3_motor_set_power(EV3_PORT_B, RightMotorPower);

    
}

//*****************************************************************************
// 関数名 : turn_90_degree
// 引数 :   1-右旋回　2-左旋回
// 返り値 : 
// 概要 :　
//*****************************************************************************
void turn_90_degree(int flag_turn){

    //車体　90度分旋回時　回転角度
    const int rotate_degree=125;

    if(flag_turn==1){
        //右旋回
        ev3_motor_rotate(right_motor, rotate_degree , 20, false);
        ev3_motor_rotate(left_motor , -rotate_degree, 20, true);

    }else if(flag_turn==2){
        //左旋回
        ev3_motor_rotate(left_motor , rotate_degree , 20, false);
        ev3_motor_rotate(right_motor , -rotate_degree, 20, true);

    }
    
}

//*****************************************************************************
// 関数名 : turn_70_degree
// 引数 :   1-右旋回　2-左旋回
// 返り値 : 
// 概要 :　
//*****************************************************************************
void turn_70_degree(int flag_turn){

    //車体　70度分旋回時　回転角度
    const int rotate_degree=97;

    if(flag_turn==1){
        //右旋回
        ev3_motor_rotate(right_motor, rotate_degree , 20, false);
        ev3_motor_rotate(left_motor , -rotate_degree, 20, true);

    }else if(flag_turn==2){
        //左旋回
        ev3_motor_rotate(left_motor , rotate_degree , 20, false);
        ev3_motor_rotate(right_motor , -rotate_degree, 20, true);

    }
    
}
//*****************************************************************************
// 関数名 : trace_node
// 引数 : 
// 返り値 : 
// 概要 :　
//*****************************************************************************
void trace_node(){

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
        read_color=ret_color_code();

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



//*****************************************************************************
// 関数名 : motor_stop
// 引数 :   
// 返り値 : なし
// 概要 :　車体を停止する
//*****************************************************************************
void motor_stop(){
    ev3_motor_stop(EV3_PORT_B,true);
    ev3_motor_stop(EV3_PORT_C,true);

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
static void _syslog(int level, char* text){
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
static void _log(char *text){
    _syslog(LOG_NOTICE, text);
}