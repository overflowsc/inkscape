/*
 * Our nice measuring tool
 *
 * Authors:
 *   Felipe Correa da Silva Sanches <juca@members.fsf.org>
 *
 * Copyright (C) 2011 Authors
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */


#include <gdk/gdkkeysyms.h>

#include "macros.h"
#include "display/curve.h"
#include "sp-shape.h"
#include "display/sp-ctrlline.h"
#include "display/sodipodi-ctrl.h"
#include "display/sp-canvas-item.h"
#include "display/sp-canvas-util.h"
#include "desktop.h"
#include "document.h"
#include "pixmaps/cursor-measure.xpm"
#include "preferences.h"
#include "inkscape.h"
#include "desktop-handles.h"
#include "measure-context.h"
#include "display/canvas-text.h"
#include "path-chemistry.h"
#include "2geom/line.h"
#include <2geom/path-intersection.h>
#include <2geom/pathvector.h>
#include <2geom/crossing.h>

static void sp_measure_context_class_init(SPMeasureContextClass *klass);
static void sp_measure_context_init(SPMeasureContext *measure_context);
static void sp_measure_context_setup(SPEventContext *ec);
static void sp_measure_context_finish (SPEventContext *ec);

static gint sp_measure_context_root_handler(SPEventContext *event_context, GdkEvent *event);
static gint sp_measure_context_item_handler(SPEventContext *event_context, SPItem *item, GdkEvent *event);

static SPEventContextClass *parent_class;

static gint xp = 0, yp = 0; // where drag started
static gint tolerance = 0;
static bool within_tolerance = false;
static SPCanvasItem * line = NULL;
Geom::Point start_point;
std::vector<Inkscape::Display::TemporaryItem*> measure_tmp_items;

GType sp_measure_context_get_type(void)
{
    static GType type = 0;

    if (!type) {
        GTypeInfo info = {
            sizeof(SPMeasureContextClass),
            NULL, NULL,
            (GClassInitFunc) sp_measure_context_class_init,
            NULL, NULL,
            sizeof(SPMeasureContext),
            4,
            (GInstanceInitFunc) sp_measure_context_init,
            NULL,	/* value_table */
        };
        type = g_type_register_static(SP_TYPE_EVENT_CONTEXT, "SPMeasureContext", &info, (GTypeFlags) 0);
    }

    return type;
}

static void sp_measure_context_class_init(SPMeasureContextClass *klass)
{
    SPEventContextClass *event_context_class = (SPEventContextClass *) klass;

    parent_class = (SPEventContextClass*) g_type_class_peek_parent(klass);

    event_context_class->setup = sp_measure_context_setup;
    event_context_class->finish = sp_measure_context_finish;

    event_context_class->root_handler = sp_measure_context_root_handler;
    event_context_class->item_handler = sp_measure_context_item_handler;
}

static void sp_measure_context_init (SPMeasureContext *measure_context)
{
    SPEventContext *event_context = SP_EVENT_CONTEXT(measure_context);

    event_context->cursor_shape = cursor_measure_xpm;
    event_context->hot_x = 4;
    event_context->hot_y = 4;
}

static void
sp_measure_context_finish (SPEventContext *ec)
{
	SPMeasureContext *mc = SP_MEASURE_CONTEXT(ec);
	
	ec->enableGrDrag(false);

    if (mc->grabbed) {
        sp_canvas_item_ungrab(mc->grabbed, GDK_CURRENT_TIME);
        mc->grabbed = NULL;
    }
}

static void sp_measure_context_setup(SPEventContext *ec)
{
    if (((SPEventContextClass *) parent_class)->setup) {
        ((SPEventContextClass *) parent_class)->setup(ec);
    }
}

static gint sp_measure_context_item_handler(SPEventContext *event_context, SPItem *item, GdkEvent *event)
{
    gint ret = FALSE;

    if (((SPEventContextClass *) parent_class)->item_handler) {
        ret = ((SPEventContextClass *) parent_class)->item_handler (event_context, item, event);
    }

    return ret;
}

bool GeomPointSortPredicate(const Geom::Point& p1, const Geom::Point& p2)
{
  return p1[Geom::Y] < p2[Geom::Y];
}

static gint sp_measure_context_root_handler(SPEventContext *event_context, GdkEvent *event)
{
    SPDesktop *desktop = event_context->desktop;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    tolerance = prefs->getIntLimited("/options/dragtolerance/value", 0, 0, 100);
	
    SPMeasureContext *mc = SP_MEASURE_CONTEXT(event_context);
    gint ret = FALSE;

    switch (event->type) {
        case GDK_BUTTON_PRESS:
        {
            Geom::Point const button_w(event->button.x, event->button.y);
            start_point = desktop->w2d(button_w);
            if (event->button.button == 1 && !event_context->space_panning) {
                // save drag origin
                xp = (gint) event->button.x;
                yp = (gint) event->button.y;
                within_tolerance = true;

                ret = TRUE;
            }

            if (!line){
                SPDesktop *desktop = inkscape_active_desktop();
                line = sp_canvas_item_new(sp_desktop_controls(desktop), SP_TYPE_CTRLLINE, NULL);
            }

            sp_ctrlline_set_coords (SP_CTRLLINE(line), start_point, start_point);
            sp_canvas_item_show (line);

            sp_canvas_item_grab(SP_CANVAS_ITEM(desktop->acetate),
								GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK | GDK_BUTTON_PRESS_MASK,
								NULL, event->button.time);
            mc->grabbed = SP_CANVAS_ITEM(desktop->acetate);
            break;
        }

	case GDK_MOTION_NOTIFY:
        {
            if (event->motion.state & GDK_BUTTON1_MASK && !event_context->space_panning) {
                ret = TRUE;

                if ( within_tolerance
                     && ( abs( (gint) event->motion.x - xp ) < tolerance )
                     && ( abs( (gint) event->motion.y - yp ) < tolerance ) ) {
                    break; // do not drag if we're within tolerance from origin
                }
                // Once the user has moved farther than tolerance from the original location
                // (indicating they intend to move the object, not click), then always process the
                // motion notify coordinates as given (no snapping back to origin)
                within_tolerance = false;

                Geom::Point const motion_w(event->motion.x, event->motion.y);
                Geom::Point const motion_dt(desktop->w2d(motion_w));

                sp_ctrlline_set_coords (SP_CTRLLINE(line), start_point[Geom::X], start_point[Geom::Y], motion_dt[Geom::X], motion_dt[Geom::Y]);

                Geom::PathVector lineseg;
                Geom::Path p;
                p.start(desktop->dt2doc(start_point));
                p.appendNew<Geom::LineSegment>(desktop->dt2doc(motion_dt));
                lineseg.push_back(p);

                double deltax = motion_dt[Geom::X] - start_point[Geom::X];
                double deltay = motion_dt[Geom::Y] - start_point[Geom::Y];
                double angle = atan2(deltay, deltax);

//TODO: calculate NPOINTS
//800 seems to be a good value for 800x600 resolution
#define NPOINTS 800

                std::vector<Geom::Point> points;
                double i;
                for (i=0; i<NPOINTS; i++){
                    points.push_back(desktop->d2w(start_point + (i/NPOINTS)*(motion_dt-start_point)));
                }

                //select elements crossed by line segment:
                GSList *items = sp_desktop_document(desktop)->getItemsAtPoints(desktop->dkey, points);
                SPItem* item;
                GSList *l;
                int counter=0;
                std::vector<Geom::Point> intersections;
                Inkscape::Preferences *prefs = Inkscape::Preferences::get();
                bool ignore_1st_and_last = prefs->getBool("/tools/measure/ignore_1st_and_last", true);

                if (!ignore_1st_and_last){
                    intersections.push_back(desktop->dt2doc(start_point));
                }

                for (l = items; l != NULL; l = l->next){
                    item = (SPItem*) (l->data);
#if 0
//TODO: deal with all kinds of objects:

                    Inkscape::XML::Node *repr = sp_selected_item_to_curved_repr(item, 0);

                    if (!repr) continue;
                    item = (SPItem *) doc->getObjectByRepr(repr);
                    if (!item) continue;
                    SPCurve* curve = SP_SHAPE(item)->getCurve();
#else
                    SPCurve* curve = NULL;
                    if (SP_IS_SHAPE(item)) {
                        curve = SP_SHAPE(item)->getCurve();
                    } 
#endif
                    if (!curve) continue;
                    counter++;

                    Geom::PathVector pathv = curve->get_pathvector();

                    // Find all intersections of the control-line with this shape
                    Geom::CrossingSet cs = Geom::crossings(lineseg, pathv);
                    // Store the results as intersection points
                    unsigned int index = 0;
                    for (Geom::CrossingSet::const_iterator i = cs.begin(); i != cs.end(); i++) {
                        if (index >= lineseg.size()) {
                            break;
                        }
                        // Reconstruct and store the points of intersection
                        for (Geom::Crossings::const_iterator m = (*i).begin(); m != (*i).end(); m++) {
                            intersections.push_back(lineseg[index].pointAt((*m).ta));
                        }
                        index++;
                    }
                    //g_free(repr);
                }

                if (!ignore_1st_and_last){
                    intersections.push_back(desktop->dt2doc(motion_dt));
                }

                //sort intersections
                if (intersections.size()>2){
                    std::sort(intersections.begin(), intersections.end(), GeomPointSortPredicate);
                }

                unsigned int idx;
                for (idx=0; idx<measure_tmp_items.size(); idx++){
                    desktop->remove_temporary_canvasitem(measure_tmp_items[idx]);
                }
                measure_tmp_items.clear();

                for (idx=0;idx<intersections.size(); idx++){
                    // Display the intersection indicator (i.e. the cross)
                    SPCanvasItem * canvasitem = NULL;
                    canvasitem = sp_canvas_item_new(sp_desktop_tempgroup (desktop),
                                                    SP_TYPE_CTRL,
                                                    "anchor", GTK_ANCHOR_CENTER,
                                                    "size", 8.0,
                                                    "stroked", TRUE,
                                                    "stroke_color", 0xff0000ff,
                                                    "mode", SP_KNOT_MODE_XOR,
                                                    "shape", SP_KNOT_SHAPE_CROSS,
                                                    NULL );

                    SP_CTRL(canvasitem)->moveto(desktop->doc2dt(intersections[idx]));
                    measure_tmp_items.push_back(desktop->add_temporary_canvasitem(canvasitem, 0));

                }

                double fontsize = prefs->getInt("/tools/measure/fontsize");

                Geom::Point previous_point;
                if (intersections.size()>0)
                    previous_point = intersections[0];

                for (idx=1; idx < intersections.size(); idx++){
                    Geom::Point measure_text_pos = (previous_point + intersections[idx])/2;
//TODO: shift label a few pixels in the y coordinate

                    char* measure_str = (char*) malloc(sizeof(char)*20);
                    sprintf(measure_str, "%.2f", (intersections[idx] - previous_point).length());
                    SPCanvasItem *canvas_tooltip = sp_canvastext_new(sp_desktop_tempgroup(desktop), desktop, desktop->dt2doc(measure_text_pos), measure_str);

                    sp_canvastext_set_fontsize (SP_CANVASTEXT(canvas_tooltip), fontsize);

                    measure_tmp_items.push_back(desktop->add_temporary_canvasitem(canvas_tooltip, 0));
                    free(measure_str);
                    previous_point = intersections[idx];
                }

                char* angle_str = (char*) malloc(sizeof(char)*20);
                sprintf(angle_str, "%.2f °", angle * 180/3.1415 );
                SPCanvasItem *canvas_tooltip = sp_canvastext_new(sp_desktop_tempgroup(desktop), desktop, motion_dt + desktop->w2d(Geom::Point(50,0)), angle_str);
                sp_canvastext_set_fontsize (SP_CANVASTEXT(canvas_tooltip), fontsize);
                sp_canvastext_set_rgba32 (SP_CANVASTEXT(canvas_tooltip), 0x337f33ff, 0xffffffff);

                measure_tmp_items.push_back(desktop->add_temporary_canvasitem(canvas_tooltip, 0));
                free(angle_str);

                gobble_motion_events(GDK_BUTTON1_MASK);
            }
            break;
        }

      	case GDK_BUTTON_RELEASE:
        {
            if (line){
                sp_canvas_item_hide(line);
            }

            unsigned int idx;
            for (idx=0; idx<measure_tmp_items.size(); idx++){
                desktop->remove_temporary_canvasitem(measure_tmp_items[idx]);
            }
            measure_tmp_items.clear();

            if (mc->grabbed) {
                sp_canvas_item_ungrab(mc->grabbed, event->button.time);
                mc->grabbed = NULL;
            }
            xp = yp = 0;
            break;
        }
	default:
            break;
    }

    if (!ret) {
        if (((SPEventContextClass *) parent_class)->root_handler) {
            ret = ((SPEventContextClass *) parent_class)->root_handler(event_context, event);
        }
    }

    return ret;
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :