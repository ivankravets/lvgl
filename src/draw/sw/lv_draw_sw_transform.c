/**
 * @file lv_draw_sw_tranform.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_draw_sw.h"
#include "../../misc/lv_assert.h"
#include "../../misc/lv_area.h"

#if LV_DRAW_COMPLEX
/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
    int32_t x_in;
    int32_t y_in;
    int32_t x_out;
    int32_t y_out;
    int32_t sinma;
    int32_t cosma;
    int32_t zoom;
    int32_t angle;
    lv_point_t pivot;
} point_transform_dsc_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
/**
 * Transform a point with 1/256 precision (the output coordinates are upscaled by 256)
 * @param t         pointer to n initialized `point_transform_dsc_t` structure
 * @param xin       X coordinate to rotate
 * @param yin       Y coordinate to rotate
 * @param xout      upscaled, transformed X
 * @param yout      upscaled, transformed Y
 */
static void transform_point_upscaled(point_transform_dsc_t * t, int32_t xin, int32_t yin, int32_t * xout,
                                     int32_t * yout);

static void argb_no_aa(const uint8_t * src, lv_coord_t src_w, lv_coord_t src_h, lv_coord_t src_stride,
                       int32_t xs_ups, int32_t ys_ups, int32_t xs_step, int32_t ys_step,
                       int32_t x_end, lv_color_t * cbuf, uint8_t * abuf);

static void rgb_no_aa(const uint8_t * src, lv_coord_t src_w, lv_coord_t src_h, lv_coord_t src_stride,
                      int32_t xs_ups, int32_t ys_ups, int32_t xs_step, int32_t ys_step,
                      int32_t x_end, lv_color_t * cbuf, uint8_t * abuf);

static void argb_and_rgb_aa(const uint8_t * src, lv_coord_t src_w, lv_coord_t src_h, lv_coord_t src_stride,
                            int32_t xs_ups, int32_t ys_ups, int32_t xs_step, int32_t ys_step,
                            int32_t x_end, lv_color_t * cbuf, uint8_t * abuf, bool has_alpha);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_sw_transform(lv_draw_ctx_t * draw_ctx, const lv_area_t * dest_area, const void * src_buf,
                          lv_coord_t src_w, lv_coord_t src_h, lv_coord_t src_stride,
                          const lv_draw_img_dsc_t * draw_dsc, lv_img_cf_t cf, lv_color_t * cbuf, lv_opa_t * abuf)
{
    LV_UNUSED(draw_ctx);

    point_transform_dsc_t tr_dsc;
    tr_dsc.angle = -draw_dsc->angle;
    tr_dsc.zoom = (256 * 256) / draw_dsc->zoom;
    tr_dsc.pivot = draw_dsc->pivot;

    int32_t angle_low = tr_dsc.angle / 10;
    int32_t angle_high = angle_low + 1;
    int32_t angle_rem = tr_dsc.angle  - (angle_low * 10);

    int32_t s1 = lv_trigo_sin(angle_low);
    int32_t s2 = lv_trigo_sin(angle_high);

    int32_t c1 = lv_trigo_sin(angle_low + 90);
    int32_t c2 = lv_trigo_sin(angle_high + 90);

    tr_dsc.sinma = (s1 * (10 - angle_rem) + s2 * angle_rem) / 10;
    tr_dsc.cosma = (c1 * (10 - angle_rem) + c2 * angle_rem) / 10;
    tr_dsc.sinma = tr_dsc.sinma >> (LV_TRIGO_SHIFT - 10);
    tr_dsc.cosma = tr_dsc.cosma >> (LV_TRIGO_SHIFT - 10);

    lv_coord_t dest_w = lv_area_get_width(dest_area);
    lv_coord_t dest_h = lv_area_get_height(dest_area);
    lv_coord_t y;
    bool has_alpha = lv_img_cf_has_alpha(cf);
    for(y = 0; y < dest_h; y++) {
        int32_t xs1_ups, ys1_ups, xs2_ups, ys2_ups;

        transform_point_upscaled(&tr_dsc, dest_area->x1, dest_area->y1 + y, &xs1_ups, &ys1_ups);
        transform_point_upscaled(&tr_dsc, dest_area->x2, dest_area->y1 + y, &xs2_ups, &ys2_ups);

        int32_t xs_diff = xs2_ups - xs1_ups;
        int32_t ys_diff = ys2_ups - ys1_ups;
        int32_t xs_step_256 = 0;
        int32_t ys_step_256 = 0;
        if(dest_w > 1) {
            xs_step_256 = (256 * xs_diff) / (dest_w - 1);
            ys_step_256 = (256 * ys_diff) / (dest_w - 1);
        }
        int32_t xs_ups = xs1_ups + 1 * xs_step_256 / 2 / 256;     /*Init. + go the center of the pixel*/
        int32_t ys_ups = ys1_ups + 1 * ys_step_256 / 2 / 256;

        if(draw_dsc->antialias == 0) {
            if(cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {
                argb_no_aa(src_buf, src_w, src_h, src_stride, xs_ups, ys_ups, xs_step_256, ys_step_256, dest_w, cbuf, abuf);
            }
            else {
                rgb_no_aa(src_buf, src_w, src_h, src_stride, xs_ups, ys_ups, xs_step_256, ys_step_256, dest_w, cbuf, abuf);
            }
        }
        else {
            argb_and_rgb_aa(src_buf, src_w, src_h, src_stride, xs_ups, ys_ups, xs_step_256, ys_step_256, dest_w, cbuf, abuf,
                            has_alpha);
        }
        cbuf += dest_w;
        abuf += dest_w;
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void rgb_no_aa(const uint8_t * src, lv_coord_t src_w, lv_coord_t src_h, lv_coord_t src_stride,
                      int32_t xs_ups, int32_t ys_ups, int32_t xs_step, int32_t ys_step,
                      int32_t x_end, lv_color_t * cbuf, uint8_t * abuf)
{
    int32_t xs_ups_start = xs_ups;
    int32_t ys_ups_start = ys_ups;

    lv_memset_ff(abuf, x_end);

    lv_coord_t x;
    for(x = 0; x < x_end; x++) {
        xs_ups = xs_ups_start + ((xs_step * x) >> 8);
        ys_ups = ys_ups_start + ((ys_step * x) >> 8);

        int32_t xs_int = xs_ups >> 8;
        int32_t ys_int = ys_ups >> 8;
        if(xs_int < 0 || xs_int >= src_w || ys_int < 0 || ys_int >= src_h) {
            abuf[x] = 0;
        }
        else {
            const uint8_t * src_tmp = src;
            src_tmp += (ys_int * src_stride * sizeof(lv_color_t)) + xs_int * sizeof(lv_color_t);

#if LV_COLOR_DEPTH == 8
            cbuf[x].full = src_tmp[0];
#elif LV_COLOR_DEPTH == 16
            cbuf[x].full = src_tmp[0] + (src_tmp[1] << 8);
#elif LV_COLOR_DEPTH == 32
            cbuf[x].full = *((uint32_t *)src_tmp);
#endif
        }
    }
}

static void argb_no_aa(const uint8_t * src, lv_coord_t src_w, lv_coord_t src_h, lv_coord_t src_stride,
                       int32_t xs_ups, int32_t ys_ups, int32_t xs_step, int32_t ys_step,
                       int32_t x_end, lv_color_t * cbuf, uint8_t * abuf)
{
    int32_t xs_ups_start = xs_ups;
    int32_t ys_ups_start = ys_ups;

    lv_coord_t x;
    for(x = 0; x < x_end; x++) {
        xs_ups = xs_ups_start + ((xs_step * x) >> 8);
        ys_ups = ys_ups_start + ((ys_step * x) >> 8);

        int32_t xs_int = xs_ups >> 8;
        int32_t ys_int = ys_ups >> 8;
        if(xs_int < 0 || xs_int >= src_w || ys_int < 0 || ys_int >= src_h) {
            abuf[x] = 0;
        }
        else {
            const uint8_t * src_tmp = src;
            src_tmp += (ys_int * src_stride * LV_IMG_PX_SIZE_ALPHA_BYTE) + xs_int * LV_IMG_PX_SIZE_ALPHA_BYTE;

#if LV_COLOR_DEPTH == 8
            cbuf[x].full = src_tmp[0];
#elif LV_COLOR_DEPTH == 16
            cbuf[x].full = src_tmp[0] + (src_tmp[1] << 8);
#elif LV_COLOR_DEPTH == 32
            cbuf[x].full = *((uint32_t *)src_tmp);
#endif
            abuf[x] = src_tmp[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
        }
    }
}

static void argb_and_rgb_aa(const uint8_t * src, lv_coord_t src_w, lv_coord_t src_h, lv_coord_t src_stride,
                            int32_t xs_ups, int32_t ys_ups, int32_t xs_step, int32_t ys_step,
                            int32_t x_end, lv_color_t * cbuf, uint8_t * abuf, bool has_alpha)
{

    int32_t xs_ups_start = xs_ups;
    int32_t ys_ups_start = ys_ups;
    const int32_t px_size = has_alpha ? LV_IMG_PX_SIZE_ALPHA_BYTE : sizeof(lv_color_t);

    lv_coord_t x;
    for(x = 0; x < x_end; x++) {
        xs_ups = xs_ups_start + ((xs_step * x) >> 8);
        ys_ups = ys_ups_start + ((ys_step * x) >> 8);

        int32_t xs_int = xs_ups >> 8;
        int32_t ys_int = ys_ups >> 8;

        /*Fully out of the image*/
        if(xs_int < 0 || xs_int >= src_w || ys_int < 0 || ys_int >= src_h) {
            abuf[x] = 0x00;
            continue;
        }


        /*Get the direction the hor and ver neighbor
         *`fract` will be in range of 0x00..0xFF and `next` (+/-1) indicates the direction*/
        int32_t xs_fract = xs_ups & 0xFF;
        int32_t ys_fract = ys_ups & 0xFF;

        int32_t x_next;
        int32_t y_next;
        if(xs_fract < 0x80) {
            x_next = -1;
            xs_fract = (0x7F - xs_fract) * 2;
        }
        else {
            x_next = 1;
            xs_fract = (xs_fract - 0x80) * 2;
        }
        if(ys_fract < 0x80) {
            y_next = -1;
            ys_fract = (0x7F - ys_fract) * 2;
        }
        else {
            y_next = 1;
            ys_fract = (ys_fract - 0x80) * 2;
        }

        const uint8_t * src_tmp = src;
        src_tmp += (ys_int * src_stride * px_size) + xs_int * px_size;

        if(xs_int + x_next >= 0 &&
           xs_int + x_next <= src_w - 1 &&
           ys_int + y_next >= 0 &&
           ys_int + y_next <= src_h - 1) {

            const uint8_t * px_base = src_tmp;
            const uint8_t * px_hor = src_tmp + x_next * px_size;
            const uint8_t * px_ver = src_tmp + y_next * src_stride * px_size;
            lv_color_t c_base;
            lv_color_t c_ver;
            lv_color_t c_hor;

            if(has_alpha) {
                lv_opa_t a_base = px_base[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
                lv_opa_t a_ver = px_ver[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];
                lv_opa_t a_hor = px_hor[LV_IMG_PX_SIZE_ALPHA_BYTE - 1];

                if(a_ver != a_base) a_ver = ((a_ver * ys_fract) + (a_base * (0x100 - ys_fract))) >> 8;
                if(a_hor != a_base) a_hor = ((a_hor * xs_fract) + (a_base * (0x100 - xs_fract))) >> 8;
                abuf[x] = (a_ver + a_hor) >> 1;

                if(abuf[x] == 0x00) continue;

#if LV_COLOR_DEPTH == 8
                c_base.full = px_base[0];
                c_ver.full = px_ver[0];
                c_hor.full = px_hor[0];
#elif LV_COLOR_DEPTH == 16
                c_base.full = px_base[0] + (px_base[1] << 8);
                c_ver.full = px_ver[0] + (px_ver[1] << 8);
                c_hor.full = px_hor[0] + (px_hor[1] << 8);
#elif LV_COLOR_DEPTH == 32
                c_base.full = *((uint32_t *)px_base);
                c_ver.full = *((uint32_t *)px_ver);
                c_hor.full = *((uint32_t *)px_hor);
#endif
            }
            /*No alpha channel -> RGB*/
            else {
                c_base = *((const lv_color_t *) px_base);
                c_hor = *((const lv_color_t *) px_hor);
                c_ver = *((const lv_color_t *) px_ver);
                abuf[x] = 0xff;
            }

            if(c_base.full == c_ver.full && c_base.full == c_hor.full) {
                cbuf[x] = c_base;
            }
            else {
                c_ver = lv_color_mix(c_ver, c_base, ys_fract);
                c_hor = lv_color_mix(c_hor, c_base, xs_fract);
                cbuf[x] = lv_color_mix(c_hor, c_ver, LV_OPA_50);
            }
        }
        /*Partially out of the image*/
        else {
#if LV_COLOR_DEPTH == 8
            cbuf[x].full = src_tmp[0];
#elif LV_COLOR_DEPTH == 16
            cbuf[x].full = src_tmp[0] + (src_tmp[1] << 8);
#elif LV_COLOR_DEPTH == 32
            cbuf[x].full = *((uint32_t *)src_tmp);
#endif

            lv_opa_t a = has_alpha ? src_tmp[LV_IMG_PX_SIZE_ALPHA_BYTE - 1] : 0xff;

            if((xs_int == 0 && x_next < 0) || (xs_int == src_w - 1 && x_next > 0))  {
                abuf[x] = (a * (0xFF - xs_fract)) >> 8;
            }
            else if((ys_int == 0 && y_next < 0) || (ys_int == src_h - 1 && y_next > 0))  {
                abuf[x] = (a * (0xFF - ys_fract)) >> 8;
            }
            else {
                abuf[x] = 0x00;
            }
        }
    }
}

static void transform_point_upscaled(point_transform_dsc_t * t, int32_t xin, int32_t yin, int32_t * xout,
                                     int32_t * yout)
{
    if(t->angle == 0 && t->zoom == LV_IMG_ZOOM_NONE) {
        *xout = xin << 8;
        *yout = yin << 8;
        return;
    }

    xin -= t->pivot.x;
    yin -= t->pivot.y;

    if(t->angle == 0) {
        *xout = ((int32_t)(xin * t->zoom)) + (t->pivot.x << 8);
        *yout = ((int32_t)(yin * t->zoom)) + (t->pivot.y << 8);
    }
    else if(t->zoom == LV_IMG_ZOOM_NONE) {
        *xout = ((t->cosma * xin - t->sinma * yin) >> 2) + (t->pivot.x << 8);
        *yout = ((t->sinma * xin + t->cosma * yin) >> 2) + (t->pivot.y << 8);
    }
    else {
        *xout = (((t->cosma * xin - t->sinma * yin) * t->zoom) >> 10) + (t->pivot.x << 8);
        *yout = (((t->sinma * xin + t->cosma * yin) * t->zoom) >> 10) + (t->pivot.y << 8);
    }
}

#endif

