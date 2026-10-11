#include "flatpack2.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

void fp2_build_login(fp2_frame_t *f, const fp2_t *p)
{
    uint8_t id = p->id ? p->id : 1;
    f->id = 0x05004800u | (uint32_t)(id * 4u);
    f->dlc = 8;
    memcpy(f->d, p->serial, 6);
    f->d[6] = 0; f->d[7] = 0;
}

void fp2_build_set(fp2_frame_t *f, uint16_t i_da, uint16_t v_cv, uint16_t ovp_cv)
{
    if (v_cv < FP2_V_MIN_CV) v_cv = FP2_V_MIN_CV;
    if (v_cv > FP2_V_MAX_CV) v_cv = FP2_V_MAX_CV;
    if (i_da > FP2_I_MAX_DA) i_da = FP2_I_MAX_DA;
    f->id = 0x05FF4004u;
    f->dlc = 8;
    put16(&f->d[0], i_da); put16(&f->d[2], v_cv); put16(&f->d[4], v_cv); put16(&f->d[6], ovp_cv);
}

void fp2_build_default_v(fp2_frame_t *f, const fp2_t *p, uint16_t v_cv)
{
    uint8_t id = p->id ? p->id : 1;
    f->id = 0x05009C00u | ((uint32_t)id << 16);
    f->dlc = 5;
    f->d[0] = 0x29; f->d[1] = 0x15; f->d[2] = 0x00; put16(&f->d[3], v_cv);
    f->d[5] = f->d[6] = f->d[7] = 0;
}

bool fp2_on_rx(fp2_t *p, uint32_t id, const uint8_t *d, uint8_t dlc, uint32_t now)
{
    if ((id & 0xFF000000u) != 0x05000000u) return false;
    if ((id & 0x00FF0000u) == 0 && dlc >= 7 && d[0] == 0x1B) {      /* hello 0x0500XXXX */
        memcpy(p->serial, &d[1], 6);
        p->seen = true;
        if (!p->id) p->id = 1;
        return true;
    }
    if ((id & 0x0000FF00u) == 0x00004000u && dlc >= 8) {            /* status 0x05II40SS */
        p->id = (uint8_t)((id >> 16) & 0xFF);
        p->state = (uint8_t)(id & 0xFF);
        p->t_in_c = (int8_t)d[0];
        p->i_da = get16(&d[1]);
        p->vout_cv = get16(&d[3]);
        p->vin_v = get16(&d[5]);
        p->t_out_c = (int8_t)d[7];
        p->status_valid = true;
        p->status_ms = now;
        return true;
    }
    return true;                                                      /* other Flatpack frame */
}

uint16_t fp2_i_limit_da(uint16_t v_cv)
{
    if (v_cv < 100) return FP2_I_MAX_DA;
    uint32_t i = (uint32_t)FP2_P_MAX_W * 1000u / v_cv;   /* W / (V/100) * 10 -> 0.1 A */
    return (uint16_t)(i < FP2_I_MAX_DA ? i : FP2_I_MAX_DA);
}
