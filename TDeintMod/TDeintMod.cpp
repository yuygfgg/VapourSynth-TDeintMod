/*
**   VapourSynth port by HolyWu
**
**                TDeinterlace v1.1 for Avisynth 2.5.x
**
**   TDeinterlace is a bi-directionally motion adaptive deinterlacer.
**   It also uses a couple modified forms of ela interpolation which
**   help to reduce "jaggy" edges in places where interpolation must
**   be used. TDeinterlace currently supports YV12 and YUY2 colorspaces.
**
**   Copyright (C) 2004-2007 Kevin Stone
**
**   This program is free software; you can redistribute it and/or modify
**   it under the terms of the GNU General Public License as published by
**   the Free Software Foundation; either version 2 of the License, or
**   (at your option) any later version.
**
**   This program is distributed in the hope that it will be useful,
**   but WITHOUT ANY WARRANTY; without even the implied warranty of
**   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**   GNU General Public License for more details.
**
**   You should have received a copy of the GNU General Public License
**   along with this program; if not, write to the Free Software
**   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <algorithm>
#include <vector>
#include "VapourSynth.h"
#include "VSHelper.h"

struct TDeintModData {
    VSNodeRef * node, * node2, * mask, * clip2, * edeint;
    VSVideoInfo vi;
    const VSVideoInfo * viSaved;
    int order, field, mode, length, mtype, ttype, mtqL, mthL, mtqC, mthC, nt, minthresh, maxthresh, cstr, cthresh, blockx, blocky, MI, metric;
    bool full, chroma;
    int * offplut[3], * offnlut[3], mlut[256], gvlut[60];
    std::vector<int> vlut, tmmlut16;
    int xhalf, yhalf, xshift, yshift, cthresh6, cthreshsq;
    bool useClip2;
};

static inline bool isPowerOf2(int i) {
    return i && !(i & (i - 1));
}

static inline void threshMask(const VSFrameRef * src, VSFrameRef * dst, const TDeintModData * d, const VSAPI * vsapi) {
    for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
        const int width = vsapi->getFrameWidth(src, plane);
        const int height = vsapi->getFrameHeight(src, plane);
        const int stride = vsapi->getStride(src, plane);
        const uint8_t * srcp = vsapi->getReadPtr(src, plane);
        uint8_t * dstp0 = vsapi->getWritePtr(dst, plane);
        uint8_t * dstp1 = dstp0 + stride * height;
        if (plane == 0 && d->mtqL > -1 && d->mthL > -1) {
            memset(dstp0, d->mtqL, stride * height);
            memset(dstp1, d->mthL, stride * height);
            continue;
        } else if (plane > 0 && d->mtqC > -1 && d->mthC > -1) {
            memset(dstp0, d->mtqC, stride * height);
            memset(dstp1, d->mthC, stride * height);
            continue;
        }
        const int hs = plane ? d->vi.format->subSamplingW : 0;
        const int vs = plane ? 1 << d->vi.format->subSamplingH : 1;
        const int vss = 1 << (vs - 1);
        const int * offpt = d->offplut[plane];
        const int * offnt = d->offnlut[plane];
        if (d->ttype == 0) { // 4 neighbors - compensated
            for (int y = 0; y < height; y++) {
                const uint8_t * srcpp = srcp - (y == 0 ? -stride : stride);
                const uint8_t * srcpn = srcp + (y == height - 1 ? -stride : stride);
                for (int x = 0; x < width; x++) {
                    const int offp = offpt[x];
                    const int offn = offnt[x];
                    int min0 = 256, max0 = -1;
                    int min1 = 256, max1 = -1;
                    if (srcpp[x] < min0)
                        min0 = srcpp[x];
                    if (srcpp[x] > max0)
                        max0 = srcpp[x];
                    if (srcp[x - offp] < min1)
                        min1 = srcp[x - offp];
                    if (srcp[x - offp] > max1)
                        max1 = srcp[x - offp];
                    if (srcp[x + offn] < min1)
                        min1 = srcp[x + offn];
                    if (srcp[x + offn] > max1)
                        max1 = srcp[x + offn];
                    if (srcpn[x] < min0)
                        min0 = srcpn[x];
                    if (srcpn[x] > max0)
                        max0 = srcpn[x];
                    const int atv = std::max((std::abs(srcp[x] - min0) + vss) >> vs, (std::abs(srcp[x] - max0) + vss) >> vs);
                    const int ath = std::max((std::abs(srcp[x] - min1) + hs) >> hs, (std::abs(srcp[x] - max1) + hs) >> hs);
                    const int atmax = std::max(atv, ath);
                    dstp0[x] = (atmax + 2) >> 2;
                    dstp1[x] = (atmax + 1) >> 1;
                }
                srcp += stride;
                dstp0 += stride;
                dstp1 += stride;
            }
        } else if (d->ttype == 1) { // 8 neighbors - compensated
            for (int y = 0; y < height; y++) {
                const uint8_t * srcpp = srcp - (y == 0 ? -stride : stride);
                const uint8_t * srcpn = srcp + (y == height - 1 ? -stride : stride);
                for (int x = 0; x < width; x++) {
                    const int offp = offpt[x];
                    const int offn = offnt[x];
                    int min0 = 256, max0 = -1;
                    int min1 = 256, max1 = -1;
                    if (srcpp[x - offp] < min0)
                        min0 = srcpp[x - offp];
                    if (srcpp[x - offp] > max0)
                        max0 = srcpp[x - offp];
                    if (srcpp[x] < min0)
                        min0 = srcpp[x];
                    if (srcpp[x] > max0)
                        max0 = srcpp[x];
                    if (srcpp[x + offn] < min0)
                        min0 = srcpp[x + offn];
                    if (srcpp[x + offn] > max0)
                        max0 = srcpp[x + offn];
                    if (srcp[x - offp] < min1)
                        min1 = srcp[x - offp];
                    if (srcp[x - offp] > max1)
                        max1 = srcp[x - offp];
                    if (srcp[x + offn] < min1)
                        min1 = srcp[x + offn];
                    if (srcp[x + offn] > max1)
                        max1 = srcp[x + offn];
                    if (srcpn[x - offp] < min0)
                        min0 = srcpn[x - offp];
                    if (srcpn[x - offp] > max0)
                        max0 = srcpn[x - offp];
                    if (srcpn[x] < min0)
                        min0 = srcpn[x];
                    if (srcpn[x] > max0)
                        max0 = srcpn[x];
                    if (srcpn[x + offn] < min0)
                        min0 = srcpn[x + offn];
                    if (srcpn[x + offn] > max0)
                        max0 = srcpn[x + offn];
                    const int atv = std::max((std::abs(srcp[x] - min0) + vss) >> vs, (std::abs(srcp[x] - max0) + vss) >> vs);
                    const int ath = std::max((std::abs(srcp[x] - min1) + hs) >> hs, (std::abs(srcp[x] - max1) + hs) >> hs);
                    const int atmax = std::max(atv, ath);
                    dstp0[x] = (atmax + 2) >> 2;
                    dstp1[x] = (atmax + 1) >> 1;
                }
                srcp += stride;
                dstp0 += stride;
                dstp1 += stride;
            }
        } else if (d->ttype == 2) { // 4 neighbors - not compensated
            for (int y = 0; y < height; y++) {
                const uint8_t * srcpp = srcp - (y == 0 ? -stride : stride);
                const uint8_t * srcpn = srcp + (y == height - 1 ? -stride : stride);
                for (int x = 0; x < width; x++) {
                    const int offp = offpt[x];
                    const int offn = offnt[x];
                    int min0 = 256, max0 = -1;
                    if (srcpp[x] < min0)
                        min0 = srcpp[x];
                    if (srcpp[x] > max0)
                        max0 = srcpp[x];
                    if (srcp[x - offp] < min0)
                        min0 = srcp[x - offp];
                    if (srcp[x - offp] > max0)
                        max0 = srcp[x - offp];
                    if (srcp[x + offn] < min0)
                        min0 = srcp[x + offn];
                    if (srcp[x + offn] > max0)
                        max0 = srcp[x + offn];
                    if (srcpn[x] < min0)
                        min0 = srcpn[x];
                    if (srcpn[x] > max0)
                        max0 = srcpn[x];
                    const int at = std::max(std::abs(srcp[x] - min0), std::abs(srcp[x] - max0));
                    dstp0[x] = (at + 2) >> 2;
                    dstp1[x] = (at + 1) >> 1;
                }
                srcp += stride;
                dstp0 += stride;
                dstp1 += stride;
            }
        } else if (d->ttype == 3) { // 8 neighbors - not compensated
            for (int y = 0; y < height; y++) {
                const uint8_t * srcpp = srcp - (y == 0 ? -stride : stride);
                const uint8_t * srcpn = srcp + (y == height - 1 ? -stride : stride);
                for (int x = 0; x < width; x++) {
                    const int offp = offpt[x];
                    const int offn = offnt[x];
                    int min0 = 256, max0 = -1;
                    if (srcpp[x - offp] < min0)
                        min0 = srcpp[x - offp];
                    if (srcpp[x - offp] > max0)
                        max0 = srcpp[x - offp];
                    if (srcpp[x] < min0)
                        min0 = srcpp[x];
                    if (srcpp[x] > max0)
                        max0 = srcpp[x];
                    if (srcpp[x + offn] < min0)
                        min0 = srcpp[x + offn];
                    if (srcpp[x + offn] > max0)
                        max0 = srcpp[x + offn];
                    if (srcp[x - offp] < min0)
                        min0 = srcp[x - offp];
                    if (srcp[x - offp] > max0)
                        max0 = srcp[x - offp];
                    if (srcp[x + offn] < min0)
                        min0 = srcp[x + offn];
                    if (srcp[x + offn] > max0)
                        max0 = srcp[x + offn];
                    if (srcpn[x - offp] < min0)
                        min0 = srcpn[x - offp];
                    if (srcpn[x - offp] > max0)
                        max0 = srcpn[x - offp];
                    if (srcpn[x] < min0)
                        min0 = srcpn[x];
                    if (srcpn[x] > max0)
                        max0 = srcpn[x];
                    if (srcpn[x + offn] < min0)
                        min0 = srcpn[x + offn];
                    if (srcpn[x + offn] > max0)
                        max0 = srcpn[x + offn];
                    const int at = std::max(std::abs(srcp[x] - min0), std::abs(srcp[x] - max0));
                    dstp0[x] = (at + 2) >> 2;
                    dstp1[x] = (at + 1) >> 1;
                }
                srcp += stride;
                dstp0 += stride;
                dstp1 += stride;
            }
        } else if (d->ttype == 4) { // 4 neighbors - not compensated (range)
            for (int y = 0; y < height; y++) {
                const uint8_t * srcpp = srcp - (y == 0 ? -stride : stride);
                const uint8_t * srcpn = srcp + (y == height - 1 ? -stride : stride);
                for (int x = 0; x < width; x++) {
                    const int offp = offpt[x];
                    const int offn = offnt[x];
                    int min0 = 256, max0 = -1;
                    if (srcpp[x] < min0)
                        min0 = srcpp[x];
                    if (srcpp[x] > max0)
                        max0 = srcpp[x];
                    if (srcp[x - offp] < min0)
                        min0 = srcp[x - offp];
                    if (srcp[x - offp] > max0)
                        max0 = srcp[x - offp];
                    if (srcp[x] < min0)
                        min0 = srcp[x];
                    if (srcp[x] > max0)
                        max0 = srcp[x];
                    if (srcp[x + offn] < min0)
                        min0 = srcp[x + offn];
                    if (srcp[x + offn] > max0)
                        max0 = srcp[x + offn];
                    if (srcpn[x] < min0)
                        min0 = srcpn[x];
                    if (srcpn[x] > max0)
                        max0 = srcpn[x];
                    const int at = max0 - min0;
                    dstp0[x] = (at + 2) >> 2;
                    dstp1[x] = (at + 1) >> 1;
                }
                srcp += stride;
                dstp0 += stride;
                dstp1 += stride;
            }
        } else if (d->ttype == 5) { // 8 neighbors - not compensated (range)
            for (int y = 0; y < height; y++) {
                const uint8_t * srcpp = srcp - (y == 0 ? -stride : stride);
                const uint8_t * srcpn = srcp + (y == height - 1 ? -stride : stride);
                for (int x = 0; x < width; x++) {
                    const int offp = offpt[x];
                    const int offn = offnt[x];
                    int min0 = 256, max0 = -1;
                    if (srcpp[x - offp] < min0)
                        min0 = srcpp[x - offp];
                    if (srcpp[x - offp] > max0)
                        max0 = srcpp[x - offp];
                    if (srcpp[x] < min0)
                        min0 = srcpp[x];
                    if (srcpp[x] > max0)
                        max0 = srcpp[x];
                    if (srcpp[x + offn] < min0)
                        min0 = srcpp[x + offn];
                    if (srcpp[x + offn] > max0)
                        max0 = srcpp[x + offn];
                    if (srcp[x - offp] < min0)
                        min0 = srcp[x - offp];
                    if (srcp[x - offp] > max0)
                        max0 = srcp[x - offp];
                    if (srcp[x] < min0)
                        min0 = srcp[x];
                    if (srcp[x] > max0)
                        max0 = srcp[x];
                    if (srcp[x + offn] < min0)
                        min0 = srcp[x + offn];
                    if (srcp[x + offn] > max0)
                        max0 = srcp[x + offn];
                    if (srcpn[x - offp] < min0)
                        min0 = srcpn[x - offp];
                    if (srcpn[x - offp] > max0)
                        max0 = srcpn[x - offp];
                    if (srcpn[x] < min0)
                        min0 = srcpn[x];
                    if (srcpn[x] > max0)
                        max0 = srcpn[x];
                    if (srcpn[x + offn] < min0)
                        min0 = srcpn[x + offn];
                    if (srcpn[x + offn] > max0)
                        max0 = srcpn[x + offn];
                    const int at = max0 - min0;
                    dstp0[x] = (at + 2) >> 2;
                    dstp1[x] = (at + 1) >> 1;
                }
                srcp += stride;
                dstp0 += stride;
                dstp1 += stride;
            }
        }
        if (plane == 0 && d->mtqL > -1)
            memset(vsapi->getWritePtr(dst, plane), d->mtqL, stride * height);
        else if (plane == 0 && d->mthL > -1)
            memset(vsapi->getWritePtr(dst, plane) + stride * height, d->mthL, stride * height);
        else if (plane > 0 && d->mtqC > -1)
            memset(vsapi->getWritePtr(dst, plane), d->mtqC, stride * height);
        else if (plane > 0 && d->mthC > -1)
            memset(vsapi->getWritePtr(dst, plane) + stride * height, d->mthC, stride * height);
    }
}

static void motionMask(const VSFrameRef * src1, const VSFrameRef * msk1, const VSFrameRef * src2, const VSFrameRef * msk2, VSFrameRef * dst, const TDeintModData * d, const VSAPI * vsapi) {
    for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
        const int width = vsapi->getFrameWidth(src1, plane);
        const int height = vsapi->getFrameHeight(src1, plane);
        const int stride = vsapi->getStride(src1, plane);
        const uint8_t * srcp1 = vsapi->getReadPtr(src1, plane);
        const uint8_t * srcp2 = vsapi->getReadPtr(src2, plane);
        const uint8_t * mskp1q = vsapi->getReadPtr(msk1, plane);
        const uint8_t * mskp1h = mskp1q + stride * height;
        const uint8_t * mskp2q = vsapi->getReadPtr(msk2, plane);
        const uint8_t * mskp2h = mskp2q + stride * height;
        uint8_t * dstpq = vsapi->getWritePtr(dst, plane);
        uint8_t * dstph = dstpq + stride * height;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                const int diff = std::abs(srcp1[x] - srcp2[x]);
                const int threshq = std::min(mskp1q[x], mskp2q[x]);
                if (diff <= d->mlut[threshq])
                    dstpq[x] = 255;
                else
                    dstpq[x] = 0;
                const int threshh = std::min(mskp1h[x], mskp2h[x]);
                if (diff <= d->mlut[threshh])
                    dstph[x] = 255;
                else
                    dstph[x] = 0;
            }
            srcp1 += stride;
            srcp2 += stride;
            mskp1q += stride;
            mskp1h += stride;
            mskp2q += stride;
            mskp2h += stride;
            dstpq += stride;
            dstph += stride;
        }
    }
}

static inline void andMasks(const VSFrameRef * src1, const VSFrameRef * src2, VSFrameRef * dst, const TDeintModData * d, const VSAPI * vsapi) {
    for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
        const int width = vsapi->getFrameWidth(src1, plane);
        const int height = vsapi->getFrameHeight(src1, plane);
        const int stride = vsapi->getStride(src1, plane);
        const uint8_t * srcp1 = vsapi->getReadPtr(src1, plane);
        const uint8_t * srcp2 = vsapi->getReadPtr(src2, plane);
        uint8_t * dstp = vsapi->getWritePtr(dst, plane);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++)
                dstp[x] &= (srcp1[x] & srcp2[x]);
            srcp1 += stride;
            srcp2 += stride;
            dstp += stride;
        }
    }
}

static inline void combineMasks(const VSFrameRef * src, VSFrameRef * dst, const TDeintModData * d, const VSAPI * vsapi) {
    for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
        const int width = vsapi->getFrameWidth(src, plane);
        const int height = vsapi->getFrameHeight(dst, plane);
        const int stride = vsapi->getStride(src, plane);
        const uint8_t * srcp0 = vsapi->getReadPtr(src, plane);
        const uint8_t * srcp1 = srcp0 + stride * height;
        uint8_t * dstp = vsapi->getWritePtr(dst, plane);
        memcpy(dstp, srcp0, stride * height);
        const int * offpt = d->offplut[plane];
        const int * offnt = d->offnlut[plane];
        for (int y = 0; y < height; y++) {
            const uint8_t * srcpp0 = srcp0 - (y == 0 ? -stride : stride);
            const uint8_t * srcpn0 = srcp0 + (y == height - 1 ? -stride : stride);
            for (int x = 0; x < width; x++) {
                if (srcp0[x] || !srcp1[x])
                    continue;
                const int offp = offpt[x];
                const int offn = offnt[x];
                int count = 0;
                if (srcpp0[x - offp])
                    count++;
                if (srcpp0[x])
                    count++;
                if (srcpp0[x + offn])
                    count++;
                if (srcp0[x - offp])
                    count++;
                if (srcp0[x + offn])
                    count++;
                if (srcpn0[x - offp])
                    count++;
                if (srcpn0[x])
                    count++;
                if (srcpn0[x + offn])
                    count++;
                if (count >= d->cstr)
                    dstp[x] = 255;
            }
            srcp0 += stride;
            srcp1 += stride;
            dstp += stride;
        }
    }
}

static inline bool checkCombed(const VSFrameRef * src, VSFrameRef * cmask, int * VS_RESTRICT cArray, const TDeintModData * d, const VSAPI * vsapi) {
    for (int plane = 0; plane < (d->chroma ? 3 : 1); plane++) {
        const int width = vsapi->getFrameWidth(src, plane);
        const int height = vsapi->getFrameHeight(src, plane);
        const int stride = vsapi->getStride(src, plane);
        const uint8_t * srcp = vsapi->getReadPtr(src, plane);
        const uint8_t * srcpp = srcp - stride;
        const uint8_t * srcppp = srcpp - stride;
        const uint8_t * srcpn = srcp + stride;
        const uint8_t * srcpnn = srcpn + stride;
        uint8_t * cmkp = vsapi->getWritePtr(cmask, plane);
        if (d->cthresh < 0) {
            memset(cmkp, 255, stride * height);
            continue;
        }
        memset(cmkp, 0, stride * height);
        if (d->metric == 0) {
            for (int x = 0; x < width; x++) {
                const int sFirst = srcp[x] - srcpn[x];
                if ((sFirst > d->cthresh || sFirst < -d->cthresh) && std::abs(srcpnn[x] + (srcp[x] << 2) + srcpnn[x] - (3 * (srcpn[x] + srcpn[x]))) > d->cthresh6)
                    cmkp[x] = 0xFF;
            }
            srcppp += stride;
            srcpp += stride;
            srcp += stride;
            srcpn += stride;
            srcpnn += stride;
            cmkp += stride;
            for (int x = 0; x < width; x++) {
                const int sFirst = srcp[x] - srcpp[x];
                const int sSecond = srcp[x] - srcpn[x];
                if (((sFirst > d->cthresh && sSecond > d->cthresh) || (sFirst < -d->cthresh && sSecond < -d->cthresh)) &&
                    std::abs(srcpnn[x] + (srcp[x] << 2) + srcpnn[x] - (3 * (srcpp[x] + srcpn[x]))) > d->cthresh6)
                    cmkp[x] = 0xFF;
            }
            srcppp += stride;
            srcpp += stride;
            srcp += stride;
            srcpn += stride;
            srcpnn += stride;
            cmkp += stride;
            for (int y = 2; y < height - 2; y++) {
                for (int x = 0; x < width; x++) {
                    const int sFirst = srcp[x] - srcpp[x];
                    const int sSecond = srcp[x] - srcpn[x];
                    if (((sFirst > d->cthresh && sSecond > d->cthresh) || (sFirst < -d->cthresh && sSecond < -d->cthresh)) &&
                        std::abs(srcppp[x] + (srcp[x] << 2) + srcpnn[x] - (3 * (srcpp[x] + srcpn[x]))) > d->cthresh6)
                        cmkp[x] = 0xFF;
                }
                srcppp += stride;
                srcpp += stride;
                srcp += stride;
                srcpn += stride;
                srcpnn += stride;
                cmkp += stride;
            }
            for (int x = 0; x < width; x++) {
                const int sFirst = srcp[x] - srcpp[x];
                const int sSecond = srcp[x] - srcpn[x];
                if (((sFirst > d->cthresh && sSecond > d->cthresh) || (sFirst < -d->cthresh && sSecond < -d->cthresh)) &&
                    std::abs(srcppp[x] + (srcp[x] << 2) + srcppp[x] - (3 * (srcpp[x] + srcpn[x]))) > d->cthresh6)
                    cmkp[x] = 0xFF;
            }
            srcppp += stride;
            srcpp += stride;
            srcp += stride;
            srcpn += stride;
            srcpnn += stride;
            cmkp += stride;
            for (int x = 0; x < width; x++) {
                const int sFirst = srcp[x] - srcpp[x];
                if ((sFirst > d->cthresh || sFirst < -d->cthresh) && std::abs(srcppp[x] + (srcp[x] << 2) + srcppp[x] - (3 * (srcpp[x] + srcpp[x]))) > d->cthresh6)
                    cmkp[x] = 0xFF;
            }
        } else {
            for (int x = 0; x < width; x++) {
                if ((srcp[x] - srcpn[x]) * (srcp[x] - srcpn[x]) > d->cthreshsq)
                    cmkp[x] = 0xFF;
            }
            srcpp += stride;
            srcp += stride;
            srcpn += stride;
            cmkp += stride;
            for (int y = 1; y < height - 1; y++) {
                for (int x = 0; x < width; x++) {
                    if ((srcp[x] - srcpp[x]) * (srcp[x] - srcpn[x]) > d->cthreshsq)
                        cmkp[x] = 0xFF;
                }
                srcpp += stride;
                srcp += stride;
                srcpn += stride;
                cmkp += stride;
            }
            for (int x = 0; x < width; x++) {
                if ((srcp[x] - srcpp[x]) * (srcp[x] - srcpp[x]) > d->cthreshsq)
                    cmkp[x] = 0xFF;
            }
        }
    }
    if (d->chroma) {
        const int width = vsapi->getFrameWidth(cmask, 2);
        const int height = vsapi->getFrameHeight(cmask, 2);
        const int stride = vsapi->getStride(cmask, 0) * 2;
        const int strideUV = vsapi->getStride(cmask, 2);
        uint8_t * cmkp = vsapi->getWritePtr(cmask, 0);
        const uint8_t * cmkpU = vsapi->getReadPtr(cmask, 1);
        const uint8_t * cmkpV = vsapi->getReadPtr(cmask, 2);
        uint8_t * cmkpp = cmkp - (stride / 2);
        uint8_t * cmkpn = cmkp + (stride / 2);
        uint8_t * cmkpnn = cmkpn + (stride / 2);
        const uint8_t * cmkppU = cmkpU - strideUV;
        const uint8_t * cmkpnU = cmkpU + strideUV;
        const uint8_t * cmkppV = cmkpV - strideUV;
        const uint8_t * cmkpnV = cmkpV + strideUV;
        for (int y = 1; y < height - 1; y++) {
            cmkpp += stride;
            cmkp += stride;
            cmkpn += stride;
            cmkpnn += stride;
            cmkppU += strideUV;
            cmkpU += strideUV;
            cmkpnU += strideUV;
            cmkppV += strideUV;
            cmkpV += strideUV;
            cmkpnV += strideUV;
            for (int x = 1; x < width - 1; x++) {
                if ((cmkpU[x] == 0xFF && (cmkpU[x - 1] == 0xFF || cmkpU[x + 1] == 0xFF ||
                     cmkppU[x - 1] == 0xFF || cmkppU[x] == 0xFF || cmkppU[x + 1] == 0xFF ||
                     cmkpnU[x - 1] == 0xFF || cmkpnU[x] == 0xFF || cmkpnU[x + 1] == 0xFF)) ||
                    (cmkpV[x] == 0xFF && (cmkpV[x - 1] == 0xFF || cmkpV[x + 1] == 0xFF ||
                     cmkppV[x - 1] == 0xFF || cmkppV[x] == 0xFF || cmkppV[x + 1] == 0xFF ||
                     cmkpnV[x - 1] == 0xFF || cmkpnV[x] == 0xFF || cmkpnV[x + 1] == 0xFF))) {
                    ((uint16_t *)cmkp)[x] = 0xFFFF;
                    ((uint16_t *)cmkpn)[x] = 0xFFFF;
                    if (y & 1)
                        ((uint16_t *)cmkpp)[x] = 0xFFFF;
                    else
                        ((uint16_t *)cmkpnn)[x] = 0xFFFF;
                }
            }
        }
    }
    const int width = vsapi->getFrameWidth(cmask, 0);
    const int height = vsapi->getFrameHeight(cmask, 0);
    const int stride = vsapi->getStride(cmask, 0);
    const uint8_t * cmkp = vsapi->getReadPtr(cmask, 0) + stride;
    const uint8_t * cmkpp = cmkp - stride;
    const uint8_t * cmkpn = cmkp + stride;
    const int xBlocks = ((width + d->xhalf) >> d->xshift) + 1;
    const int xBlocks4 = xBlocks * 4;
    const int yBlocks = ((height + d->yhalf) >> d->yshift) + 1;
    const int arraySize = (xBlocks * yBlocks) * 4;
    memset(cArray, 0, arraySize * sizeof(int));
    const int widtha = (width >> (d->xshift - 1)) << (d->xshift - 1);
    int heighta = (height >> (d->yshift - 1)) << (d->yshift - 1);
    if (heighta == height)
        heighta = height - d->yhalf;
    for (int y = 1; y < d->yhalf; y++) {
        const int temp1 = (y >> d->yshift) * xBlocks4;
        const int temp2 = ((y + d->yhalf) >> d->yshift) * xBlocks4;
        for (int x = 0; x < width; x++) {
            if (cmkpp[x] == 0xFF && cmkp[x] == 0xFF && cmkpn[x] == 0xFF) {
                const int box1 = (x >> d->xshift) << 2;
                const int box2 = ((x + d->xhalf) >> d->xshift) << 2;
                ++cArray[temp1 + box1];
                ++cArray[temp1 + box2 + 1];
                ++cArray[temp2 + box1 + 2];
                ++cArray[temp2 + box2 + 3];
            }
        }
        cmkpp += stride;
        cmkp += stride;
        cmkpn += stride;
    }
    for (int y = d->yhalf; y < heighta; y += d->yhalf) {
        const int temp1 = (y >> d->yshift) * xBlocks4;
        const int temp2 = ((y + d->yhalf) >> d->yshift) * xBlocks4;
        for (int x = 0; x < widtha; x += d->xhalf) {
            const uint8_t * cmkppT = cmkpp;
            const uint8_t * cmkpT = cmkp;
            const uint8_t * cmkpnT = cmkpn;
            int sum = 0;
            for (int u = 0; u < d->yhalf; u++) {
                for (int v = 0; v < d->xhalf; v++) {
                    if (cmkppT[x + v] == 0xFF && cmkpT[x + v] == 0xFF && cmkpnT[x + v] == 0xFF)
                        sum++;
                }
                cmkppT += stride;
                cmkpT += stride;
                cmkpnT += stride;
            }
            if (sum) {
                const int box1 = (x >> d->xshift) << 2;
                const int box2 = ((x + d->xhalf) >> d->xshift) << 2;
                cArray[temp1 + box1] += sum;
                cArray[temp1 + box2 + 1] += sum;
                cArray[temp2 + box1 + 2] += sum;
                cArray[temp2 + box2 + 3] += sum;
            }
        }
        for (int x = widtha; x < width; x++) {
            const uint8_t * cmkppT = cmkpp;
            const uint8_t * cmkpT = cmkp;
            const uint8_t * cmkpnT = cmkpn;
            int sum = 0;
            for (int u = 0; u < d->yhalf; u++) {
                if (cmkppT[x] == 0xFF && cmkpT[x] == 0xFF && cmkpnT[x] == 0xFF)
                    sum++;
                cmkppT += stride;
                cmkpT += stride;
                cmkpnT += stride;
            }
            if (sum) {
                const int box1 = (x >> d->xshift) << 2;
                const int box2 = ((x + d->xhalf) >> d->xshift) << 2;
                cArray[temp1 + box1] += sum;
                cArray[temp1 + box2 + 1] += sum;
                cArray[temp2 + box1 + 2] += sum;
                cArray[temp2 + box2 + 3] += sum;
            }
        }
        cmkpp += stride * d->yhalf;
        cmkp += stride * d->yhalf;
        cmkpn += stride * d->yhalf;
    }
    for (int y = heighta; y < height - 1; y++) {
        const int temp1 = (y >> d->yshift) * xBlocks4;
        const int temp2 = ((y + d->yhalf) >> d->yshift) * xBlocks4;
        for (int x = 0; x < width; x++) {
            if (cmkpp[x] == 0xFF && cmkp[x] == 0xFF && cmkpn[x] == 0xFF) {
                const int box1 = (x >> d->xshift) << 2;
                const int box2 = ((x + d->xhalf) >> d->xshift) << 2;
                ++cArray[temp1 + box1];
                ++cArray[temp1 + box2 + 1];
                ++cArray[temp2 + box1 + 2];
                ++cArray[temp2 + box2 + 3];
            }
        }
        cmkpp += stride;
        cmkp += stride;
        cmkpn += stride;
    }
    int MIC = 0;
    for (int x = 0; x < arraySize; x++) {
        if (cArray[x] > MIC)
            MIC = cArray[x];
    }
    if (MIC > d->MI)
        return true;
    else
        return false;
}

static inline void eDeint(VSFrameRef * dst, const VSFrameRef * mask, const VSFrameRef * prv, const VSFrameRef * src, const VSFrameRef * nxt, const VSFrameRef * efrm,
                          const TDeintModData * d, const VSAPI * vsapi) {
    for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
        const int width = vsapi->getFrameWidth(src, plane);
        const int height = vsapi->getFrameHeight(src, plane);
        const int stride = vsapi->getStride(src, plane);
        const uint8_t * prvp = vsapi->getReadPtr(prv, plane);
        const uint8_t * srcp = vsapi->getReadPtr(src, plane);
        const uint8_t * nxtp = vsapi->getReadPtr(nxt, plane);
        const uint8_t * maskp = vsapi->getReadPtr(mask, plane);
        const uint8_t * efrmp = vsapi->getReadPtr(efrm, plane);
        uint8_t * dstp = vsapi->getWritePtr(dst, plane);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (maskp[x] == 10)
                    dstp[x] = srcp[x];
                else if (maskp[x] == 20)
                    dstp[x] = prvp[x];
                else if (maskp[x] == 30)
                    dstp[x] = nxtp[x];
                else if (maskp[x] == 40)
                    dstp[x] = (srcp[x] + nxtp[x] + 1) >> 1;
                else if (maskp[x] == 50)
                    dstp[x] = (srcp[x] + prvp[x] + 1) >> 1;
                else if (maskp[x] == 70)
                    dstp[x] = (prvp[x] + (srcp[x] << 1) + nxtp[x] + 2) >> 2;
                else if (maskp[x] == 60)
                    dstp[x] = efrmp[x];
            }
            prvp += stride;
            srcp += stride;
            nxtp += stride;
            maskp += stride;
            efrmp += stride;
            dstp += stride;
        }
    }
}

static inline void cubicDeint(VSFrameRef * dst, const VSFrameRef * mask, const VSFrameRef * prv, const VSFrameRef * src, const VSFrameRef * nxt,
                              const TDeintModData * d, const VSAPI * vsapi) {
    for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
        const int width = vsapi->getFrameWidth(src, plane);
        const int height = vsapi->getFrameHeight(src, plane);
        const int stride = vsapi->getStride(src, plane);
        const uint8_t * prvp = vsapi->getReadPtr(prv, plane);
        const uint8_t * srcp = vsapi->getReadPtr(src, plane);
        const uint8_t * nxtp = vsapi->getReadPtr(nxt, plane);
        const uint8_t * maskp = vsapi->getReadPtr(mask, plane);
        uint8_t * dstp = vsapi->getWritePtr(dst, plane);
        const uint8_t * srcpp = srcp - stride;
        const uint8_t * srcppp = srcpp - stride * 2;
        const uint8_t * srcpn = srcp + stride;
        const uint8_t * srcpnn = srcpn + stride * 2;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (maskp[x] == 10)
                    dstp[x] = srcp[x];
                else if (maskp[x] == 20)
                    dstp[x] = prvp[x];
                else if (maskp[x] == 30)
                    dstp[x] = nxtp[x];
                else if (maskp[x] == 40)
                    dstp[x] = (srcp[x] + nxtp[x] + 1) >> 1;
                else if (maskp[x] == 50)
                    dstp[x] = (srcp[x] + prvp[x] + 1) >> 1;
                else if (maskp[x] == 70)
                    dstp[x] = (prvp[x] + (srcp[x] << 1) + nxtp[x] + 2) >> 2;
                else if (maskp[x] == 60) {
                    if (y == 0) {
                        dstp[x] = srcpn[x];
                    } else if (y == height - 1) {
                        dstp[x] = srcpp[x];
                    } else if (y < 3 || y > height - 4) {
                        dstp[x] = (srcpn[x] + srcpp[x] + 1) >> 1;
                    } else {
                        const int temp = (19 * (srcpp[x] + srcpn[x]) - 3 * (srcppp[x] + srcpnn[x]) + 16) >> 5;
                        dstp[x] = std::max(std::min(temp, 255), 0);
                    }
                }
            }
            prvp += stride;
            srcppp += stride;
            srcpp += stride;
            srcp += stride;
            srcpn += stride;
            srcpnn += stride;
            nxtp += stride;
            maskp += stride;
            dstp += stride;
        }
    }
}

static bool invokeCache(VSNodeRef ** node, VSMap * out, VSPlugin * stdPlugin, const VSAPI * vsapi) {
    VSMap * args = vsapi->createMap();
    vsapi->propSetNode(args, "clip", *node, paReplace);
    vsapi->freeNode(*node);
    VSMap * ret = vsapi->invoke(stdPlugin, "Cache", args);
    vsapi->freeMap(args);
    if (!vsapi->getError(ret)) {
        *node = vsapi->propGetNode(ret, "clip", 0, nullptr);
        vsapi->freeMap(ret);
        return true;
    } else {
        vsapi->setError(out, vsapi->getError(ret));
        vsapi->freeMap(ret);
        return false;
    }
}

static void VS_CC tdeintmodInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    TDeintModData * d = (TDeintModData *)*instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC tdeintmodCreateMMGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    const TDeintModData * d = (const TDeintModData *)*instanceData;

    if (activationReason == arInitial) {
        for (int i = 0; i < 3; i++) {
            if (n < d->vi.numFrames - i)
                vsapi->requestFrameFilter(n + i, d->node, frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef * src[3];
        VSFrameRef * msk[3][2];
        VSFrameRef * dst[] = {
            vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height * 2, nullptr, core), vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, nullptr, core)
        };

        for (int i = 0; i < 3; i++) {
            src[i] = vsapi->getFrameFilter(std::min(n + i, d->vi.numFrames - 1), d->node, frameCtx);
            msk[i][0] = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height * 2, nullptr, core);
            msk[i][1] = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height * 2, nullptr, core);
            threshMask(src[i], msk[i][0], d, vsapi);
        }
        for (int i = 0; i < 2; i++)
            motionMask(src[i], msk[i][0], src[i + 1], msk[i + 1][0], msk[i][1], d, vsapi);
        motionMask(src[0], msk[0][0], src[2], msk[2][0], dst[0], d, vsapi);
        andMasks(msk[0][1], msk[1][1], dst[0], d, vsapi);
        combineMasks(dst[0], dst[1], d, vsapi);

        for (int i = 0; i < 3; i++) {
            vsapi->freeFrame(src[i]);
            vsapi->freeFrame(msk[i][0]);
            vsapi->freeFrame(msk[i][1]);
        }
        vsapi->freeFrame(dst[0]);
        return dst[1];
    }

    return nullptr;
}

static const VSFrameRef *VS_CC tdeintmodBuildMMGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    const TDeintModData * d = (const TDeintModData *)*instanceData;

    if (activationReason == arInitial) {
        int fieldt = d->field;
        if (d->mode == 1) {
            fieldt = n & 1 ? 1 - d->order : d->order;
            n /= 2;
        }

        int tstart, tstop, bstart, bstop;
        if (fieldt == 1) {
            tstart = n - (d->length - 1) / 2;
            tstop = n + (d->length - 1) / 2 - 2;
            const int bn = d->order == 1 ? n - 1 : n;
            bstart = bn - (d->length - 2) / 2;
            bstop = bn + 1 + (d->length - 2) / 2 - 2;
        } else {
            const int tn = d->order == 0 ? n - 1 : n;
            tstart = tn - (d->length - 2) / 2;
            tstop = tn + 1 + (d->length - 2) / 2 - 2;
            bstart = n - (d->length - 1) / 2;
            bstop = n + (d->length - 1) / 2 - 2;
        }

        for (int i = tstart; i <= tstop; i++) {
            if (i >= 0 && i < d->viSaved->numFrames - 2)
                vsapi->requestFrameFilter(i, d->node, frameCtx);
        }
        for (int i = bstart; i <= bstop; i++) {
            if (i >= 0 && i < d->viSaved->numFrames - 2)
                vsapi->requestFrameFilter(i, d->node2, frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        // In the AVS version, it's dynamically allocated to the size of (length - 2) and length doesn't have an upper limit.
        // Since I set the upper limit of length to 60 in VS port now, I just declare the array to the maximum possible size instead of using dynamic memory allocation.
        VSFrameRef * srct[58];
        VSFrameRef * srcb[58];
        VSFrameRef * dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, nullptr, core);

        int fieldt = d->field;
        if (d->mode == 1) {
            fieldt = n & 1 ? 1 - d->order : d->order;
            n /= 2;
        }

        const int * tmmlut = d->tmmlut16.data() + d->order * 8 + fieldt * 4;
        int tmmlutf[64];
        for (int i = 0; i < 64; i++)
            tmmlutf[i] = tmmlut[d->vlut[i]];

        int tstart, tstop, bstart, bstop, ccount, ocount;
        VSFrameRef ** csrc, ** osrc;
        if (fieldt == 1) {
            tstart = n - (d->length - 1) / 2;
            tstop = n + (d->length - 1) / 2 - 2;
            const int bn = d->order == 1 ? n - 1 : n;
            bstart = bn - (d->length - 2) / 2;
            bstop = bn + 1 + (d->length - 2) / 2 - 2;
            ocount = tstop - tstart + 1;
            ccount = bstop - bstart + 1;
            osrc = srct;
            csrc = srcb;
        } else {
            const int tn = d->order == 0 ? n - 1 : n;
            tstart = tn - (d->length - 2) / 2;
            tstop = tn + 1 + (d->length - 2) / 2 - 2;
            bstart = n - (d->length - 1) / 2;
            bstop = n + (d->length - 1) / 2 - 2;
            ccount = tstop - tstart + 1;
            ocount = bstop - bstart + 1;
            csrc = srct;
            osrc = srcb;
        }

        for (int i = tstart; i <= tstop; i++) {
            if (i < 0 || i >= d->viSaved->numFrames - 2) {
                srct[i - tstart] = vsapi->newVideoFrame(d->viSaved->format, d->viSaved->width, d->viSaved->height, nullptr, core);
                for (int plane = 0; plane < d->viSaved->format->numPlanes; plane++)
                    memset(vsapi->getWritePtr(srct[i - tstart], plane), 0, vsapi->getStride(srct[i - tstart], plane) * vsapi->getFrameHeight(srct[i - tstart], plane));
            } else {
                const VSFrameRef * src = vsapi->getFrameFilter(i, d->node, frameCtx);
                srct[i - tstart] = vsapi->copyFrame(src, core);
                vsapi->freeFrame(src);
            }
        }
        for (int i = bstart; i <= bstop; i++) {
            if (i < 0 || i >= d->viSaved->numFrames - 2) {
                srcb[i - bstart] = vsapi->newVideoFrame(d->viSaved->format, d->viSaved->width, d->viSaved->height, nullptr, core);
                for (int plane = 0; plane < d->viSaved->format->numPlanes; plane++)
                    memset(vsapi->getWritePtr(srcb[i - bstart], plane), 0, vsapi->getStride(srcb[i - bstart], plane) * vsapi->getFrameHeight(srcb[i - bstart], plane));
            } else {
                const VSFrameRef * src = vsapi->getFrameFilter(i, d->node2, frameCtx);
                srcb[i - bstart] = vsapi->copyFrame(src, core);
                vsapi->freeFrame(src);
            }
        }

        int plut[2][119];     // Again, the size is (2 * length - 1) for the second dimension in the AVS version.
        uint8_t ** ptlut[3];
        for (int i = 0; i < 3; i++)
            ptlut[i] = new uint8_t *[i & 1 ? ccount : ocount];
        const int offo = d->length & 1 ? 0 : 1;
        const int offc = d->length & 1 ? 1 : 0;

        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            const int width = vsapi->getFrameWidth(dst, plane);
            const int height = vsapi->getFrameHeight(dst, plane);
            const int stride = vsapi->getStride(dst, plane);
            for (int i = 0; i < ccount; i++)
                ptlut[1][i] = vsapi->getWritePtr(csrc[i], plane);
            for (int i = 0; i < ocount; i++) {
                if (fieldt == 1) {
                    ptlut[0][i] = vsapi->getWritePtr(osrc[i], plane);
                    ptlut[2][i] = ptlut[0][i] + stride;
                } else {
                    ptlut[0][i] = ptlut[2][i] = vsapi->getWritePtr(osrc[i], plane);
                }
            }
            uint8_t * dstp = vsapi->getWritePtr(dst, plane);

            if (fieldt == 1) {
                for (int j = 0; j < height; j += 2)
                    memset(dstp + stride * j, 10, width);
                dstp += stride;
            } else {
                for (int j = 1; j < height; j += 2)
                    memset(dstp + stride * j, 10, width);
            }

            const int ct = ccount / 2;
            for (int y = fieldt; y < height; y += 2) {
                for (int x = 0; x < width; x++) {
                    if (!ptlut[1][ct - 2][x] && !ptlut[1][ct][x] && !ptlut[1][ct + 1][x]) {
                        dstp[x] = 60;
                        continue;
                    }
                    for (int j = 0; j < ccount; j++)
                        plut[0][j * 2 + offc] = plut[1][j * 2 + offc] = ptlut[1][j][x];
                    for (int j = 0; j < ocount; j++) {
                        plut[0][j * 2 + offo] = ptlut[0][j][x];
                        plut[1][j * 2 + offo] = ptlut[2][j][x];
                    }
                    int val = 0;
                    for (int i = 0; i < d->length; i++) {
                        for (int j = 0; j < d->length - 4; j++) {
                            if (!plut[0][i + j])
                                goto j1;
                        }
                        val |= d->gvlut[i] * 8;
                    j1:
                        for (int j = 0; j < d->length - 4; j++) {
                            if (!plut[1][i + j])
                                goto j2;
                        }
                        val |= d->gvlut[i];
                    j2:
                        if (d->vlut[val] == 2)
                            break;
                    }
                    dstp[x] = tmmlutf[val];
                }
                for (int i = 0; i < ccount; i++)
                    ptlut[1][i] += stride;
                for (int i = 0; i < ocount; i++) {
                    if (y != 0)
                        ptlut[0][i] += stride;
                    if (y != height - 3)
                        ptlut[2][i] += stride;
                }
                dstp += stride * 2;
            }
        }

        for (int i = tstart; i <= tstop; i++)
            vsapi->freeFrame(srct[i - tstart]);
        for (int i = bstart; i <= bstop; i++)
            vsapi->freeFrame(srcb[i - bstart]);
        for (int i = 0; i < 3; i++)
            delete[] ptlut[i];
        return dst;
    }

    return nullptr;
}

static const VSFrameRef *VS_CC tdeintmodGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    const TDeintModData * d = (const TDeintModData *)*instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->mask, frameCtx);
        if (d->edeint)
            vsapi->requestFrameFilter(n, d->edeint, frameCtx);

        if (d->mode == 1)
            n /= 2;

        if (n > 0)
            vsapi->requestFrameFilter(n - 1, !d->useClip2 ? d->node : d->clip2, frameCtx);
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        if (d->useClip2)
            vsapi->requestFrameFilter(n, d->clip2, frameCtx);
        if (n < d->viSaved->numFrames - 1)
            vsapi->requestFrameFilter(n + 1, !d->useClip2 ? d->node : d->clip2, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const int nSaved = n;
        if (d->mode == 1)
            n /= 2;

        const VSFrameRef * src = vsapi->getFrameFilter(n, d->node, frameCtx);

        if (d->mode == 0 && !d->full) {
            VSFrameRef * cmask = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, nullptr, core);
            int * cArray = vs_aligned_malloc<int>((((d->viSaved->width + d->xhalf) >> d->xshift) + 1) * (((d->viSaved->height + d->yhalf) >> d->yshift) + 1) * 4 * sizeof(int), 32);
            if (!cArray) {
                vsapi->setFilterError("TDeintMod: malloc failure (cArray)", frameCtx);
                vsapi->freeFrame(src);
                vsapi->freeFrame(cmask);
                return nullptr;
            }
            const bool isCombed = checkCombed(src, cmask, cArray, d, vsapi);
            vsapi->freeFrame(cmask);
            vs_aligned_free(cArray);
            if (!isCombed)
                return src;
        }

        const VSFrameRef * prv = vsapi->getFrameFilter(std::max(n - 1, 0), !d->useClip2 ? d->node : d->clip2, frameCtx);
        if (d->useClip2) {
            vsapi->freeFrame(src);
            src = vsapi->getFrameFilter(n, d->clip2, frameCtx);
        }
        const VSFrameRef * nxt = vsapi->getFrameFilter(std::min(n + 1, d->viSaved->numFrames - 1), !d->useClip2 ? d->node : d->clip2, frameCtx);
        const VSFrameRef * mask = vsapi->getFrameFilter(nSaved, d->mask, frameCtx);
        VSFrameRef * dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);

        if (d->edeint) {
            const VSFrameRef * efrm = vsapi->getFrameFilter(nSaved, d->edeint, frameCtx);
            eDeint(dst, mask, prv, src, nxt, efrm, d, vsapi);
            vsapi->freeFrame(efrm);
        } else {
            cubicDeint(dst, mask, prv, src, nxt, d, vsapi);
        }

        vsapi->freeFrame(prv);
        vsapi->freeFrame(src);
        vsapi->freeFrame(nxt);
        vsapi->freeFrame(mask);
        return dst;
    }

    return nullptr;
}

static void VS_CC tdeintmodCreateMMFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TDeintModData * d = (TDeintModData *)instanceData;
    vsapi->freeNode(d->node);
    delete d;
}

static void VS_CC tdeintmodBuildMMFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TDeintModData * d = (TDeintModData *)instanceData;
    vsapi->freeNode(d->node);
    vsapi->freeNode(d->node2);
    delete d;
}

static void VS_CC tdeintmodFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    TDeintModData * d = (TDeintModData *)instanceData;
    vsapi->freeNode(d->node);
    vsapi->freeNode(d->mask);
    vsapi->freeNode(d->clip2);
    vsapi->freeNode(d->edeint);
    for (int i = 0; i < d->vi.format->numPlanes; i++) {
        delete[] d->offplut[i];
        delete[] d->offnlut[i];
    }
    delete d;
}

static void VS_CC tdeintmodCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    TDeintModData d;
    int err;

    d.order = !!vsapi->propGetInt(in, "order", 0, nullptr);
    d.field = !!vsapi->propGetInt(in, "field", 0, &err);
    if (err)
        d.field = d.order;
    d.mode = !!vsapi->propGetInt(in, "mode", 0, &err);
    d.length = int64ToIntS(vsapi->propGetInt(in, "length", 0, &err));
    if (err)
        d.length = 10;
    d.mtype = int64ToIntS(vsapi->propGetInt(in, "mtype", 0, &err));
    if (err)
        d.mtype = 1;
    d.ttype = int64ToIntS(vsapi->propGetInt(in, "ttype", 0, &err));
    if (err)
        d.ttype = 1;
    d.mtqL = int64ToIntS(vsapi->propGetInt(in, "mtql", 0, &err));
    if (err)
        d.mtqL = -1;
    d.mthL = int64ToIntS(vsapi->propGetInt(in, "mthl", 0, &err));
    if (err)
        d.mthL = -1;
    d.mtqC = int64ToIntS(vsapi->propGetInt(in, "mtqc", 0, &err));
    if (err)
        d.mtqC = -1;
    d.mthC = int64ToIntS(vsapi->propGetInt(in, "mthc", 0, &err));
    if (err)
        d.mthC = -1;
    d.nt = int64ToIntS(vsapi->propGetInt(in, "nt", 0, &err));
    if (err)
        d.nt = 2;
    d.minthresh = int64ToIntS(vsapi->propGetInt(in, "minthresh", 0, &err));
    if (err)
        d.minthresh = 4;
    d.maxthresh = int64ToIntS(vsapi->propGetInt(in, "maxthresh", 0, &err));
    if (err)
        d.maxthresh = 75;
    d.cstr = int64ToIntS(vsapi->propGetInt(in, "cstr", 0, &err));
    if (err)
        d.cstr = 4;
    d.full = !!vsapi->propGetInt(in, "full", 0, &err);
    if (err)
        d.full = true;
    d.cthresh = int64ToIntS(vsapi->propGetInt(in, "cthresh", 0, &err));
    if (err)
        d.cthresh = 6;
    d.blockx = int64ToIntS(vsapi->propGetInt(in, "blockx", 0, &err));
    if (err)
        d.blockx = 16;
    d.blocky = int64ToIntS(vsapi->propGetInt(in, "blocky", 0, &err));
    if (err)
        d.blocky = 16;
    d.chroma = !!vsapi->propGetInt(in, "chroma", 0, &err);
    d.MI = int64ToIntS(vsapi->propGetInt(in, "mi", 0, &err));
    if (err)
        d.MI = 64;
    d.metric = !!vsapi->propGetInt(in, "metric", 0, &err);

    if (d.length < 6 || d.length > 60) {
        vsapi->setError(out, "TDeintMod: length must be between 6 and 60 inclusive");
        return;
    }
    if (d.mtype < 0 || d.mtype > 2) {
        vsapi->setError(out, "TDeintMod: mtype must be 0, 1, or 2");
        return;
    }
    if (d.ttype < 0 || d.ttype > 5) {
        vsapi->setError(out, "TDeintMod: ttype must be 0, 1, 2, 3, 4, or 5");
        return;
    }
    if (d.mtqL < -1 || d.mtqL > 255) {
        vsapi->setError(out, "TDeintMod: mtql must be between -1 and 255 inclusive");
        return;
    }
    if (d.mthL < -1 || d.mthL > 255) {
        vsapi->setError(out, "TDeintMod: mthl must be between -1 and 255 inclusive");
        return;
    }
    if (d.mtqC < -1 || d.mtqC > 255) {
        vsapi->setError(out, "TDeintMod: mtqc must be between -1 and 255 inclusive");
        return;
    }
    if (d.mthC < -1 || d.mthC > 255) {
        vsapi->setError(out, "TDeintMod: mthc must be between -1 and 255 inclusive");
        return;
    }
    if (d.blockx < 4 || d.blockx > 2048 || !isPowerOf2(d.blockx)) {
        vsapi->setError(out, "TDeintMod: illegal blockx size");
        return;
    }
    if (d.blocky < 4 || d.blocky > 2048 || !isPowerOf2(d.blocky)) {
        vsapi->setError(out, "TDeintMod: illegal blocky size");
        return;
    }

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(&d.vi) || !d.vi.numFrames || (d.vi.format->colorFamily != cmGray && d.vi.format->colorFamily != cmYUV) ||
        d.vi.format->sampleType != stInteger || d.vi.format->bitsPerSample != 8) {
        vsapi->setError(out, "TDeintMod: only constant format 8-bit Gray or YUV integer input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.vi.width & 1 || d.vi.height & 1) {
        vsapi->setError(out, "TDeintMod: width and height must be multiples of 2");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.vi.format->colorFamily == cmGray)
        d.chroma = false;

    VSMap * args = vsapi->createMap();
    VSPlugin * stdPlugin = vsapi->getPluginById("com.vapoursynth.std", core);

    vsapi->propSetNode(args, "clip", d.node, paReplace);
    vsapi->freeNode(d.node);
    vsapi->propSetInt(args, "tff", 1, paReplace);
    VSMap * ret = vsapi->invoke(stdPlugin, "SeparateFields", args);
    if (vsapi->getError(ret)) {
        vsapi->setError(out, vsapi->getError(ret));
        vsapi->freeMap(args);
        vsapi->freeMap(ret);
        return;
    }
    VSNodeRef * separated = vsapi->propGetNode(ret, "clip", 0, nullptr);
    vsapi->clearMap(args);
    vsapi->freeMap(ret);

    vsapi->propSetNode(args, "clip", separated, paReplace);
    vsapi->propSetInt(args, "cycle", 2, paReplace);
    vsapi->propSetInt(args, "offsets", 0, paReplace);
    ret = vsapi->invoke(stdPlugin, "SelectEvery", args);
    if (vsapi->getError(ret)) {
        vsapi->setError(out, vsapi->getError(ret));
        vsapi->freeMap(args);
        vsapi->freeMap(ret);
        vsapi->freeNode(separated);
        return;
    }
    d.node = vsapi->propGetNode(ret, "clip", 0, nullptr);
    d.vi = *vsapi->getVideoInfo(d.node);
    vsapi->clearMap(args);
    vsapi->freeMap(ret);

    for (int i = 0; i < d.vi.format->numPlanes; i++) {
        const int width = d.vi.width >> (i ? d.vi.format->subSamplingW : 0);
        d.offplut[i] = new int[width];
        d.offnlut[i] = new int[width];
        for (int j = 0; j < width; j++) {
            if (j == 0)
                d.offplut[i][j] = -1;
            else
                d.offplut[i][j] = 1;
            if (j == width - 1)
                d.offnlut[i][j] = -1;
            else
                d.offnlut[i][j] = 1;
        }
    }

    for (int i = 0; i < 256; i++)
        d.mlut[i] = std::min(std::max(i + d.nt, d.minthresh), d.maxthresh);

    TDeintModData * data = new TDeintModData(d);

    vsapi->createFilter(in, out, "TDeintMod", tdeintmodInit, tdeintmodCreateMMGetFrame, tdeintmodCreateMMFree, fmParallel, 0, data, core);
    VSNodeRef * temp = vsapi->propGetNode(out, "clip", 0, nullptr);
    vsapi->clearMap(out);
    if (!invokeCache(&temp, out, stdPlugin, vsapi))
        return;

    vsapi->propSetNode(args, "clip", separated, paReplace);
    vsapi->freeNode(separated);
    vsapi->propSetInt(args, "cycle", 2, paReplace);
    vsapi->propSetInt(args, "offsets", 1, paReplace);
    ret = vsapi->invoke(stdPlugin, "SelectEvery", args);
    if (vsapi->getError(ret)) {
        vsapi->setError(out, vsapi->getError(ret));
        vsapi->freeMap(args);
        vsapi->freeMap(ret);
        vsapi->freeNode(temp);
        return;
    }
    d.node = vsapi->propGetNode(ret, "clip", 0, nullptr);
    d.vi = *vsapi->getVideoInfo(d.node);
    vsapi->freeMap(args);
    vsapi->freeMap(ret);

    data = new TDeintModData(d);

    vsapi->createFilter(in, out, "TDeintMod", tdeintmodInit, tdeintmodCreateMMGetFrame, tdeintmodCreateMMFree, fmParallel, 0, data, core);
    d.node2 = vsapi->propGetNode(out, "clip", 0, nullptr);
    vsapi->clearMap(out);
    if (!invokeCache(&d.node2, out, stdPlugin, vsapi))
        return;

    d.node = temp;
    d.vi = *vsapi->getVideoInfo(d.node);
    d.viSaved = vsapi->getVideoInfo(d.node);

    d.vi.height *= 2;
    if (d.mode == 1) {
        d.vi.numFrames *= 2;
        d.vi.fpsNum *= 2;
    }

    for (int i = 0; i < d.length; i++)
        d.gvlut[i] = i == 0 ? 1 : (i == d.length - 1 ? 4 : 2);

    if (d.mtype == 0) {
        d.vlut = {
            0, 1, 2, 2, 3, 0, 2, 2,
            1, 1, 2, 2, 0, 1, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            3, 0, 2, 2, 3, 3, 2, 2,
            0, 1, 2, 2, 3, 1, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2
        };
    } else if (d.mtype == 1) {
        d.vlut = {
            0, 0, 2, 2, 0, 0, 2, 2,
            0, 1, 2, 2, 0, 1, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            0, 0, 2, 2, 3, 3, 2, 2,
            0, 1, 2, 2, 3, 1, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2
        };
    } else {
        d.vlut = {
            0, 0, 0, 0, 0, 0, 0, 0,
            0, 1, 0, 1, 0, 1, 0, 1,
            0, 0, 2, 2, 0, 0, 2, 2,
            0, 1, 2, 2, 0, 1, 2, 2,
            0, 0, 0, 0, 3, 3, 3, 3,
            0, 1, 0, 1, 3, 1, 3, 1,
            0, 0, 2, 2, 3, 3, 2, 2,
            0, 1, 2, 2, 3, 1, 2, 2
        };
    }

    d.tmmlut16 = {
        60, 20, 50, 10, 60, 10, 40, 30,
        60, 10, 40, 30, 60, 20, 50, 10
    };

    data = new TDeintModData(d);

    vsapi->createFilter(in, out, "TDeintMod", tdeintmodInit, tdeintmodBuildMMGetFrame, tdeintmodBuildMMFree, fmParallel, 0, data, core);
    d.mask = vsapi->propGetNode(out, "clip", 0, nullptr);
    vsapi->clearMap(out);
    if (!invokeCache(&d.mask, out, stdPlugin, vsapi))
        return;

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.clip2 = vsapi->propGetNode(in, "clip2", 0, &err);
    d.edeint = vsapi->propGetNode(in, "edeint", 0, &err);
    d.vi = *vsapi->getVideoInfo(d.node);
    d.viSaved = vsapi->getVideoInfo(d.node);

    d.useClip2 = false;
    if (!d.full && d.mode == 0 && d.clip2) {
        if (!isSameFormat(vsapi->getVideoInfo(d.clip2), d.viSaved)) {
            vsapi->setError(out, "TDeintMod: clip2 must have the same dimensions as main clip and be the same format");
            vsapi->freeNode(d.node);
            vsapi->freeNode(d.mask);
            vsapi->freeNode(d.clip2);
            vsapi->freeNode(d.edeint);
            return;
        }

        if (vsapi->getVideoInfo(d.clip2)->numFrames != d.viSaved->numFrames) {
            vsapi->setError(out, "TDeintMod: clip2's number of frames doesn't match");
            vsapi->freeNode(d.node);
            vsapi->freeNode(d.mask);
            vsapi->freeNode(d.clip2);
            vsapi->freeNode(d.edeint);
            return;
        }

        d.useClip2 = true;
    }

    if (d.edeint) {
        if (!isSameFormat(vsapi->getVideoInfo(d.edeint), d.viSaved)) {
            vsapi->setError(out, "TDeintMod: edeint clip must have the same dimensions as main clip and be the same format");
            vsapi->freeNode(d.node);
            vsapi->freeNode(d.mask);
            vsapi->freeNode(d.clip2);
            vsapi->freeNode(d.edeint);
            return;
        }

        if (vsapi->getVideoInfo(d.edeint)->numFrames != d.viSaved->numFrames * (d.mode == 0 ? 1 : 2)) {
            vsapi->setError(out, "TDeintMod: edeint clip's number of frames doesn't match");
            vsapi->freeNode(d.node);
            vsapi->freeNode(d.mask);
            vsapi->freeNode(d.clip2);
            vsapi->freeNode(d.edeint);
            return;
        }
    }

    d.xhalf = d.blockx / 2;
    d.yhalf = d.blocky / 2;
    d.xshift = (int)std::log2(d.blockx);
    d.yshift = (int)std::log2(d.blocky);
    d.cthresh6 = d.cthresh * 6;
    d.cthreshsq = d.cthresh * d.cthresh;

    if (d.mode == 1) {
        d.vi.numFrames *= 2;
        d.vi.fpsNum *= 2;
    }

    data = new TDeintModData(d);

    vsapi->createFilter(in, out, "TDeintMod", tdeintmodInit, tdeintmodGetFrame, tdeintmodFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.holywu.tdeintmod", "tdm", "A bi-directionally motion adaptive deinterlacer", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("TDeintMod",
                 "clip:clip;order:int;field:int:opt;mode:int:opt;"
                 "length:int:opt;mtype:int:opt;ttype:int:opt;mtql:int:opt;mthl:int:opt;mtqc:int:opt;mthc:int:opt;nt:int:opt;minthresh:int:opt;maxthresh:int:opt;cstr:int:opt;"
                 "clip2:clip:opt;full:int:opt;cthresh:int:opt;blockx:int:opt;blocky:int:opt;chroma:int:opt;mi:int:opt;edeint:clip:opt;metric:int:opt;",
                 tdeintmodCreate, nullptr, plugin);
}
