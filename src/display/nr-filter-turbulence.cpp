/*
 * feTurbulence filter primitive renderer
 *
 * Authors:
 *   Felipe Corrêa da Silva Sanches <felipe.sanches@gmail.com>
 *
 * Copyright (C) 2007 authors
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include "display/nr-arena-item.h"
#include "display/nr-filter.h"
#include "display/nr-filter-turbulence.h"
#include "display/nr-filter-units.h"
#include "display/nr-filter-utils.h"
#include "libnr/nr-rect-l.h"
#include <math.h>

namespace NR {

FilterTurbulence::FilterTurbulence()
: XbaseFrequency(0),
  YbaseFrequency(0),
  numOctaves(1),
  seed(0),
  updated(false),
  updated_area(IPoint(), IPoint()),
  pix(NULL),
  fTileWidth(10), //guessed
  fTileHeight(10), //guessed
  fTileX(1), //guessed
  fTileY(1) //guessed
{
}

FilterPrimitive * FilterTurbulence::create() {
    return new FilterTurbulence();
}

FilterTurbulence::~FilterTurbulence()
{
    if (pix) {
        nr_pixblock_release(pix);
        delete pix;
    }
}

void FilterTurbulence::set_baseFrequency(int axis, double freq){
    if (axis==0) XbaseFrequency=freq;
    if (axis==1) YbaseFrequency=freq;
}

void FilterTurbulence::set_numOctaves(int num){
    numOctaves=num;
}

void FilterTurbulence::set_seed(double s){
    seed=s;
}

void FilterTurbulence::set_stitchTiles(bool st){
    stitchTiles=st;
}

void FilterTurbulence::set_type(FilterTurbulenceType t){
    type=t;
}

void FilterTurbulence::set_updated(bool u){
    updated=u;
}

void FilterTurbulence::update_pixbuffer(FilterSlot &/*slot*/, IRect &area) {
//g_warning("update_pixbuf");
    int bbox_x0 = area.min()[X];
    int bbox_y0 = area.min()[Y];
    int bbox_x1 = area.max()[X];
    int bbox_y1 = area.max()[Y];

    int w = bbox_x1 - bbox_x0;
    int h = bbox_y1 - bbox_y0;

    if (!pix){
        pix = new NRPixBlock;
        nr_pixblock_setup_fast(pix, NR_PIXBLOCK_MODE_R8G8B8A8N, bbox_x0, bbox_y0, bbox_x1, bbox_y1, true);
        pix_data = NR_PIXBLOCK_PX(pix);
    }
    else if (bbox_x0 != pix->area.x0 || bbox_y0 != pix->area.y0 ||
        bbox_x1 != pix->area.x1 || bbox_y1 != pix->area.y1)
    {
        /* TODO: release-setup cycle not actually needed, if pixblock
         * width and height don't change */
        nr_pixblock_release(pix);
        nr_pixblock_setup_fast(pix, NR_PIXBLOCK_MODE_R8G8B8A8N, bbox_x0, bbox_y0, bbox_x1, bbox_y1, true);
        pix_data = NR_PIXBLOCK_PX(pix);
    }

    TurbulenceInit((long)seed);

    double point[2];

    if (type==TURBULENCE_TURBULENCE){
        for (point[0]=0; point[0] < w; point[0]++){
            for (point[1]=0; point[1] < h; point[1]++){
                pix_data[4*(int(point[0]) + w*int(point[1]))] = CLAMP_D_TO_U8( turbulence(0,point)*255 );
                pix_data[4*(int(point[0]) + w*int(point[1])) + 1] = CLAMP_D_TO_U8( turbulence(1,point)*255 );
                pix_data[4*(int(point[0]) + w*int(point[1])) + 2] = CLAMP_D_TO_U8( turbulence(2,point)*255 );
                pix_data[4*(int(point[0]) + w*int(point[1])) + 3] = CLAMP_D_TO_U8( turbulence(3,point)*255 );
            }
        }
    } else {
        for (point[0]=0; point[0] < w; point[0]++){
            for (point[1]=0; point[1] < h; point[1]++){
                pix_data[4*(int(point[0]) + w*int(point[1]))] = CLAMP_D_TO_U8( ((turbulence(0,point)*255) +255)/2 );
                pix_data[4*(int(point[0]) + w*int(point[1])) + 1] = CLAMP_D_TO_U8( ((turbulence(1,point)*255)+255)/2 );
                pix_data[4*(int(point[0]) + w*int(point[1])) + 2] = CLAMP_D_TO_U8( ((turbulence(2,point)*255) +255)/2 );
                pix_data[4*(int(point[0]) + w*int(point[1])) + 3] = CLAMP_D_TO_U8( ((turbulence(3,point)*255) +255)/2 );
            }
        }
    }
    updated=true;
    updated_area = area;
}

int FilterTurbulence::render(FilterSlot &slot, FilterUnits const &units) {
//g_warning("render");
    IRect area = units.get_pixblock_filterarea_paraller();
    // TODO: could be faster - updated_area only has to be same size as area
    if (!updated || updated_area != area) update_pixbuffer(slot, area);

    NRPixBlock *in = slot.get(_input);
    NRPixBlock *out = new NRPixBlock;
    int x,y;
    int x0 = in->area.x0, y0 = in->area.y0;
    int x1 = in->area.x1, y1 = in->area.y1;
    int w = x1 - x0;
    nr_pixblock_setup_fast(out, NR_PIXBLOCK_MODE_R8G8B8A8N, x0, y0, x1, y1, true);

    int bbox_x0 = area.min()[X];
    int bbox_y0 = area.min()[Y];
    int bbox_x1 = area.max()[X];
    int bbox_y1 = area.max()[Y];
    int bbox_w = bbox_x1 - bbox_x0;

    unsigned char *out_data = NR_PIXBLOCK_PX(out);
    for (x = std::max(x0, bbox_x0); x < std::min(x1, bbox_x1); x++){
        for (y = std::max(y0, bbox_y0); y < std::min(y1, bbox_y1); y++){
            out_data[4*((x - x0)+w*(y - y0))] = pix_data[4*(x - bbox_x0 + bbox_w*(y - bbox_y0)) ];
            out_data[4*((x - x0)+w*(y - y0)) + 1] = pix_data[4*(x - bbox_x0 + bbox_w*(y - bbox_y0))+1];
            out_data[4*((x - x0)+w*(y - y0)) + 2] = pix_data[4*(x - bbox_x0 + bbox_w*(y - bbox_y0))+2];
            out_data[4*((x - x0)+w*(y - y0)) + 3] = pix_data[4*(x - bbox_x0 + bbox_w*(y - bbox_y0))+3];
        }
    }
    out->empty = FALSE;
    slot.set(_output, out);
    return 0;
}

long FilterTurbulence::Turbulence_setup_seed(long lSeed)
{
  if (lSeed <= 0) lSeed = -(lSeed % (RAND_m - 1)) + 1;
  if (lSeed > RAND_m - 1) lSeed = RAND_m - 1;
  return lSeed;
}

long FilterTurbulence::TurbulenceRandom(long lSeed)
{
  long result;
  result = RAND_a * (lSeed % RAND_q) - RAND_r * (lSeed / RAND_q);
  if (result <= 0) result += RAND_m;
  return result;
}

void FilterTurbulence::TurbulenceInit(long lSeed)
{
//g_warning("init");
  double s;
  int i, j, k;
  lSeed = Turbulence_setup_seed(lSeed);
  for(k = 0; k < 4; k++)
  {
    for(i = 0; i < BSize; i++)
    {
      uLatticeSelector[i] = i;
      for (j = 0; j < 2; j++)
        fGradient[k][i][j] = (double)(((lSeed = TurbulenceRandom(lSeed)) % (BSize + BSize)) - BSize) / BSize;
      s = double(sqrt(fGradient[k][i][0] * fGradient[k][i][0] + fGradient[k][i][1] * fGradient[k][i][1]));
      fGradient[k][i][0] /= s;
      fGradient[k][i][1] /= s;
    }
  }
  while(--i)
  {
    k = uLatticeSelector[i];
    uLatticeSelector[i] = uLatticeSelector[j = (lSeed = TurbulenceRandom(lSeed)) % BSize];
    uLatticeSelector[j] = k;
  }
  for(i = 0; i < BSize + 2; i++)
  {
    uLatticeSelector[BSize + i] = uLatticeSelector[i];
    for(k = 0; k < 4; k++)
      for(j = 0; j < 2; j++)
        fGradient[k][BSize + i][j] = fGradient[k][i][j];
  }
}

double FilterTurbulence::TurbulenceNoise2(int nColorChannel, double vec[2], StitchInfo *pStitchInfo)
{
  int bx0, bx1, by0, by1, b00, b10, b01, b11;
  double rx0, rx1, ry0, ry1, *q, sx, sy, a, b, t, u, v;
  int i, j;
  t = vec[0] + PerlinN;
  bx0 = (int)t;
  bx1 = bx0+1;
  rx0 = t - (int)t;
  rx1 = rx0 - 1.0f;
  t = vec[1] + PerlinN;
  by0 = (int)t;
  by1 = by0+1;
  ry0 = t - (int)t;
  ry1 = ry0 - 1.0f;
  // If stitching, adjust lattice points accordingly.
  if(pStitchInfo != NULL)
  {
    if(bx0 >= pStitchInfo->nWrapX)
      bx0 -= pStitchInfo->nWidth;
    if(bx1 >= pStitchInfo->nWrapX)
      bx1 -= pStitchInfo->nWidth;
    if(by0 >= pStitchInfo->nWrapY)
      by0 -= pStitchInfo->nHeight;
    if(by1 >= pStitchInfo->nWrapY)
      by1 -= pStitchInfo->nHeight;
  }
  bx0 &= BM;
  bx1 &= BM;
  by0 &= BM;
  by1 &= BM;
  i = uLatticeSelector[bx0];
  j = uLatticeSelector[bx1];
  b00 = uLatticeSelector[i + by0];
  b10 = uLatticeSelector[j + by0];
  b01 = uLatticeSelector[i + by1];
  b11 = uLatticeSelector[j + by1];
  sx = double(s_curve(rx0));
  sy = double(s_curve(ry0));
  q = fGradient[nColorChannel][b00]; u = rx0 * q[0] + ry0 * q[1];
  q = fGradient[nColorChannel][b10]; v = rx1 * q[0] + ry0 * q[1];
  a = turb_lerp(sx, u, v);
  q = fGradient[nColorChannel][b01]; u = rx0 * q[0] + ry1 * q[1];
  q = fGradient[nColorChannel][b11]; v = rx1 * q[0] + ry1 * q[1];
  b = turb_lerp(sx, u, v);
  return turb_lerp(sy, a, b);
}

double FilterTurbulence::turbulence(int nColorChannel, double *point)
{
//g_warning("turbulence");
  StitchInfo stitch;
  StitchInfo *pStitchInfo = NULL; // Not stitching when NULL.
  // Adjust the base frequencies if necessary for stitching.
  if(stitchTiles)
  {
    // When stitching tiled turbulence, the frequencies must be adjusted
    // so that the tile borders will be continuous.
    if(XbaseFrequency != 0.0)
    {
      double fLoFreq = double(floor(fTileWidth * XbaseFrequency)) / fTileWidth;
      double fHiFreq = double(ceil(fTileWidth * XbaseFrequency)) / fTileWidth;
      if(XbaseFrequency / fLoFreq < fHiFreq / XbaseFrequency)
        XbaseFrequency = fLoFreq;
      else
        XbaseFrequency = fHiFreq;
    }
    if(YbaseFrequency != 0.0)
    {
      double fLoFreq = double(floor(fTileHeight * YbaseFrequency)) / fTileHeight;
      double fHiFreq = double(ceil(fTileHeight * YbaseFrequency)) / fTileHeight;
      if(YbaseFrequency / fLoFreq < fHiFreq / YbaseFrequency)
        YbaseFrequency = fLoFreq;
      else
        YbaseFrequency = fHiFreq;
    }
    // Set up TurbulenceInitial stitch values.
    pStitchInfo = &stitch;
    stitch.nWidth = int(fTileWidth * XbaseFrequency + 0.5f);
    stitch.nWrapX = int(fTileX * XbaseFrequency + PerlinN + stitch.nWidth);
    stitch.nHeight = int(fTileHeight * YbaseFrequency + 0.5f);
    stitch.nWrapY = int(fTileY * YbaseFrequency + PerlinN + stitch.nHeight);
  }
  double fSum = 0.0f;
  double vec[2];
  vec[0] = point[0] * XbaseFrequency;
  vec[1] = point[1] * YbaseFrequency;
  double ratio = 1;
  for(int nOctave = 0; nOctave < numOctaves; nOctave++)
  {
    if(type==TURBULENCE_FRACTALNOISE)
      fSum += double(TurbulenceNoise2(nColorChannel, vec, pStitchInfo) / ratio);
    else
      fSum += double(fabs(TurbulenceNoise2(nColorChannel, vec, pStitchInfo)) / ratio);
    vec[0] *= 2;
    vec[1] *= 2;
    ratio *= 2;
    if(pStitchInfo != NULL)
    {
      // Update stitch values. Subtracting PerlinN before the multiplication and
      // adding it afterward simplifies to subtracting it once.
      stitch.nWidth *= 2;
      stitch.nWrapX = 2 * stitch.nWrapX - PerlinN;
      stitch.nHeight *= 2;
      stitch.nWrapY = 2 * stitch.nWrapY - PerlinN;
    }
  }
  return fSum;
}

FilterTraits FilterTurbulence::get_input_traits() {
    return TRAIT_PARALLER;
}

} /* namespace NR */

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=99 :
