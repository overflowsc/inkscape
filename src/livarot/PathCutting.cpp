/*
 *  PathCutting.cpp
 *  nlivarot
 *
 *  Created by fred on someday in 2004.
 *  public domain
 *
 *  Additional Code by Authors:
 *   Richard Hughes <cyreve@users.sf.net>
 *
 *  Copyright (C) 2005 Richard Hughes
 *
 *  Released under GNU GPL, read the file 'COPYING' for more information
 */

#include "Path.h"
#include "style.h"
#include "livarot/path-description.h"
#include "libnr/n-art-bpath.h"
#include "libnr/nr-point-matrix-ops.h"

void  Path::DashPolyline(float head,float tail,float body,int nbD,float *dashs,bool stPlain,float stOffset)
{
  if ( nbD <= 0 || body <= 0.0001 ) return; // pas de tirets, en fait

  std::vector<path_lineto> orig_pts = pts;
  pts.clear();
  
  int       lastMI=-1;
  int curP = 0;
  int lastMP = -1;
  
  for (int i = 0; i < int(orig_pts.size()); i++) {
    if ( orig_pts[curP].isMoveTo == polyline_moveto ) {
      if ( lastMI >= 0 && lastMI < i-1 ) { // au moins 2 points
        DashSubPath(i-lastMI,lastMP, orig_pts, head,tail,body,nbD,dashs,stPlain,stOffset);
      }
      lastMI=i;
      lastMP=curP;
    }
    curP++;
  }
  if ( lastMI >= 0 && lastMI < int(orig_pts.size()) - 1 ) {
    DashSubPath(orig_pts.size() - lastMI, lastMP, orig_pts, head, tail, body, nbD, dashs, stPlain, stOffset);
  }
}

void  Path::DashPolylineFromStyle(SPStyle *style, float scale, float min_len)
{
    if (style->stroke_dash.n_dash) {

        double dlen = 0.0;
        for (int i = 0; i < style->stroke_dash.n_dash; i++) {
            dlen += style->stroke_dash.dash[i] * scale;
        }
        if (dlen >= min_len) {
            NRVpathDash dash;
            dash.offset = style->stroke_dash.offset * scale;
            dash.n_dash = style->stroke_dash.n_dash;
            dash.dash = g_new(double, dash.n_dash);
            for (int i = 0; i < dash.n_dash; i++) {
                dash.dash[i] = style->stroke_dash.dash[i] * scale;
            }
            int    nbD=dash.n_dash;
            float  *dashs=(float*)malloc((nbD+1)*sizeof(float));
            while ( dash.offset >= dlen ) dash.offset-=dlen;
            dashs[0]=dash.dash[0];
            for (int i=1; i<nbD; i++) {
                dashs[i]=dashs[i-1]+dash.dash[i];
            }
            // modulo dlen
            this->DashPolyline(0.0, 0.0, dlen, nbD, dashs, true, dash.offset);
            free(dashs);
            g_free(dash.dash);
        }
    }
}


void Path::DashSubPath(int spL, int spP, std::vector<path_lineto> const &orig_pts, float head,float tail,float body,int nbD,float *dashs,bool stPlain,float stOffset)
{
  if ( spL <= 0 || spP == -1 ) return;
  
  double      totLength=0;
  NR::Point   lastP;
  lastP = orig_pts[spP].p;
  for (int i=1;i<spL;i++) {
    NR::Point const n = orig_pts[spP + i].p;
    NR::Point d=n-lastP;
    double    nl=NR::L2(d);
    if ( nl > 0.0001 ) {
      totLength+=nl;
      lastP=n;
    }
  }
  
  if ( totLength <= head+tail ) return; // tout mange par la tete et la queue
  
  double    curLength=0;
  double    dashPos=0;
  int       dashInd=0;
  bool      dashPlain=false;
  double    lastT=0;
  int       lastPiece=-1;
  lastP = orig_pts[spP].p;
  for (int i=1;i<spL;i++) {
    NR::Point   n;
    int         nPiece=-1;
    double      nT=0;
    if ( back ) {
      n = orig_pts[spP + i].p;
      nPiece = orig_pts[spP + i].piece;
      nT = orig_pts[spP + i].t;
    } else {
      n = orig_pts[spP + i].p;
    }
    NR::Point d=n-lastP;
    double    nl=NR::L2(d);
    if ( nl > 0.0001 ) {
      double   stLength=curLength;
      double   enLength=curLength+nl;
      // couper les bouts en trop
      if ( curLength <= head && curLength+nl > head ) {
        nl-=head-curLength;
        curLength=head;
        dashInd=0;
        dashPos=stOffset;
        bool nPlain=stPlain;
        while ( dashs[dashInd] < stOffset ) {
          dashInd++;
          nPlain=!(nPlain);
          if ( dashInd >= nbD ) {
            dashPos=0;
            dashInd=0;
            break;
          }
        }
        if ( nPlain == true && dashPlain == false ) {
          NR::Point  p=(enLength-curLength)*lastP+(curLength-stLength)*n;
          p/=(enLength-stLength);
          if ( back ) {
            double pT=0;
            if ( nPiece == lastPiece ) {
              pT=(lastT*(enLength-curLength)+nT*(curLength-stLength))/(enLength-stLength);
            } else {
              pT=(nPiece*(curLength-stLength))/(enLength-stLength);
            }
            AddPoint(p,nPiece,pT,true);
          } else {
            AddPoint(p,true);
          }
        } else if ( nPlain == false && dashPlain == true ) {
        }
        dashPlain=nPlain;
      }
      // faire les tirets
      if ( curLength >= head /*&& curLength+nl <= totLength-tail*/ ) {
        while ( curLength <= totLength-tail && nl > 0 ) {
          if ( enLength <= totLength-tail ) nl=enLength-curLength; else nl=totLength-tail-curLength;
          double  leftInDash=body-dashPos;
          if ( dashInd < nbD ) {
            leftInDash=dashs[dashInd]-dashPos;
          }
          if ( leftInDash <= nl ) {
            bool nPlain=false;
            if ( dashInd < nbD ) {
              dashPos=dashs[dashInd];
              dashInd++;
              if ( dashPlain ) nPlain=false; else nPlain=true;
            } else {
              dashInd=0;
              dashPos=0;
              //nPlain=stPlain;
              nPlain=dashPlain;
            }
            if ( nPlain == true && dashPlain == false ) {
              NR::Point  p=(enLength-curLength-leftInDash)*lastP+(curLength+leftInDash-stLength)*n;
              p/=(enLength-stLength);
              if ( back ) {
                double pT=0;
                if ( nPiece == lastPiece ) {
                  pT=(lastT*(enLength-curLength-leftInDash)+nT*(curLength+leftInDash-stLength))/(enLength-stLength);
                } else {
                  pT=(nPiece*(curLength+leftInDash-stLength))/(enLength-stLength);
                }
                AddPoint(p,nPiece,pT,true);
              } else {
                AddPoint(p,true);
              }
            } else if ( nPlain == false && dashPlain == true ) {
              NR::Point  p=(enLength-curLength-leftInDash)*lastP+(curLength+leftInDash-stLength)*n;
              p/=(enLength-stLength);
              if ( back ) {
                double pT=0;
                if ( nPiece == lastPiece ) {
                  pT=(lastT*(enLength-curLength-leftInDash)+nT*(curLength+leftInDash-stLength))/(enLength-stLength);
                } else {
                  pT=(nPiece*(curLength+leftInDash-stLength))/(enLength-stLength);
                }
                AddPoint(p,nPiece,pT,false);
              } else {
                AddPoint(p,false);
              }
            }
            dashPlain=nPlain;
            
            curLength+=leftInDash;
            nl-=leftInDash;
          } else {
            dashPos+=nl;
            curLength+=nl;
            nl=0;
          }
        }
        if ( dashPlain ) {
          if ( back ) {
            AddPoint(n,nPiece,nT,false);
          } else {
            AddPoint(n,false);
          }
        }
        nl=enLength-curLength;
      }
      if ( curLength <= totLength-tail && curLength+nl > totLength-tail ) {
        nl=totLength-tail-curLength;
        dashInd=0;
        dashPos=0;
        bool nPlain=false;
        if ( nPlain == true && dashPlain == false ) {
        } else if ( nPlain == false && dashPlain == true ) {
          NR::Point  p=(enLength-curLength)*lastP+(curLength-stLength)*n;
          p/=(enLength-stLength);
          if ( back ) {
            double pT=0;
            if ( nPiece == lastPiece ) {
              pT=(lastT*(enLength-curLength)+nT*(curLength-stLength))/(enLength-stLength);
            } else {
              pT=(nPiece*(curLength-stLength))/(enLength-stLength);
            }
            AddPoint(p,nPiece,pT,false);
          } else {
            AddPoint(p,false);
          }
        }
        dashPlain=nPlain;
      }
      // continuer
      curLength=enLength;
      lastP=n;
      lastPiece=nPiece;
      lastT=nT;
    }
  }
}
#include "../display/canvas-bpath.h"

void* Path::MakeArtBPath(void)
{
	int				 nb_cmd=0,max_cmd=0;
	NArtBpath* bpath=(NArtBpath*)g_malloc((max_cmd+1)*sizeof(NArtBpath));
	
	NR::Point   lastP,bezSt,bezEn,lastMP;
	int         lastM=-1,bezNb=0;
  for (int i=0;i<int(descr_cmd.size());i++) {
    int const typ = descr_cmd[i]->getType();
    switch ( typ ) {
      case descr_close:
      {
				if ( lastM >= 0 ) {
					bpath[lastM].code=NR_MOVETO;
					if ( nb_cmd >= max_cmd ) {
						max_cmd=2*nb_cmd+1;
						bpath=(NArtBpath*)g_realloc(bpath,(max_cmd+1)*sizeof(NArtBpath));
					}
					bpath[nb_cmd].code=NR_LINETO;
					bpath[nb_cmd].x3=lastMP[0];
					bpath[nb_cmd].y3=lastMP[1];
					nb_cmd++;
				}
				lastM=-1;
			} 
				break;
			case descr_lineto:
				{
        PathDescrLineTo *nData = dynamic_cast<PathDescrLineTo *>(descr_cmd[i]);
				if ( nb_cmd >= max_cmd ) {
					max_cmd=2*nb_cmd+1;
					bpath=(NArtBpath*)g_realloc(bpath,(max_cmd+1)*sizeof(NArtBpath));
				}
				bpath[nb_cmd].code=NR_LINETO;
				bpath[nb_cmd].x3=nData->p[0];
				bpath[nb_cmd].y3=nData->p[1];
				nb_cmd++;
				lastP=nData->p;
      }
        break;
      case descr_moveto:
      {
        PathDescrMoveTo *nData = dynamic_cast<PathDescrMoveTo *>(descr_cmd[i]);
				if ( nb_cmd >= max_cmd ) {
					max_cmd=2*nb_cmd+1;
					bpath=(NArtBpath*)g_realloc(bpath,(max_cmd+1)*sizeof(NArtBpath));
				}
				bpath[nb_cmd].code=NR_MOVETO_OPEN;
				bpath[nb_cmd].x3=nData->p[0];
				bpath[nb_cmd].y3=nData->p[1];
				lastM=nb_cmd;
				nb_cmd++;
				lastP=lastMP=nData->p;
      }
        break;
      case descr_arcto:
      {
        PathDescrArcTo *nData = dynamic_cast<PathDescrArcTo *>(descr_cmd[i]);
				lastP=nData->p;
      }
        break;
      case descr_cubicto:
      {
        PathDescrCubicTo *nData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[i]);
				if ( nb_cmd >= max_cmd ) {
					max_cmd=2*nb_cmd+1;
					bpath=(NArtBpath*)g_realloc(bpath,(max_cmd+1)*sizeof(NArtBpath));
				}
				bpath[nb_cmd].code=NR_CURVETO;
				bpath[nb_cmd].x1=lastP[0]+0.333333*nData->start[0];
				bpath[nb_cmd].y1=lastP[1]+0.333333*nData->start[1];
				bpath[nb_cmd].x2=nData->p[0]-0.333333*nData->end[0];
				bpath[nb_cmd].y2=nData->p[1]-0.333333*nData->end[1];
				bpath[nb_cmd].x3=nData->p[0];
				bpath[nb_cmd].y3=nData->p[1];
				nb_cmd++;
				lastP=nData->p;
      }
        break;
      case descr_bezierto:
      {
        PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[i]);
				if ( nb_cmd >= max_cmd ) {
					max_cmd=2*nb_cmd+1;
					bpath=(NArtBpath*)g_realloc(bpath,(max_cmd+1)*sizeof(NArtBpath));
				}
				if ( nData->nb <= 0 ) {
					bpath[nb_cmd].code=NR_LINETO;
					bpath[nb_cmd].x3=nData->p[0];
					bpath[nb_cmd].y3=nData->p[1];
					nb_cmd++;
					bezNb=0;
				} else if ( nData->nb == 1 ){
					PathDescrIntermBezierTo *iData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[i+1]);
					bpath[nb_cmd].code=NR_CURVETO;
					bpath[nb_cmd].x1=0.333333*(lastP[0]+2*iData->p[0]);
					bpath[nb_cmd].y1=0.333333*(lastP[1]+2*iData->p[1]);
					bpath[nb_cmd].x2=0.333333*(nData->p[0]+2*iData->p[0]);
					bpath[nb_cmd].y2=0.333333*(nData->p[1]+2*iData->p[1]);
					bpath[nb_cmd].x3=nData->p[0];
					bpath[nb_cmd].y3=nData->p[1];
					nb_cmd++;
					bezNb=0;
				} else {
					bezSt=2*lastP-nData->p;
					bezEn=nData->p;
					bezNb=nData->nb;
				}
				lastP=nData->p;
      }
        break;
      case descr_interm_bezier:
      {
				if ( bezNb > 0 ) {
					PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[i]);
					NR::Point p_m=nData->p,p_s=0.5*(bezSt+p_m),p_e;
					if ( bezNb > 1 ) {
						PathDescrIntermBezierTo *iData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[i+1]);
						p_e=0.5*(p_m+iData->p);
					} else {
						p_e=bezEn;
					}
					
					if ( nb_cmd >= max_cmd ) {
						max_cmd=2*nb_cmd+1;
						bpath=(NArtBpath*)g_realloc(bpath,(max_cmd+1)*sizeof(NArtBpath));
					}
					bpath[nb_cmd].code=NR_CURVETO;
					NR::Point  cp1=0.333333*(p_s+2*p_m),cp2=0.333333*(2*p_m+p_e);
					bpath[nb_cmd].x1=cp1[0];
					bpath[nb_cmd].y1=cp1[1];
					bpath[nb_cmd].x2=cp2[0];
					bpath[nb_cmd].y2=cp2[1];
					bpath[nb_cmd].x3=p_e[0];
					bpath[nb_cmd].y3=p_e[1];
					nb_cmd++;
					
					bezNb--;
				}
			}
        break;
    }
  }
	bpath[nb_cmd].code=NR_END;
	return bpath;
}

void  Path::LoadArtBPath(void *iV,NR::Matrix const &trans,bool doTransformation)
{
  if ( iV == NULL ) return;
  NArtBpath *bpath = (NArtBpath*)iV;
  
  SetBackData (false);
  Reset();
  {
    int   i;
    bool  closed = false;
    NR::Point lastX(0,0);
    
    for (i = 0; bpath[i].code != NR_END; i++)
    {
      switch (bpath[i].code)
      {
        case NR_LINETO:
          lastX[0] = bpath[i].x3;
          lastX[1] = bpath[i].y3;
          if ( doTransformation ) {
            lastX*=trans;
          }
            LineTo (lastX);
            break;
          
        case NR_CURVETO:
        {
          NR::Point  tmp,tms(0,0),tme(0,0),tm1,tm2;
          tmp[0]=bpath[i].x3;
          tmp[1]=bpath[i].y3;
          tm1[0]=bpath[i].x1;
          tm1[1]=bpath[i].y1;
          tm2[0]=bpath[i].x2;
          tm2[1]=bpath[i].y2;
          if ( doTransformation ) {
            tmp*=trans;
            tm1*=trans;
            tm2*=trans;
          }
          tms=3 * (tm1 - lastX);
          tme=3 * (tmp - tm2);
          CubicTo (tmp,tms,tme);
        }
          lastX[0] = bpath[i].x3;
          lastX[1] = bpath[i].y3;
          if ( doTransformation ) {
            lastX*=trans;
          }
          break;
          
        case NR_MOVETO_OPEN:
        case NR_MOVETO:
          if (closed) Close ();
          closed = (bpath[i].code == NR_MOVETO);
          lastX[0] = bpath[i].x3;
          lastX[1] = bpath[i].y3;
          if ( doTransformation ) {
            lastX*=trans;
          }
            MoveTo (lastX);
            break;
        default:
          break;
      }
    }
    if (closed) Close ();
  }
}


/**
 *    \return Length of the lines in the pts vector.
 */

double Path::Length()
{
    if ( pts.empty() ) {
        return 0;
    }

    NR::Point lastP = pts[0].p;

    double len = 0;
    for (std::vector<path_lineto>::const_iterator i = pts.begin(); i != pts.end(); i++) {

        if ( i->isMoveTo != polyline_moveto ) {
            len += NR::L2(i->p - lastP);
        }

        lastP = i->p;
    }
    
    return len;
}


double Path::Surface()
{
    if ( pts.empty() ) {
        return 0;
    }
    
    NR::Point lastM = pts[0].p;
    NR::Point lastP = lastM;

    double surf = 0;
    for (std::vector<path_lineto>::const_iterator i = pts.begin(); i != pts.end(); i++) {

        if ( i->isMoveTo == polyline_moveto ) {
            surf += NR::cross(lastM - lastP, lastM);
            lastP = lastM = i->p;
        } else {
            surf += NR::cross(i->p - lastP, i->p);
            lastP = i->p;
        }
        
    }
    
  return surf;
}


Path**      Path::SubPaths(int &outNb,bool killNoSurf)
{
  int      nbRes=0;
  Path**   res=NULL;
  Path*    curAdd=NULL;
  
  for (int i=0;i<int(descr_cmd.size());i++) {
    int const typ = descr_cmd[i]->getType();
    switch ( typ ) {
      case descr_moveto:
        if ( curAdd ) {
          if ( curAdd->descr_cmd.size() > 1 ) {
            curAdd->Convert(1.0);
            double addSurf=curAdd->Surface();
            if ( fabs(addSurf) > 0.0001 || killNoSurf == false ) {
              res=(Path**)g_realloc(res,(nbRes+1)*sizeof(Path*));
              res[nbRes++]=curAdd;
            } else { 
              delete curAdd;
            }
          } else {
            delete curAdd;
          }
          curAdd=NULL;
        }
        curAdd=new Path;
        curAdd->SetBackData(false);
        {
          PathDescrMoveTo *nData = dynamic_cast<PathDescrMoveTo *>(descr_cmd[i]);
          curAdd->MoveTo(nData->p);
        }
          break;
      case descr_close:
      {
        curAdd->Close();
      }
        break;        
      case descr_lineto:
      {
        PathDescrLineTo *nData = dynamic_cast<PathDescrLineTo *>(descr_cmd[i]);
        curAdd->LineTo(nData->p);
      }
        break;
      case descr_cubicto:
      {
        PathDescrCubicTo *nData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[i]);
        curAdd->CubicTo(nData->p,nData->start,nData->end);
      }
        break;
      case descr_arcto:
      {
        PathDescrArcTo *nData = dynamic_cast<PathDescrArcTo *>(descr_cmd[i]);
        curAdd->ArcTo(nData->p,nData->rx,nData->ry,nData->angle,nData->large,nData->clockwise);
      }
        break;
      case descr_bezierto:
      {
        PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[i]);
        curAdd->BezierTo(nData->p);
      }
        break;
      case descr_interm_bezier:
      {
        PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[i]);
        curAdd->IntermBezierTo(nData->p);
      }
        break;
      default:
        break;
    }
  }
  if ( curAdd ) {
    if ( curAdd->descr_cmd.size() > 1 ) {
      curAdd->Convert(1.0);
      double addSurf=curAdd->Surface();
      if ( fabs(addSurf) > 0.0001 || killNoSurf == false  ) {
        res=(Path**)g_realloc(res,(nbRes+1)*sizeof(Path*));
        res[nbRes++]=curAdd;
      } else {
        delete curAdd;
      }
    } else {
      delete curAdd;
    }
  }
  curAdd=NULL;
  
  outNb=nbRes;
  return res;
}
Path**      Path::SubPathsWithNesting(int &outNb,bool killNoSurf,int nbNest,int* nesting,int* conts)
{
  int      nbRes=0;
  Path**   res=NULL;
  Path*    curAdd=NULL;
  bool     increment=false;
  
  for (int i=0;i<int(descr_cmd.size());i++) {
    int const typ = descr_cmd[i]->getType();
    switch ( typ ) {
      case descr_moveto:
      {
        if ( curAdd && increment == false ) {
          if ( curAdd->descr_cmd.size() > 1 ) {
            // sauvegarder descr_cmd[0]->associated
            int savA=curAdd->descr_cmd[0]->associated;
            curAdd->Convert(1.0);
            curAdd->descr_cmd[0]->associated=savA; // associated n'est pas utilise apres
            double addSurf=curAdd->Surface();
            if ( fabs(addSurf) > 0.0001 || killNoSurf == false ) {
              res=(Path**)g_realloc(res,(nbRes+1)*sizeof(Path*));
              res[nbRes++]=curAdd;
            } else { 
              delete curAdd;
            }
          } else {
            delete curAdd;
          }
          curAdd=NULL;
        }
        Path*  hasDad=NULL;
        for (int j=0;j<nbNest;j++) {
          if ( conts[j] == i && nesting[j] >= 0 ) {
            int  dadMvt=conts[nesting[j]];
            for (int k=0;k<nbRes;k++) {
              if ( res[k] && res[k]->descr_cmd.empty() == false && res[k]->descr_cmd[0]->associated == dadMvt ) {
                hasDad=res[k];
                break;
              }
            }
          }
          if ( conts[j] > i  ) break;
        }
        if ( hasDad ) {
          curAdd=hasDad;
          increment=true;
        } else {
          curAdd=new Path;
          curAdd->SetBackData(false);
          increment=false;
        }
        PathDescrMoveTo *nData = dynamic_cast<PathDescrMoveTo *>(descr_cmd[i]);
        int mNo=curAdd->MoveTo(nData->p);
        curAdd->descr_cmd[mNo]->associated=i;
        }
        break;
      case descr_close:
      {
        curAdd->Close();
      }
        break;        
      case descr_lineto:
      {
        PathDescrLineTo *nData = dynamic_cast<PathDescrLineTo *>(descr_cmd[i]);
        curAdd->LineTo(nData->p);
      }
        break;
      case descr_cubicto:
      {
        PathDescrCubicTo *nData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[i]);
        curAdd->CubicTo(nData->p,nData->start,nData->end);
      }
        break;
      case descr_arcto:
      {
        PathDescrArcTo *nData = dynamic_cast<PathDescrArcTo *>(descr_cmd[i]);
        curAdd->ArcTo(nData->p,nData->rx,nData->ry,nData->angle,nData->large,nData->clockwise);
      }
        break;
      case descr_bezierto:
      {
        PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[i]);
        curAdd->BezierTo(nData->p);
      }
        break;
      case descr_interm_bezier:
      {
        PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[i]);
        curAdd->IntermBezierTo(nData->p);
      }
        break;
      default:
        break;
    }
  }
  if ( curAdd && increment == false ) {
    if ( curAdd->descr_cmd.size() > 1 ) {
      curAdd->Convert(1.0);
      double addSurf=curAdd->Surface();
      if ( fabs(addSurf) > 0.0001 || killNoSurf == false  ) {
        res=(Path**)g_realloc(res,(nbRes+1)*sizeof(Path*));
        res[nbRes++]=curAdd;
      } else {
        delete curAdd;
      }
    } else {
      delete curAdd;
    }
  }
  curAdd=NULL;
  
  outNb=nbRes;
  return res;
}


void Path::ConvertForcedToVoid()
{  
    for (int i=0; i < int(descr_cmd.size()); i++) {
        if ( descr_cmd[i]->getType() == descr_forced) {
            delete descr_cmd[i];
            descr_cmd.erase(descr_cmd.begin() + i);
        }
    }
}


void Path::ConvertForcedToMoveTo()
{  
    NR::Point lastSeen(0, 0);
    NR::Point lastMove(0, 0);
    
    {
        NR::Point lastPos(0, 0);
        for (int i = int(descr_cmd.size()) - 1; i >= 0; i--) {
            int const typ = descr_cmd[i]->getType();
            switch ( typ ) {
            case descr_forced:
            {
                PathDescrForced *d = dynamic_cast<PathDescrForced *>(descr_cmd[i]);
                d->p = lastPos;
                break;
            }
            case descr_close:
            {
                PathDescrClose *d = dynamic_cast<PathDescrClose *>(descr_cmd[i]);
                d->p = lastPos;
                break;
            }
            case descr_moveto:
            {
                PathDescrMoveTo *d = dynamic_cast<PathDescrMoveTo *>(descr_cmd[i]);
                lastPos = d->p;
                break;
            }
            case descr_lineto:
            {
                PathDescrLineTo *d = dynamic_cast<PathDescrLineTo *>(descr_cmd[i]);
                lastPos = d->p;
                break;
            }
            case descr_arcto:
            {
                PathDescrArcTo *d = dynamic_cast<PathDescrArcTo *>(descr_cmd[i]);
                lastPos = d->p;
                break;
            }
            case descr_cubicto:
            {
                PathDescrCubicTo *d = dynamic_cast<PathDescrCubicTo *>(descr_cmd[i]);
                lastPos = d->p;
                break;
            }
            case descr_bezierto:
            {
                PathDescrBezierTo *d = dynamic_cast<PathDescrBezierTo *>(descr_cmd[i]);
                lastPos = d->p;
                break;
            }
            case descr_interm_bezier:
            {
                PathDescrIntermBezierTo *d = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[i]);
                lastPos = d->p;
                break;
            }
            default:
                break;
            }
        }
    }

    bool hasMoved = false;
    for (int i = 0; i < int(descr_cmd.size()); i++) {
        int const typ = descr_cmd[i]->getType();
        switch ( typ ) {
        case descr_forced:
            if ( i < int(descr_cmd.size()) - 1 && hasMoved ) { // sinon il termine le chemin

                delete descr_cmd[i];
                descr_cmd[i] = new PathDescrMoveTo(lastSeen);
                lastMove = lastSeen;
                hasMoved = true;
            }
            break;
            
        case descr_moveto:
        {
          PathDescrMoveTo *nData = dynamic_cast<PathDescrMoveTo *>(descr_cmd[i]);
          lastMove = lastSeen = nData->p;
          hasMoved = true;
        }
        break;
      case descr_close:
      {
        lastSeen=lastMove;
      }
        break;        
      case descr_lineto:
      {
        PathDescrLineTo *nData = dynamic_cast<PathDescrLineTo *>(descr_cmd[i]);
        lastSeen=nData->p;
      }
        break;
      case descr_cubicto:
      {
        PathDescrCubicTo *nData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[i]);
        lastSeen=nData->p;
     }
        break;
      case descr_arcto:
      {
        PathDescrArcTo *nData = dynamic_cast<PathDescrArcTo *>(descr_cmd[i]);
        lastSeen=nData->p;
      }
        break;
      case descr_bezierto:
      {
        PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[i]);
        lastSeen=nData->p;
     }
        break;
      case descr_interm_bezier:
      {
        PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[i]);
        lastSeen=nData->p;
      }
        break;
      default:
        break;
    }
  }
}
static int       CmpPosition(const void * p1, const void * p2) {
  Path::cut_position *cp1=(Path::cut_position*)p1;
  Path::cut_position *cp2=(Path::cut_position*)p2;
  if ( cp1->piece < cp2->piece ) return -1;
  if ( cp1->piece > cp2->piece ) return 1;
  if ( cp1->t < cp2->t ) return -1;
  if ( cp1->t > cp2->t ) return 1;
  return 0;
}
static int       CmpCurv(const void * p1, const void * p2) {
  double *cp1=(double*)p1;
  double *cp2=(double*)p2;
  if ( *cp1 < *cp2 ) return -1;
  if ( *cp1 > *cp2 ) return 1;
  return 0;
}


Path::cut_position* Path::CurvilignToPosition(int nbCv, double *cvAbs, int &nbCut)
{
    if ( nbCv <= 0 || pts.empty() || back == false ) {
        return NULL;
    }
  
    qsort(cvAbs, nbCv, sizeof(double), CmpCurv);
  
    cut_position *res = NULL;
    nbCut = 0;
    int curCv = 0;
  
    double len = 0;
    double lastT = 0;
    int lastPiece = -1;

    NR::Point lastM = pts[0].p;
    NR::Point lastP = lastM;

    for (std::vector<path_lineto>::const_iterator i = pts.begin(); i != pts.end(); i++) {

        if ( i->isMoveTo == polyline_moveto ) {

            lastP = lastM = i->p;
            lastT = i->t;
            lastPiece = i->piece;

        } else {
            
            double const add = NR::L2(i->p - lastP);
            double curPos = len;
            double curAdd = add;
            
            while ( curAdd > 0.0001 && curCv < nbCv && curPos + curAdd >= cvAbs[curCv] ) {
                double const theta = (cvAbs[curCv] - len) / add;
                res = (cut_position*) g_realloc(res, (nbCut + 1) * sizeof(cut_position));
                res[nbCut].piece = i->piece;
                res[nbCut].t = theta * i->t + (1 - theta) * ( (lastPiece != i->piece) ? 0 : lastT);
                nbCut++;
                curAdd -= cvAbs[curCv] - curPos;
                curPos = cvAbs[curCv];
                curCv++;
            }
            
            len += add;
            lastPiece = i->piece;
            lastP = i->p;
            lastT = i->t;
        }
    }
    
    return res;
}

/* 
Moved from Layout-TNG-OutIter.cpp
TODO: clean up uses of the original function and remove

Original Comment:
"this function really belongs to Path. I'll probably move it there eventually,
hence the Path-esque coding style"

*/
template<typename T> inline static T square(T x) {return x*x;}
Path::cut_position Path::PointToCurvilignPosition(NR::Point const &pos) const
{
    unsigned bestSeg = 0;
    double bestRangeSquared = DBL_MAX;
    double bestT = 0.0; // you need a sentinel, or make sure that you prime with correct values.

    for (unsigned i = 1 ; i < pts.size() ; i++) {
        if (pts[i].isMoveTo == polyline_moveto) continue;
        NR::Point p1, p2, localPos;
        double thisRangeSquared;
        double t;

        if (pts[i - 1].p == pts[i].p) {
            thisRangeSquared = square(pts[i].p[NR::X] - pos[NR::X]) + square(pts[i].p[NR::Y] - pos[NR::Y]);
            t = 0.0;
        } else {
            // we rotate all our coordinates so we're always looking at a mostly vertical line.
            if (fabs(pts[i - 1].p[NR::X] - pts[i].p[NR::X]) < fabs(pts[i - 1].p[NR::Y] - pts[i].p[NR::Y])) {
                p1 = pts[i - 1].p;
                p2 = pts[i].p;
                localPos = pos;
            } else {
                p1 = pts[i - 1].p.cw();
                p2 = pts[i].p.cw();
                localPos = pos.cw();
            }
            double gradient = (p2[NR::X] - p1[NR::X]) / (p2[NR::Y] - p1[NR::Y]);
            double intersection = p1[NR::X] - gradient * p1[NR::Y];
            /*
              orthogonalGradient = -1.0 / gradient; // you are going to have numerical problems here.
              orthogonalIntersection = localPos[NR::X] - orthogonalGradient * localPos[NR::Y];
              nearestY = (orthogonalIntersection - intersection) / (gradient - orthogonalGradient);

              expand out nearestY fully :
              nearestY = (localPos[NR::X] - (-1.0 / gradient) * localPos[NR::Y] - intersection) / (gradient - (-1.0 / gradient));

              multiply top and bottom by gradient:
              nearestY = (localPos[NR::X] * gradient - (-1.0) * localPos[NR::Y] - intersection * gradient) / (gradient * gradient - (-1.0));

              and simplify to get:
            */
            double nearestY =  (localPos[NR::X] * gradient + localPos[NR::Y] - intersection * gradient)
                             / (gradient * gradient + 1.0);
            t = (nearestY - p1[NR::Y]) / (p2[NR::Y] - p1[NR::Y]);
            if (t <= 0.0) thisRangeSquared = square(p1[NR::X] - localPos[NR::X]) + square(p1[NR::Y] - localPos[NR::Y]);
            else if (t >= 1.0) thisRangeSquared = square(p2[NR::X] - localPos[NR::X]) + square(p2[NR::Y] - localPos[NR::Y]);
            else thisRangeSquared = square(nearestY * gradient + intersection - localPos[NR::X]) + square(nearestY - localPos[NR::Y]);
        }

        if (thisRangeSquared < bestRangeSquared) {
            bestSeg = i;
            bestRangeSquared = thisRangeSquared;
            bestT = t;
        }
    }
    Path::cut_position result;
    if (bestSeg == 0) {
        result.piece = 0;
        result.t = 0.0;
    } else {
        result.piece = pts[bestSeg].piece;
        if (result.piece == pts[bestSeg - 1].piece)
            result.t = pts[bestSeg - 1].t * (1.0 - bestT) + pts[bestSeg].t * bestT;
        else
            result.t = pts[bestSeg].t * bestT;
    }
    return result;
}
/*
    this one also belongs to Path
    returns the length of the path up to the position indicated by t (0..1)

    TODO: clean up uses of the original function and remove

    should this take a cut_position as a parameter?
*/
double Path::PositionToLength(int piece, double t)
{
    double length = 0.0;
    for (unsigned i = 1 ; i < pts.size() ; i++) {
        if (pts[i].isMoveTo == polyline_moveto) continue;
        if (pts[i].piece == piece && t < pts[i].t) {
            length += NR::L2((t - pts[i - 1].t) / (pts[i].t - pts[i - 1].t) * (pts[i].p - pts[i - 1].p));
            break;
        }
        length += NR::L2(pts[i].p - pts[i - 1].p);
    }
    return length;
}

void Path::ConvertPositionsToForced(int nbPos, cut_position *poss)
{
    if ( nbPos <= 0 ) {
        return;
    }
    
    {
        NR::Point lastPos(0, 0);
        for (int i = int(descr_cmd.size()) - 1; i >= 0; i--) {
            int const typ = descr_cmd[i]->getType();
            switch ( typ ) {
                
            case descr_forced:
            {
                PathDescrForced *d = dynamic_cast<PathDescrForced *>(descr_cmd[i]);
                d->p = lastPos;
                break;
            }
                
            case descr_close:
            {
                delete descr_cmd[i];
                descr_cmd[i] = new PathDescrLineTo(NR::Point(0, 0));

                int fp = i - 1;
                while ( fp >= 0 && (descr_cmd[fp]->getType()) != descr_moveto ) {
                    fp--;
                }
                
                if ( fp >= 0 ) {
                    PathDescrMoveTo *oData = dynamic_cast<PathDescrMoveTo *>(descr_cmd[fp]);
                    dynamic_cast<PathDescrLineTo*>(descr_cmd[i])->p = oData->p;
                }
            }
            break;
            
            case descr_bezierto:
            {
                PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[i]);
                NR::Point theP = nData->p;
                if ( nData->nb == 0 ) {
                    lastPos = theP;
                }
            }
            break;
            
        case descr_moveto:
        {
            PathDescrMoveTo *d = dynamic_cast<PathDescrMoveTo *>(descr_cmd[i]);
            lastPos = d->p;
            break;
        }
        case descr_lineto:
        {
            PathDescrLineTo *d = dynamic_cast<PathDescrLineTo *>(descr_cmd[i]);
            lastPos = d->p;
            break;
        }
        case descr_arcto:
        {
            PathDescrArcTo *d = dynamic_cast<PathDescrArcTo *>(descr_cmd[i]);
            lastPos = d->p;
            break;
        }
        case descr_cubicto:
        {
            PathDescrCubicTo *d = dynamic_cast<PathDescrCubicTo *>(descr_cmd[i]);
            lastPos = d->p;
            break;
        }
        case descr_interm_bezier:
        {
            PathDescrIntermBezierTo *d = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[i]);
            lastPos = d->p;
            break;
        }
        default:
          break;
      }
    }
  }

  qsort(poss, nbPos, sizeof(cut_position), CmpPosition);

  for (int curP=0;curP<nbPos;curP++) {
    int   cp=poss[curP].piece;
    if ( cp < 0 || cp >= int(descr_cmd.size()) ) break;
    float ct=poss[curP].t;
    if ( ct < 0 ) continue;
    if ( ct > 1 ) continue;
        
    int const typ = descr_cmd[cp]->getType();
    if ( typ == descr_moveto || typ == descr_forced || typ == descr_close ) {
      // ponctuel= rien a faire
    } else if ( typ == descr_lineto || typ == descr_arcto || typ == descr_cubicto ) {
      // facile: creation d'un morceau et d'un forced -> 2 commandes
      NR::Point        theP;
      NR::Point        theT;
      NR::Point        startP;
      startP=PrevPoint(cp-1);
      if ( typ == descr_cubicto ) {
        double           len,rad;
        NR::Point        stD,enD,endP;
        {
          PathDescrCubicTo *oData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[cp]);
          stD=oData->start;
          enD=oData->end;
          endP=oData->p;
          TangentOnCubAt (ct, startP, *oData,true, theP,theT,len,rad);
        }
        
        theT*=len;
        
        InsertCubicTo(endP,(1-ct)*theT,(1-ct)*enD,cp+1);
        InsertForcePoint(cp+1);
        {
          PathDescrCubicTo *nData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[cp]);
          nData->start=ct*stD;
          nData->end=ct*theT;
          nData->p=theP;
        }
        // decalages dans le tableau des positions de coupe
        for (int j=curP+1;j<nbPos;j++) {
          if ( poss[j].piece == cp ) {
            poss[j].piece+=2;
            poss[j].t=(poss[j].t-ct)/(1-ct);
          } else {
            poss[j].piece+=2;
          }
        }
      } else if ( typ == descr_lineto ) {
        NR::Point        endP;
        {
          PathDescrLineTo *oData = dynamic_cast<PathDescrLineTo *>(descr_cmd[cp]);
          endP=oData->p;
        }

        theP=ct*endP+(1-ct)*startP;
        
        InsertLineTo(endP,cp+1);
        InsertForcePoint(cp+1);
        {
          PathDescrLineTo *nData = dynamic_cast<PathDescrLineTo *>(descr_cmd[cp]);
          nData->p=theP;
        }
        // decalages dans le tableau des positions de coupe
       for (int j=curP+1;j<nbPos;j++) {
          if ( poss[j].piece == cp ) {
            poss[j].piece+=2;
            poss[j].t=(poss[j].t-ct)/(1-ct);
          } else {
            poss[j].piece+=2;
          }
        }
      } else if ( typ == descr_arcto ) {
        NR::Point        endP;
        double           rx,ry,angle;
        bool             clockw,large;
        double   delta=0;
        {
          PathDescrArcTo *oData = dynamic_cast<PathDescrArcTo *>(descr_cmd[cp]);
          endP=oData->p;
          rx=oData->rx;
          ry=oData->ry;
          angle=oData->angle;
          clockw=oData->clockwise;
          large=oData->large;
        }
        {
          double      sang,eang;
          ArcAngles(startP,endP,rx,ry,angle,large,clockw,sang,eang);
          
          if (clockw) {
            if ( sang < eang ) sang += 2*M_PI;
            delta=eang-sang;
          } else {
            if ( sang > eang ) sang -= 2*M_PI;
            delta=eang-sang;
          }
          if ( delta < 0 ) delta=-delta;
        }
        
        PointAt (cp,ct, theP);
        
        if ( delta*(1-ct) > M_PI ) {
          InsertArcTo(endP,rx,ry,angle,true,clockw,cp+1);
        } else {
          InsertArcTo(endP,rx,ry,angle,false,clockw,cp+1);
        }
        InsertForcePoint(cp+1);
        {
          PathDescrArcTo *nData = dynamic_cast<PathDescrArcTo *>(descr_cmd[cp]);
          nData->p=theP;
          if ( delta*ct > M_PI ) {
            nData->clockwise=true;
          } else {
            nData->clockwise=false;
          }
        }
        // decalages dans le tableau des positions de coupe
        for (int j=curP+1;j<nbPos;j++) {
          if ( poss[j].piece == cp ) {
            poss[j].piece+=2;
            poss[j].t=(poss[j].t-ct)/(1-ct);
          } else {
            poss[j].piece+=2;
          }
        }
      }
    } else if ( typ == descr_bezierto || typ == descr_interm_bezier ) {
      // dur
      int theBDI=cp;
      while ( theBDI >= 0 && (descr_cmd[theBDI]->getType()) != descr_bezierto ) theBDI--;
      if ( (descr_cmd[theBDI]->getType()) == descr_bezierto ) {
        PathDescrBezierTo theBD=*(dynamic_cast<PathDescrBezierTo *>(descr_cmd[theBDI]));
        if ( cp >= theBDI && cp < theBDI+theBD.nb ) {
          if ( theBD.nb == 1 ) {
            NR::Point        endP=theBD.p;
            NR::Point        midP;
            NR::Point        startP;
            startP=PrevPoint(theBDI-1);
            {
              PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[theBDI+1]);
              midP=nData->p;
            }
            NR::Point       aP=ct*midP+(1-ct)*startP;
            NR::Point       bP=ct*endP+(1-ct)*midP;
            NR::Point       knotP=ct*bP+(1-ct)*aP;
                        
            InsertIntermBezierTo(bP,theBDI+2);
            InsertBezierTo(knotP,1,theBDI+2);
            InsertForcePoint(theBDI+2);
            {
              PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[theBDI+1]);
              nData->p=aP;
            }
            {
              PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[theBDI]);
              nData->p=knotP;
            }
            // decalages dans le tableau des positions de coupe
            for (int j=curP+1;j<nbPos;j++) {
              if ( poss[j].piece == cp ) {
                poss[j].piece+=3;
                poss[j].t=(poss[j].t-ct)/(1-ct);
              } else {
                poss[j].piece+=3;
              }
            }
            
          } else {
            // decouper puis repasser
            if ( cp > theBDI ) {
              NR::Point   pcP,ncP;
              {
                PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[cp]);
                pcP=nData->p;
              }
              {
                PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[cp+1]);
                ncP=nData->p;
              }
              NR::Point knotP=0.5*(pcP+ncP);
              
              InsertBezierTo(knotP,theBD.nb-(cp-theBDI),cp+1);
              {
                PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[theBDI]);
                nData->nb=cp-theBDI;
              }
              
              // decalages dans le tableau des positions de coupe
              for (int j=curP;j<nbPos;j++) {
                if ( poss[j].piece == cp ) {
                  poss[j].piece+=1;
                } else {
                  poss[j].piece+=1;
                }
              }
              curP--;
            } else {
              NR::Point   pcP,ncP;
              {
                PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[cp+1]);
                pcP=nData->p;
              }
              {
                PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[cp+2]);
                ncP=nData->p;
              }
              NR::Point knotP=0.5*(pcP+ncP);
              
              InsertBezierTo(knotP,theBD.nb-1,cp+2);
              {
                PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[theBDI]);
                nData->nb=1;
              }
              
              // decalages dans le tableau des positions de coupe
              for (int j=curP;j<nbPos;j++) {
                if ( poss[j].piece == cp ) {
//                  poss[j].piece+=1;
                } else {
                  poss[j].piece+=1;
                }
              }
              curP--;
            }
          }
        } else {
          // on laisse aussi tomber
        }
      } else {
        // on laisse tomber
      }
    }
  }
}

void        Path::ConvertPositionsToMoveTo(int nbPos,cut_position* poss)
{
  ConvertPositionsToForced(nbPos,poss);
//  ConvertForcedToMoveTo();
  // on fait une version customizee a la place

  Path*  res=new Path;
  
  NR::Point    lastP(0,0);
  for (int i=0;i<int(descr_cmd.size());i++) {
    int const typ = descr_cmd[i]->getType();
    if ( typ == descr_moveto ) {
      NR::Point  np;
      {
        PathDescrMoveTo *nData = dynamic_cast<PathDescrMoveTo *>(descr_cmd[i]);
        np=nData->p;
      }
      NR::Point  endP;
      bool       hasClose=false;
      int        hasForced=-1;
      bool       doesClose=false;
      int        j=i+1;
      for (;j<int(descr_cmd.size());j++) {
        int const ntyp = descr_cmd[j]->getType();
        if ( ntyp == descr_moveto ) {
          j--;
          break;
        } else if ( ntyp == descr_forced ) {
          if ( hasForced < 0 ) hasForced=j;
        } else if ( ntyp == descr_close ) {
          hasClose=true;
          break;
        } else if ( ntyp == descr_lineto ) {
          PathDescrLineTo *nData = dynamic_cast<PathDescrLineTo *>(descr_cmd[j]);
          endP=nData->p;
        } else if ( ntyp == descr_arcto ) {
          PathDescrArcTo *nData = dynamic_cast<PathDescrArcTo *>(descr_cmd[j]);
          endP=nData->p;
        } else if ( ntyp == descr_cubicto ) {
          PathDescrCubicTo *nData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[j]);
          endP=nData->p;
        } else if ( ntyp == descr_bezierto ) {
          PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[j]);
          endP=nData->p;
        } else {
        }
      }
      if ( NR::LInfty(endP-np) < 0.00001 ) {
        doesClose=true;
      }
      if ( ( doesClose || hasClose ) && hasForced >= 0 ) {
 //       printf("nasty i=%i j=%i frc=%i\n",i,j,hasForced);
        // aghhh.
        NR::Point   nMvtP=PrevPoint(hasForced);
        res->MoveTo(nMvtP);
        NR::Point   nLastP=nMvtP;
        for (int k = hasForced + 1; k < j; k++) {
          int ntyp=descr_cmd[k]->getType();
          if ( ntyp == descr_moveto ) {
            // ne doit pas arriver
          } else if ( ntyp == descr_forced ) {
            res->MoveTo(nLastP);
          } else if ( ntyp == descr_close ) {
            // rien a faire ici; de plus il ne peut y en avoir qu'un
          } else if ( ntyp == descr_lineto ) {
            PathDescrLineTo *nData = dynamic_cast<PathDescrLineTo *>(descr_cmd[k]);
            res->LineTo(nData->p);
            nLastP=nData->p;
          } else if ( ntyp == descr_arcto ) {
            PathDescrArcTo *nData = dynamic_cast<PathDescrArcTo *>(descr_cmd[k]);
            res->ArcTo(nData->p,nData->rx,nData->ry,nData->angle,nData->large,nData->clockwise);
            nLastP=nData->p;
          } else if ( ntyp == descr_cubicto ) {
            PathDescrCubicTo *nData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[k]);
            res->CubicTo(nData->p,nData->start,nData->end);
            nLastP=nData->p;
          } else if ( ntyp == descr_bezierto ) {
            PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[k]);
            res->BezierTo(nData->p);
            nLastP=nData->p;
          } else if ( ntyp == descr_interm_bezier ) {
            PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[k]);
            res->IntermBezierTo(nData->p);
          } else {
          }
        }
        if ( doesClose == false ) res->LineTo(np);
        nLastP=np;
        for (int k=i+1;k<hasForced;k++) {
          int ntyp=descr_cmd[k]->getType();
          if ( ntyp == descr_moveto ) {
            // ne doit pas arriver
          } else if ( ntyp == descr_forced ) {
            res->MoveTo(nLastP);
          } else if ( ntyp == descr_close ) {
            // rien a faire ici; de plus il ne peut y en avoir qu'un
          } else if ( ntyp == descr_lineto ) {
            PathDescrLineTo *nData = dynamic_cast<PathDescrLineTo *>(descr_cmd[k]);
            res->LineTo(nData->p);
            nLastP=nData->p;
          } else if ( ntyp == descr_arcto ) {
            PathDescrArcTo *nData = dynamic_cast<PathDescrArcTo *>(descr_cmd[k]);
            res->ArcTo(nData->p,nData->rx,nData->ry,nData->angle,nData->large,nData->clockwise);
            nLastP=nData->p;
          } else if ( ntyp == descr_cubicto ) {
            PathDescrCubicTo *nData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[k]);
            res->CubicTo(nData->p,nData->start,nData->end);
            nLastP=nData->p;
          } else if ( ntyp == descr_bezierto ) {
            PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[k]);
            res->BezierTo(nData->p);
            nLastP=nData->p;
          } else if ( ntyp == descr_interm_bezier ) {
            PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[k]);
            res->IntermBezierTo(nData->p);
          } else {
          }
        }
        lastP=nMvtP;
        i=j;
      } else {
        // regular, just move on
        res->MoveTo(np);
        lastP=np;
      }
    } else if ( typ == descr_close ) {
      res->Close();
    } else if ( typ == descr_forced ) {
      res->MoveTo(lastP);
    } else if ( typ == descr_lineto ) {
      PathDescrLineTo *nData = dynamic_cast<PathDescrLineTo *>(descr_cmd[i]);
      res->LineTo(nData->p);
      lastP=nData->p;
    } else if ( typ == descr_arcto ) {
      PathDescrArcTo *nData = dynamic_cast<PathDescrArcTo *>(descr_cmd[i]);
      res->ArcTo(nData->p,nData->rx,nData->ry,nData->angle,nData->large,nData->clockwise);
      lastP=nData->p;
    } else if ( typ == descr_cubicto ) {
      PathDescrCubicTo *nData = dynamic_cast<PathDescrCubicTo *>(descr_cmd[i]);
      res->CubicTo(nData->p,nData->start,nData->end);
      lastP=nData->p;
    } else if ( typ == descr_bezierto ) {
      PathDescrBezierTo *nData = dynamic_cast<PathDescrBezierTo *>(descr_cmd[i]);
      res->BezierTo(nData->p);
      lastP=nData->p;
    } else if ( typ == descr_interm_bezier ) {
      PathDescrIntermBezierTo *nData = dynamic_cast<PathDescrIntermBezierTo *>(descr_cmd[i]);
      res->IntermBezierTo(nData->p);
    } else {
    }
  }

  Copy(res);
  delete res;
  return;
}

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
