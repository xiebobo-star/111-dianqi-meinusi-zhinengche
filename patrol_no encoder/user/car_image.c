/* White ROAD region on darker blue mat. No black boundary tape required.
 * CPU0 only. Width history is row-specific and expires; inferred edges are
 * never used to train width. Junction detection is heuristic, not routing. */
#include "car_image.h"
#include <string.h>
#pragma section all "cpu0_dsram"
static uint16 manual_th, threshold;
static uint8 widths[CAR_IMG_SCAN_ROWS], ages[CAR_IMG_SCAN_ROWS];
static uint8 route_widths[CAR_IMG_SCAN_ROWS];
static int16 route;
static uint8 row_pixels[CAR_IMG_W];
static int16 seed = CAR_IMG_CENTER;
static uint8 lost_frames;
static int iabs(int x) { return x < 0 ? -x : x; }
static int clip(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

/* ROI histogram, Otsu partition, then midpoint of class means. Using the
 * partition index directly is unstable on two nearly uniform surfaces. */
static uint16 get_threshold(const uint8 *im, uint8 *contrast)
{
    uint16 hist[256] = {0};
    uint32 total=0, sum=0, count=0, partial=0, best_count=0, best_sum=0;
    float best=-1.0f;
    int x,y,t;
    for(y=CAR_ROI_TOP; y<=CAR_ROI_BOTTOM; y+=2)
        for(x=0; x<CAR_IMG_W; x+=2) {
            uint8 v=im[y*CAR_IMG_W+x]; hist[v]++; total++; sum+=v;
        }
    for(t=0;t<255;t++) {
        float a,b,score;
        count+=hist[t]; partial+=(uint32)t*hist[t];
        if(!count || count==total) continue;
        a=(float)partial/count; b=(float)(sum-partial)/(total-count);
        score=(float)count*(total-count)*(a-b)*(a-b);
        if(score>best) { best=score; best_count=count; best_sum=partial; }
    }
    *contrast=0;
    if(best_count && best_count<total) {
        int dark=(int)(best_sum/best_count);
        int light=(int)((sum-best_sum)/(total-best_count));
        *contrast=(uint8)(light-dark);
        t=(dark+light)/2;
        threshold=threshold ? (uint16)((threshold+t)/2) : (uint16)t;
        /* Keep threshold between current class means after lighting steps. */
        threshold=(uint16)clip(threshold,dark+1,light-1);
    }
    if(manual_th) return manual_th;
    return threshold;
}

void car_image_init(void)
{
    manual_th=threshold=0; seed=CAR_IMG_CENTER; lost_frames=0;
    memset(widths,0,sizeof(widths)); memset(ages,0,sizeof(ages));
    memset(route_widths,0,sizeof(route_widths)); route=0;
}
void car_image_set_route(int16 direction)
{
    int n;
    direction=direction<0 ? -1 : (direction>0 ? 1 : 0);
    if(direction && direction!=route) {
        /* Snapshot only recent measured widths; expire by controller's
         * junction deadline, never learn widths from generated paths. */
        for(n=0;n<CAR_IMG_SCAN_ROWS;n++)
            route_widths[n]=ages[n]<=CAR_WIDTH_MEMORY_FRAMES ? widths[n] : 0;
    }
    route=direction;
}
void car_image_set_threshold(uint16 value) { manual_th=value>255 ? 255 : value; }
uint16 car_image_get_threshold(void) { return manual_th ? manual_th : threshold; }
void car_image_process(car_image_info_struct *info)
{ car_image_process_gray(&mt9v03x_image[0][0],info); }

void car_image_process_gray(const uint8 *im, car_image_info_struct *o)
{
    int n,x,y,last=seed,previous_width=0,have_previous=0,missing=0;
    int sums[3]={0,0,0}, counts[3]={0,0,0}, real=0, ambiguous=0;
    int bottom_center=seed, got_bottom=0, weighted=0, weight=0;
    int resolved=0, unresolved=0;
    const int weights[3]={3,4,3};
    memset(o,0,sizeof(*o));
    o->threshold=get_threshold(im,&o->contrast);
    for(n=0;n<CAR_IMG_SCAN_ROWS;n++) {
        o->left[n]=o->right[n]=o->center[n]=CAR_IMG_CENTER;
        if(ages[n]<255) ages[n]++;
    }
    for(n=CAR_ROI_BOTTOM/2; n>=CAR_ROI_TOP/2; n--) {
        int best_l=-1,best_r=0,best_c=0,best_score=10000,candidates=0;
        int band,full=0,best_fixed=0;
        y=n*2+1;
        if(o->contrast<CAR_MIN_CONTRAST) { o->lost_rows++; continue; }
        for(x=0;x<CAR_IMG_W;x++) row_pixels[x]=(im[y*CAR_IMG_W+x]>o->threshold);
        /* Close only small internal dark gaps, not large holes in a loop. */
        x=0;
        while(x<CAR_IMG_W) {
            int begin,end;
            if(row_pixels[x]) { x++; continue; }
            begin=x; while(x<CAR_IMG_W && !row_pixels[x]) x++; end=x;
            if(begin>0 && end<CAR_IMG_W && end-begin<=CAR_SEAM_GAP)
                while(begin<end) row_pixels[begin++]=1;
        }
        x=0;
        while(x<CAR_IMG_W) {
            int l,r,w,c,score,fixed=0,known=route_widths[n];
            if(!row_pixels[x]) { x++; continue; }
            l=x; while(x<CAR_IMG_W && row_pixels[x]) x++; r=x-1;
            w=r-l+1; c=(l+r)/2;
            if(w>CAR_ROAD_MAX_WIDTH && !(route && known)) { full=1; continue; }
            if(w<CAR_ROAD_MIN_WIDTH) continue;
            if(route && known && w>known*3/2+8) {
                /* A visible outer edge is mandatory, including at a merge.
                 * Never synthesize a path through an all-white frame. */
                if((route<0 && l==0) || (route>0 && r==CAR_IMG_W-1)) {
                    full=1; continue;
                }
                c=route<0 ? l+known/2 : r-known/2;
                fixed=1;
            } else if((l==0 || r==CAR_IMG_W-1)) {
                if(!widths[n] || ages[n]>CAR_WIDTH_MEMORY_FRAMES) continue;
                c=l==0 ? r-widths[n]/2 : l+widths[n]/2;
            }
            if(iabs(c-last)>CAR_ROAD_JUMP) continue;
            candidates++;
            score=iabs(c-last)*4;
            if(widths[n] && ages[n]<=CAR_WIDTH_MEMORY_FRAMES) score+=iabs(w-widths[n]);
            if((!route && score<best_score) || (route && (best_l<0 ||
               (route<0 ? c<best_c : c>best_c)))) {
                best_score=score; best_l=l; best_r=r; best_c=c; best_fixed=fixed;
            }
        }
        o->center[n]=(uint8)clip(last,0,CAR_IMG_W-1);
        if(best_l<0) {
            o->lost_rows++; missing++;
            if(full) { ambiguous++; unresolved++; }
            /* Do not reconnect to an unrelated far road beyond a broad gap. */
            if(have_previous && missing>3) break;
            continue;
        }
        {
            int w=best_r-best_l+1;
            int bounded=best_l>0 && best_r<CAR_IMG_W-1 && !best_fixed;
            int wide=(have_previous && w>previous_width*3/2+8) ||
                     (widths[n] && ages[n]<=CAR_WIDTH_MEMORY_FRAMES && w>widths[n]*3/2+8);
            if(candidates>1 || wide || best_fixed) {
                ambiguous++;
                if(route && (!wide || best_fixed)) {
                    resolved++;
                } else {
                    unresolved++; o->lost_rows++; missing++;
                    if(missing>3) break;
                    continue;
                }
            }
            o->left[n]=(uint8)best_l; o->right[n]=(uint8)best_r;
            o->center[n]=(uint8)clip(best_c,0,CAR_IMG_W-1);
            o->width[n]=(uint8)w;
            o->row_flag[n]=bounded ? CAR_ROW_FLAG_REAL : CAR_ROW_FLAG_FIX;
            if(bounded) { widths[n]=(uint8)w; ages[n]=0; real++; }
            last=o->center[n]; previous_width=best_fixed ? route_widths[n] : (bounded ? w : widths[n]);
            have_previous=1; missing=0; o->valid_rows++;
            if(!got_bottom) { bottom_center=last; got_bottom=1; }
            band=y>=86 ? 0 : (y>=60 ? 1 : 2);
            sums[band]+=last-CAR_IMG_CENTER; counts[band]++;
        }
    }
    o->lost_rows=(uint8)((CAR_ROI_BOTTOM/2-CAR_ROI_TOP/2+1)-o->valid_rows);
    o->near_rows=(uint8)counts[0]; o->mid_rows=(uint8)counts[1]; o->far_rows=(uint8)counts[2];
    o->junction=(ambiguous>=3);
    o->route_used=(resolved>=3 && !unresolved);
    o->valid=(counts[0]>=5 && counts[1]>=4 && o->valid_rows>=12 && real>=6);
    o->confidence=(uint8)((real*100+(o->valid_rows-real)*50)/40);
    o->err_near=counts[0] ? (int16)(sums[0]/counts[0]) : 0;
    o->err_mid=counts[1] ? (int16)(sums[1]/counts[1]) : o->err_near;
    o->err_far=counts[2] ? (int16)(sums[2]/counts[2]) : o->err_mid;
    for(n=0;n<3;n++) if(counts[n]>=3) { weighted+=(sums[n]/counts[n])*weights[n]; weight+=weights[n]; }
    o->err=weight ? (int16)(weighted/weight) : 0;
    o->err_norm=(int16)(o->err*1000/CAR_IMG_CENTER);
    o->bend_norm=(int16)clip((iabs(o->err_mid-o->err_near)+iabs(o->err_far-o->err_mid))*1000/CAR_IMG_CENTER,0,1000);
    o->near_center=(uint8)(CAR_IMG_CENTER+o->err_near);
    o->far_center=(uint8)(CAR_IMG_CENTER+o->err_far);
    if(o->valid) { seed=(int16)bottom_center; lost_frames=0; }
    else if(++lost_frames>=3) {
        seed=CAR_IMG_CENTER; lost_frames=3; memset(widths,0,sizeof(widths));
    }
    if(!o->valid) o->state=CAR_STATE_LOST;
    else if(o->junction) o->state=CAR_STATE_JUNCTION;
    else if(o->err_norm>500) o->state=CAR_STATE_RIGHT_BIG;
    else if(o->err_norm>120) o->state=CAR_STATE_RIGHT;
    else if(o->err_norm< -500) o->state=CAR_STATE_LEFT_BIG;
    else if(o->err_norm< -120) o->state=CAR_STATE_LEFT;
    else o->state=CAR_STATE_CENTER;
}
const char *car_image_state_name(uint8 s)
{
    switch(s) {
    case CAR_STATE_CENTER:return "CENTER";
    case CAR_STATE_LEFT:return "LEFT";
    case CAR_STATE_RIGHT:return "RIGHT";
    case CAR_STATE_LEFT_BIG:return "LEFT!";
    case CAR_STATE_RIGHT_BIG:return "RIGHT!";
    case CAR_STATE_JUNCTION:return "JUNCTION";
    default:return "LOST";
    }
}
#pragma section all restore
