/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_BLIT_H
#define B2L_BLIT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* The source and destination extents are real mapped allocations. Never derive
 * memory bounds solely from userspace width/height. No resize or YUV fallback. */
static inline int b2l_blit(void *dst,uint64_t dbytes,uint32_t dw,uint32_t dh,uint32_t ds,
    const void *src,uint64_t sbytes,uint32_t sw,uint32_t sh,uint32_t ss,uint32_t soff,
    int32_t sx,int32_t sy,int32_t dx,int32_t dy,int32_t width,int32_t height)
{
    if(!dst||!src||!dw||!dh||!sw||!sh||dw>16384||dh>16384||sw>16384||sh>16384||
       ds<dw*4||ss<sw*4||ds>65536||ss>65536||sx<0||sy<0||dx<0||dy<0||
       (uint32_t)sx>=sw||(uint32_t)sy>=sh||(uint32_t)dx>=dw||(uint32_t)dy>=dh||
       width<=0||height<=0)return 0;
    uint32_t w=(uint32_t)width,h=(uint32_t)height;
    if(w>sw-(uint32_t)sx)w=sw-(uint32_t)sx;
    if(w>dw-(uint32_t)dx)w=dw-(uint32_t)dx;
    if(h>sh-(uint32_t)sy)h=sh-(uint32_t)sy;
    if(h>dh-(uint32_t)dy)h=dh-(uint32_t)dy;
    uint64_t end_s=(uint64_t)soff+((uint64_t)sy+h-1)*ss+((uint64_t)sx+w)*4;
    uint64_t end_d=((uint64_t)dy+h-1)*ds+((uint64_t)dx+w)*4;
    if(end_s>sbytes||end_d>dbytes)return 0;
    /* memmove tolerates the default scanout surface being re-presented. */
    for(uint32_t y=0;y<h;++y)
        memmove((uint8_t*)dst+((uint64_t)dy+y)*ds+(uint64_t)dx*4,
                (const uint8_t*)src+soff+((uint64_t)sy+y)*ss+(uint64_t)sx*4,(size_t)w*4);
    return 1;
}

/* B1L-0348: the layers are copied in order without blending, so the last one shown is all that is seen when it covers
 * the panel. Returns that last layer (-1: none): updated, not disabled, with a surface. */
static inline int b2l_top_layer(uint32_t updated,uint32_t disabled,const uint32_t *ids,uint32_t count)
{
    for(int i=(int)count-1;i>=0;--i)
        if((updated&~disabled&(1u<<i))&&ids[i])return i;
    return -1;
}

/* B1L-0348: whether copying this layer would replace the whole panel, unmoved (b2l_blit clips what is larger). */
static inline int b2l_covers(uint32_t dw,uint32_t dh,uint32_t ds,uint32_t sw,uint32_t sh,uint32_t ss,
    int32_t sx,int32_t sy,int32_t dx,int32_t dy,int32_t w,int32_t h)
{
    return !sx&&!sy&&!dx&&!dy&&sw==dw&&sh==dh&&ss>=ds&&!(ss&3)&&
       (w<=0||(uint32_t)w>=dw)&&(h<=0||(uint32_t)h>=dh);
}

/* Logical canvas lw x lh presented on the dw x dh panel (nearest neighbour).
 * Rectangles are in logical coordinates; the same bounds rules as b2l_blit
 * apply to the real source and panel allocations. */
static inline int b2l_blit_scaled(void *dst,uint64_t dbytes,uint32_t dw,uint32_t dh,uint32_t ds,
    const void *src,uint64_t sbytes,uint32_t sw,uint32_t sh,uint32_t ss,uint32_t soff,
    uint32_t lw,uint32_t lh,int32_t sx,int32_t sy,int32_t dx,int32_t dy,int32_t width,int32_t height)
{
    if(!dst||!src||!dw||!dh||!sw||!sh||!lw||!lh||dw>16384||dh>16384||sw>16384||sh>16384||
       lw>dw||lh>dh||ds<dw*4||ss<sw*4||ds>65536||ss>65536||sx<0||sy<0||dx<0||dy<0||
       (uint32_t)sx>=sw||(uint32_t)sy>=sh||(uint32_t)dx>=lw||(uint32_t)dy>=lh||
       width<=0||height<=0)return 0;
    uint32_t w=(uint32_t)width,h=(uint32_t)height;
    if(w>sw-(uint32_t)sx)w=sw-(uint32_t)sx;
    if(w>lw-(uint32_t)dx)w=lw-(uint32_t)dx;
    if(h>sh-(uint32_t)sy)h=sh-(uint32_t)sy;
    if(h>lh-(uint32_t)dy)h=lh-(uint32_t)dy;
    uint64_t end_s=(uint64_t)soff+((uint64_t)sy+h-1)*ss+((uint64_t)sx+w)*4;
    if(end_s>sbytes)return 0;
    uint32_t px0=(uint32_t)((uint64_t)dx*dw/lw),py0=(uint32_t)((uint64_t)dy*dh/lh);
    uint32_t px1=(uint32_t)(((uint64_t)(dx+w)*dw+lw-1)/lw),py1=(uint32_t)(((uint64_t)(dy+h)*dh+lh-1)/lh);
    if(px1>dw)px1=dw;
    if(py1>dh)py1=dh;
    if(px0>=px1||py0>=py1)return 0;
    if(((uint64_t)py1-1)*ds+(uint64_t)px1*4>dbytes)return 0;
    const uint8_t *base=(const uint8_t*)src+soff;
    uint32_t previous=UINT32_MAX;
    for(uint32_t py=py0;py<py1;++py){
        uint32_t ly=(uint32_t)((uint64_t)py*lh/dh);
        if(ly<(uint32_t)dy)ly=(uint32_t)dy;
        if(ly>(uint32_t)dy+h-1)ly=(uint32_t)dy+h-1;
        uint32_t row=(uint32_t)sy+ly-(uint32_t)dy;
        uint32_t *out=(uint32_t*)((uint8_t*)dst+(uint64_t)py*ds)+px0;
        if(row==previous){
            /* Vertical upscaling repeats rows: reuse the panel row above. */
            memmove(out,(const uint8_t*)out-ds,(size_t)(px1-px0)*4);
            continue;
        }
        previous=row;
        const uint32_t *in=(const uint32_t*)(base+(uint64_t)row*ss);
        uint64_t acc=(uint64_t)px0*lw;
        uint32_t lx=(uint32_t)(acc/dw),rem=(uint32_t)(acc%dw);
        for(uint32_t px=px0;px<px1;++px){
            uint32_t c=lx<(uint32_t)dx?(uint32_t)dx:(lx>(uint32_t)dx+w-1?(uint32_t)dx+w-1:lx);
            out[px-px0]=in[(uint32_t)sx+c-(uint32_t)dx];
            rem+=lw;
            while(rem>=dw){rem-=dw;++lx;}
        }
    }
    return 1;
}
#endif
