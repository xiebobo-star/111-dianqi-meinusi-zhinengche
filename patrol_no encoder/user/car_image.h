#ifndef CAR_IMAGE_H
#define CAR_IMAGE_H
#include "zf_common_headfile.h"
#include "car_config.h"
#define CAR_IMG_W MT9V03X_W
#define CAR_IMG_H MT9V03X_H
#define CAR_IMG_CENTER (CAR_IMG_W / 2)
#define CAR_IMG_SCAN_STEP 2
#define CAR_IMG_SCAN_ROWS (CAR_IMG_H / CAR_IMG_SCAN_STEP)
#define CAR_ROW_FLAG_LOST 0
#define CAR_ROW_FLAG_REAL 1
#define CAR_ROW_FLAG_FIX 2
#define CAR_IMG_MODE_SINGLE 0
#define CAR_IMG_MODE CAR_IMG_MODE_SINGLE
#define CAR_IMG_LINE_DARK 0

typedef enum {
    CAR_STATE_LOST, CAR_STATE_CENTER, CAR_STATE_LEFT, CAR_STATE_RIGHT,
    CAR_STATE_LEFT_BIG, CAR_STATE_RIGHT_BIG, CAR_STATE_JUNCTION
} car_image_state_enum;
typedef struct {
    uint8 valid, state;
    uint16 threshold;
    uint8 valid_rows, lost_rows;
    uint8 left[CAR_IMG_SCAN_ROWS], right[CAR_IMG_SCAN_ROWS];
    uint8 center[CAR_IMG_SCAN_ROWS], width[CAR_IMG_SCAN_ROWS];
    uint8 row_flag[CAR_IMG_SCAN_ROWS];
    uint8 near_center, far_center;
    int16 err_near, err_mid, err_far, err, err_norm;
    uint8 confidence, junction, contrast, near_rows, mid_rows, far_rows;
    uint8 route_used; /* Junction rows resolved using requested visible edge. */
    int16 bend_norm;
} car_image_info_struct;
void car_image_init(void);
void car_image_process(car_image_info_struct *info);
void car_image_process_gray(const uint8 *image, car_image_info_struct *info);
/* CPU0 main only: 0 normal; -1 left edge/branch; +1 right edge/branch. */
void car_image_set_route(int16 direction);
void car_image_set_threshold(uint16 threshold);
uint16 car_image_get_threshold(void);
const char *car_image_state_name(uint8 state);
#endif
