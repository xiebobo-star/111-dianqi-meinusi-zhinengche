/*********************************************************************************************************************
* 文件名            imu_store.c
* 说明              零漂 + 航点的片内 Flash 存储（第 8 / 9 页，双页备份）
*
*                   一页数据的格式（都用 uint32 存）：
*                     [0] 魔数 'ZFIM'      判断这页是不是我们写的数据
*                     [1] 版本号 V1.0      格式改了就让老数据自动失效
*                     [2] 保存序号         每保存一次 +1，双页里取序号大的那页当最新
*                     [3] 航点个数
*                     [4] 零漂 X（float 原样存）
*                     [5] 零漂 Y
*                     [6] 零漂 Z
*                     [7] 校验和           前面所有字加起来，防掉电写坏
*                     [8] 是否校准过（1 = 校准过）
*                     [9] 保留
*                     [10 + i*2]  第 i 个航点：低 16 位 = X(mm)，高 16 位 = Y(mm)
*                     [11 + i*2]  第 i 个航点：航向 x10（度）
*
*                   双页策略：永远写「另一页」。写着写着断电，另一页还是上次的完整数据
********************************************************************************************************************/
#include "imu_config.h"
#include "imu_store.h"
#include "imu_attitude.h"
#include "imu_odom.h"

#define IMU_STORE_MAGIC         (0x5A46494DU)                   // 'ZFIM'
#define IMU_STORE_VERSION       (0x00010000U)                   // V1.0
#define IMU_STORE_HEAD_LEN      (10)                            // 头部 10 个 uint32
#define IMU_STORE_CHK_INDEX     (7)                             // 校验和放在第 7 个字
#define IMU_STORE_TOTAL         (IMU_STORE_HEAD_LEN + IMU_ODO_WP_MAX * 2)

static uint32 imu_store_word[IMU_STORE_TOTAL];                  // 组好的数据（回读比对用）
static uint8  imu_store_page_valid[2];                          // A 页 / B 页是否有效
static uint32 imu_store_page_seq[2];                            // A 页 / B 页的保存序号
static uint8  imu_store_loaded;                                 // 本次上电是否已经从 Flash 读到数据
static uint8  imu_store_seq_now;                                // 当前数据的序号（低 8 位够用，页面里存全 32 位）
static uint32 imu_store_seq_full;
static uint8  imu_store_result;                                 // 0 没保存过 1 成功 2 失败
static uint8  imu_store_count;                                  // 保存成功次数

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     校验一页：魔数 / 版本 / 校验和都对才算有效
// 返回参数     1 = 有效，0 = 无效
//-------------------------------------------------------------------------------------------------------------------
static uint8 imu_store_check_page (uint8 page, uint32 *seq)
{
    uint32 sum = 0;
    uint32 i;

    flash_read_page_to_buffer(0, page);

    if (IMU_STORE_MAGIC   != flash_union_buffer[0].uint32_type) { return 0; }
    if (IMU_STORE_VERSION != flash_union_buffer[1].uint32_type) { return 0; }
    if (flash_union_buffer[3].uint32_type > (uint32)IMU_ODO_WP_MAX) { return 0; }

    for (i = 0; i < (uint32)IMU_STORE_TOTAL; i ++)
    {
        if (IMU_STORE_CHK_INDEX != i) { sum += flash_union_buffer[i].uint32_type; }
    }
    if (sum != flash_union_buffer[IMU_STORE_CHK_INDEX].uint32_type) { return 0; }

    *seq = flash_union_buffer[2].uint32_type;
    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     上电载入（零漂 + 航点）
// 返回参数     1 = 有用数据并已载入，0 = 没有（保持默认：零漂 0、航点空）
//-------------------------------------------------------------------------------------------------------------------
uint8 imu_store_load (void)
{
    uint32 seq_a = 0;
    uint32 seq_b = 0;
    uint8  page;
    uint8  i;
    uint8  num;
    uint32 w;

    imu_store_page_valid[0] = imu_store_check_page(IMU_FLASH_PAGE_A, &seq_a);
    imu_store_page_valid[1] = imu_store_check_page(IMU_FLASH_PAGE_B, &seq_b);
    imu_store_page_seq[0]   = seq_a;
    imu_store_page_seq[1]   = seq_b;

    if ((0 != imu_store_page_valid[0]) && (0 != imu_store_page_valid[1]))
    {
        page = (seq_a >= seq_b) ? (IMU_FLASH_PAGE_A) : (IMU_FLASH_PAGE_B);
    }
    else if (0 != imu_store_page_valid[0]) { page = IMU_FLASH_PAGE_A; }
    else if (0 != imu_store_page_valid[1]) { page = IMU_FLASH_PAGE_B; }
    else
    {
        imu_store_loaded = 0;
        imu_store_result = 0;
        return 0;
    }

    flash_read_page_to_buffer(0, page);                             // 把这一页读回缓冲区

    imu_attitude_bias_set(flash_union_buffer[4].float_type,
                          flash_union_buffer[5].float_type,
                          flash_union_buffer[6].float_type);
    if (0 == flash_union_buffer[8].uint32_type) { imu_attitude_bias_clear(); }

    num = (uint8)flash_union_buffer[3].uint32_type;
    imu_odom_wp_clear();
    for (i = 0; i < num; i ++)
    {
        w = flash_union_buffer[IMU_STORE_HEAD_LEN + i * 2].uint32_type;
        imu_odom_wp_set(i,
                        (int16)(w & 0xFFFFU),
                        (int16)((w >> 16) & 0xFFFFU),
                        (int16)(flash_union_buffer[IMU_STORE_HEAD_LEN + i * 2 + 1].uint32_type & 0xFFFFU));
    }
    imu_odom_wp_num_set(num);

    imu_store_seq_full = flash_union_buffer[2].uint32_type;
    imu_store_loaded    = 1;
    imu_store_result    = 1;

#if (1 == IMU_SERIAL_DEBUG)
    printf("[store] load page %d seq %lu  wp %d  bias x%d y%d z%d\r\n",
           (int)page, (unsigned long)imu_store_seq_full, (int)num,
           (int)(flash_union_buffer[4].float_type * 100.0f),
           (int)(flash_union_buffer[5].float_type * 100.0f),
           (int)(flash_union_buffer[6].float_type * 100.0f));
#endif

    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     保存（零漂 + 航点），双页备份 + 写后回读校验
// 返回参数     0 = 成功，1 = 失败
//-------------------------------------------------------------------------------------------------------------------
uint8 imu_store_save (void)
{
    uint32 seq_a = 0;
    uint32 seq_b = 0;
    uint32 new_seq;
    uint32 sum = 0;
    uint32 i;
    uint8  page;
    uint8  ok = 1;
    uint8  num;
    float  bx = 0.0f;
    float  by = 0.0f;
    float  bz = 0.0f;

    // ------------------------- 1. 先看两页现在的状态，决定写哪一页 -------------------------
    imu_store_page_valid[0] = imu_store_check_page(IMU_FLASH_PAGE_A, &seq_a);
    imu_store_page_valid[1] = imu_store_check_page(IMU_FLASH_PAGE_B, &seq_b);
    imu_store_page_seq[0]   = seq_a;
    imu_store_page_seq[1]   = seq_b;

    if ((0 != imu_store_page_valid[0]) && (0 != imu_store_page_valid[1]))
    {
        if (seq_a <= seq_b) { page = IMU_FLASH_PAGE_A; new_seq = seq_b + 1; }
        else                { page = IMU_FLASH_PAGE_B; new_seq = seq_a + 1; }
    }
    else if (0 != imu_store_page_valid[0]) { page = IMU_FLASH_PAGE_B; new_seq = seq_a + 1; }
    else if (0 != imu_store_page_valid[1]) { page = IMU_FLASH_PAGE_A; new_seq = seq_b + 1; }
    else                                   { page = IMU_FLASH_PAGE_A; new_seq = 1; }

    // ------------------------- 2. 组数据 -------------------------
    imu_attitude_bias_get(&bx, &by, &bz);
    num = imu_odom_wp_num();

    flash_buffer_clear();                                           // 整页先填 0xFF（空 Flash 的样子）
    flash_union_buffer[0].uint32_type = IMU_STORE_MAGIC;
    flash_union_buffer[1].uint32_type = IMU_STORE_VERSION;
    flash_union_buffer[2].uint32_type = new_seq;
    flash_union_buffer[3].uint32_type = (uint32)num;
    flash_union_buffer[4].float_type  = bx;
    flash_union_buffer[5].float_type  = by;
    flash_union_buffer[6].float_type  = bz;
    flash_union_buffer[7].uint32_type = 0;                          // 校验和先占位
    flash_union_buffer[8].uint32_type = (0 != imu_attitude_calib_done()) ? (1) : (0);
    flash_union_buffer[9].uint32_type = 0;

    for (i = 0; i < num; i ++)
    {
        int16 wx = 0;
        int16 wy = 0;
        int16 wyaw = 0;

        imu_odom_wp_get((uint8)i, &wx, &wy, &wyaw);
        flash_union_buffer[IMU_STORE_HEAD_LEN + i * 2].uint32_type     = ((uint32)(uint16)wx) | (((uint32)(uint16)wy) << 16);
        flash_union_buffer[IMU_STORE_HEAD_LEN + i * 2 + 1].uint32_type = (uint32)(uint16)wyaw;
    }

    for (i = 0; i < (uint32)IMU_STORE_TOTAL; i ++)
    {
        if (IMU_STORE_CHK_INDEX != i) { sum += flash_union_buffer[i].uint32_type; }
    }
    flash_union_buffer[IMU_STORE_CHK_INDEX].uint32_type = sum;

    for (i = 0; i < (uint32)IMU_STORE_TOTAL; i ++)
    {
        imu_store_word[i] = flash_union_buffer[i].uint32_type;      // 留一份副本，回读比对用
    }

    // ------------------------- 3. 擦页 -> 写页 -------------------------
    flash_erase_page(0, page);
    flash_write_page(0, page, (const uint32 *)flash_union_buffer, (uint16)IMU_STORE_TOTAL);

    // ------------------------- 4. 回读校验 -------------------------
    flash_read_page_to_buffer(0, page);
    for (i = 0; i < (uint32)IMU_STORE_TOTAL; i ++)
    {
        if (imu_store_word[i] != flash_union_buffer[i].uint32_type)
        {
            ok = 0;
            break;
        }
    }

    if (0 != ok)
    {
        imu_store_loaded      = 1;
        imu_store_result      = 1;
        imu_store_count      ++;
        imu_store_seq_full    = new_seq;
        imu_store_seq_now     = (uint8)(new_seq & 0xFFU);
        if (IMU_FLASH_PAGE_A == page) { imu_store_page_valid[0] = 1; imu_store_page_seq[0] = new_seq; }
        else                          { imu_store_page_valid[1] = 1; imu_store_page_seq[1] = new_seq; }

#if (1 == IMU_SERIAL_DEBUG)
        printf("[store] save ok page %d seq %lu  wp %d\r\n", (int)page, (unsigned long)new_seq, (int)num);
#endif
        return 0;
    }

    imu_store_result = 2;

#if (1 == IMU_SERIAL_DEBUG)
    printf("[store] save FAILED page %d\r\n", (int)page);
#endif

    return 1;
}

//-------------------------------------------------------------------------------------------------------------------
// 函数简介     状态查询
//-------------------------------------------------------------------------------------------------------------------
uint8  imu_store_valid       (void) { return imu_store_loaded;      }
uint32 imu_store_seq         (void) { return imu_store_seq_full;   }
uint8  imu_store_save_count  (void) { return imu_store_count;      }
uint8  imu_store_last_result (void) { return imu_store_result;     }
