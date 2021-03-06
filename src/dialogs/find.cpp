/**
 * @file
 * Find dialog.
 */
/* Authors:
 *   bulia byak <bulia@users.sf.net>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Abhishek Sharma
 *
 * Copyright (C) 2004 Authors
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include "widgets/icon.h"
#include "message-stack.h"

//TODO  : delete this
GtkWidget * sp_find_dialog_old (void);

static void
//GtkWidget *
sp_find_dialog(){
    // DialogFind::get().present();
    sp_find_dialog_old ();
    return;
}


#include <gtk/gtk.h>

#include <glibmm/i18n.h>
#include "helper/window.h"
#include "macros.h"
#include "inkscape.h"
#include "document.h"
#include "desktop.h"
#include "selection.h"
#include "desktop-handles.h"

#include "dialog-events.h"
#include "../preferences.h"
#include "../verbs.h"
#include "../interface.h"
#include "../sp-text.h"
#include "../sp-flowtext.h"
#include "../text-editing.h"
#include "../sp-tspan.h"
#include "../sp-tref.h"
#include "../selection-chemistry.h"
#include "../sp-defs.h"
#include "../sp-rect.h"
#include "../sp-ellipse.h"
#include "../sp-star.h"
#include "../sp-spiral.h"
#include "../sp-path.h"
#include "../sp-line.h"
#include "../sp-polyline.h"
#include "../sp-item-group.h"
#include "../sp-use.h"
#include "../sp-image.h"
#include "../sp-offset.h"
#include <xml/repr.h>
#include "sp-root.h"

#define MIN_ONSCREEN_DISTANCE 50

static GtkWidget *dlg = NULL;
static win_data wd;

// impossible original values to make sure they are read from prefs
static gint x = -1000, y = -1000, w = 0, h = 0;
static Glib::ustring const prefs_path = "/dialogs/find/";


static void sp_find_dialog_destroy(GObject *object, gpointer)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setInt(prefs_path + "visible", 0);

    sp_signal_disconnect_by_data (INKSCAPE, object);
    wd.win = dlg = NULL;
    wd.stop = 0;
}



static gboolean sp_find_dialog_delete(GObject *, GdkEvent *, gpointer /*data*/)
{
    gtk_window_get_position (GTK_WINDOW (dlg), &x, &y);
    gtk_window_get_size (GTK_WINDOW (dlg), &w, &h);

    if (x<0) x=0;
    if (y<0) y=0;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setInt(prefs_path + "x", x);
    prefs->setInt(prefs_path + "y", y);
    prefs->setInt(prefs_path + "w", w);
    prefs->setInt(prefs_path + "h", h);

    return FALSE; // which means, go ahead and destroy it
}

static void
sp_find_squeeze_window()
{
    GtkRequisition r;
#if GTK_CHECK_VERSION(3,0,0)
    gtk_widget_get_preferred_size(dlg, &r, NULL);
#else
    gtk_widget_size_request(dlg, &r);
#endif
    gtk_window_resize ((GtkWindow *) dlg, r.width, r.height);
}

static bool
item_id_match (SPItem *item, const gchar *id, bool exact)
{
    if (item->getRepr() == NULL) {
        return false;
    }

    if (SP_IS_STRING(item)) { // SPStrings have "on demand" ids which are useless for searching
        return false;
    }

    const gchar *item_id = item->getRepr()->attribute("id");
    if (item_id == NULL) {
        return false;
    }

    if (exact) {
        return ((bool) !strcmp(item_id, id));
    } else {
//        g_print ("strstr: %s %s: %s\n", item_id, id, strstr(item_id, id) != NULL? "yes":"no");
        return ((bool) (strstr(item_id, id) != NULL));
    }
}

static bool
item_text_match (SPItem *item, const gchar *text, bool exact)
{
    if (item->getRepr() == NULL) {
        return false;
    }

    if (SP_IS_TEXT(item) || SP_IS_FLOWTEXT(item)) {
        const gchar *item_text = sp_te_get_string_multiline (item);
        if (item_text == NULL)
            return false;
        bool ret;
        if (exact) {
            ret = ((bool) !strcasecmp(item_text, text));
        } else {
            //FIXME: strcasestr
            ret = ((bool) (strstr(item_text, text) != NULL));
        }
        g_free ((void*) item_text);
        return ret;
    }
    return false;
}

static bool
item_style_match (SPItem *item, const gchar *text, bool exact)
{
    if (item->getRepr() == NULL) {
        return false;
    }

    const gchar *item_text = item->getRepr()->attribute("style");
    if (item_text == NULL) {
        return false;
    }

    if (exact) {
        return ((bool) !strcmp(item_text, text));
    } else {
        return ((bool) (strstr(item_text, text) != NULL));
    }
}

static bool
item_attr_match(SPItem *item, const gchar *name, bool exact)
{
    bool result = false;
    if (item->getRepr()) {
        if (exact) {
            const gchar *attr_value = item->getRepr()->attribute(name);
            result =  (attr_value != NULL);
        } else {
            result = item->getRepr()->matchAttributeName(name);
        }
    }
    return result;
}


static GSList *
filter_onefield (GSList *l, GObject *dlg, const gchar *field, bool (*match_function)(SPItem *, const gchar *, bool), bool exact)
{
    GtkWidget *widget = GTK_WIDGET (g_object_get_data(G_OBJECT (dlg), field));
    const gchar *text = gtk_entry_get_text (GTK_ENTRY(widget));

    if (strlen (text) != 0) {
        GSList *n = NULL;
        for (GSList *i = l; i != NULL; i = i->next) {
            if (match_function (SP_ITEM(i->data), text, exact)) {
                n = g_slist_prepend (n, i->data);
            }
        }
        return n;
    } else {
        return l;
    }

    return NULL;
}


static bool
type_checkbox (GtkWidget *widget, const gchar *data)
{
    return  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (widget), data)));
}

static bool
item_type_match (SPItem *item, GtkWidget *widget)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;

    if (SP_IS_RECT(item)) {
        return (type_checkbox (widget, "shapes") || type_checkbox (widget, "rects"));

    } else if (SP_IS_GENERICELLIPSE(item) || SP_IS_ELLIPSE(item) || SP_IS_ARC(item) || SP_IS_CIRCLE(item)) {
        return (type_checkbox (widget, "shapes") || type_checkbox (widget, "ellipses"));

    } else if (SP_IS_STAR(item) || SP_IS_POLYGON(item)) {
        return (type_checkbox (widget, "shapes") || type_checkbox (widget, "stars"));

    } else if (SP_IS_SPIRAL(item)) {
        return (type_checkbox (widget, "shapes") || type_checkbox (widget, "spirals"));

    } else if (SP_IS_PATH(item) || SP_IS_LINE(item) || SP_IS_POLYLINE(item)) {
        return (type_checkbox (widget, "paths"));

    } else if (SP_IS_TEXT(item) || SP_IS_TSPAN(item) || SP_IS_TREF(item) || SP_IS_STRING(item)) {
        return (type_checkbox (widget, "texts"));

    } else if (SP_IS_GROUP(item) && !desktop->isLayer(item) ) { // never select layers!
        return (type_checkbox (widget, "groups"));

    } else if (SP_IS_USE(item)) {
        return (type_checkbox (widget, "clones"));

    } else if (SP_IS_IMAGE(item)) {
        return (type_checkbox (widget, "images"));

    } else if (SP_IS_OFFSET(item)) {
        return (type_checkbox (widget, "offsets"));
    }

    return false;
}

static GSList *
filter_types (GSList *l, GObject *dlg, bool (*match_function)(SPItem *, GtkWidget *))
{
    GtkWidget *widget = GTK_WIDGET (g_object_get_data(G_OBJECT (dlg), "types"));

    GtkWidget *alltypes = GTK_WIDGET (g_object_get_data(G_OBJECT (widget), "all"));
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (alltypes)))
        return l;


    GSList *n = NULL;
    for (GSList *i = l; i != NULL; i = i->next) {
        if (match_function (SP_ITEM(i->data), widget)) {
            n = g_slist_prepend (n, i->data);
        }
    }
    return n;
}


static GSList *
filter_list (GSList *l, GObject *dlg, bool exact)
{
    l = filter_onefield (l, dlg, "text", item_text_match, exact);
    l = filter_onefield (l, dlg, "id", item_id_match, exact);
    l = filter_onefield (l, dlg, "style", item_style_match, exact);
    l = filter_onefield (l, dlg, "attr", item_attr_match, exact);

    l = filter_types (l, dlg, item_type_match);

    return l;
}

static GSList *
all_items (SPObject *r, GSList *l, bool hidden, bool locked)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;

    if (SP_IS_DEFS(r)) {
        return l; // we're not interested in items in defs
    }

    if (!strcmp(r->getRepr()->name(), "svg:metadata")) {
        return l; // we're not interested in metadata
    }

    for (SPObject *child = r->firstChild(); child; child = child->next) {
        if ( SP_IS_ITEM(child) && !child->cloned && !desktop->isLayer(child) ) {
            SPItem *item = SP_ITEM(child);
            if ((hidden || !desktop->itemIsHidden(item)) && (locked || !item->isLocked())) {
                l = g_slist_prepend (l, child);
            }
        }
        l = all_items (child, l, hidden, locked);
    }
    return l;
}

static GSList *
all_selection_items (Inkscape::Selection *s, GSList *l, SPObject *ancestor, bool hidden, bool locked)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;

   for (GSList *i = (GSList *) s->itemList(); i != NULL; i = i->next) {
        if ( SP_IS_ITEM(i->data) && !reinterpret_cast<SPObject*>(i->data)->cloned && !desktop->isLayer(SP_ITEM(i->data))) {
            SPItem * item = SP_ITEM(i->data);
            if (!ancestor || ancestor->isAncestorOf(item)) {
                if ((hidden || !desktop->itemIsHidden(item)) && (locked || !item->isLocked())) {
                    l = g_slist_prepend (l, i->data);
                }
            }
        }
        if (!ancestor || ancestor->isAncestorOf(SP_OBJECT (i->data))) {
            l = all_items (SP_OBJECT (i->data), l, hidden, locked);
        }
    }
    return l;
}


static void
sp_find_dialog_find(GObject *, GObject *dlg)
{
    SPDesktop *desktop = SP_ACTIVE_DESKTOP;

    bool hidden = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (dlg), "includehidden")));
    bool locked = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (dlg), "includelocked")));

    GSList *l = NULL;
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (dlg), "inselection")))) {
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (dlg), "inlayer")))) {
            l = all_selection_items (desktop->selection, l, desktop->currentLayer(), hidden, locked);
        } else {
            l = all_selection_items (desktop->selection, l, NULL, hidden, locked);
        }
    } else {
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (dlg), "inlayer")))) {
            l = all_items (desktop->currentLayer(), l, hidden, locked);
        } else {
            l = all_items(sp_desktop_document(desktop)->getRoot(), l, hidden, locked);
        }
    }
    guint all = g_slist_length (l);

    bool exact = true;
    GSList *n = NULL;
    n = filter_list (l, dlg, exact);
    if (n == NULL) {
        exact = false;
        n = filter_list (l, dlg, exact);
    }

    if (n != NULL) {
        int count = g_slist_length (n);
        desktop->messageStack()->flashF(Inkscape::NORMAL_MESSAGE,
                                        // TRANSLATORS: "%s" is replaced with "exact" or "partial" when this string is displayed
                                        ngettext("<b>%d</b> object found (out of <b>%d</b>), %s match.",
                                                 "<b>%d</b> objects found (out of <b>%d</b>), %s match.",
                                                 count),
                                        count, all, exact? _("exact") : _("partial"));

        Inkscape::Selection *selection = sp_desktop_selection (desktop);
        selection->clear();
        selection->setList(n);
        scroll_to_show_item (desktop, SP_ITEM(n->data));
    } else {
        desktop->messageStack()->flash(Inkscape::WARNING_MESSAGE, _("No objects found"));
    }
}

static void
sp_find_reset_searchfield (GObject *dlg, const gchar *field)
{
    GtkWidget *widget = GTK_WIDGET (g_object_get_data(G_OBJECT (dlg), field));
    gtk_entry_set_text (GTK_ENTRY(widget), "");
}


static void
sp_find_dialog_reset (GObject *, GObject *dlg)
{
    sp_find_reset_searchfield (dlg, "text");
    sp_find_reset_searchfield (dlg, "id");
    sp_find_reset_searchfield (dlg, "style");
    sp_find_reset_searchfield (dlg, "attr");

    GtkWidget *types = GTK_WIDGET (g_object_get_data(G_OBJECT (dlg), "types"));
    GtkToggleButton *tb = GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (types), "all"));
    gtk_toggle_button_toggled (tb);
    gtk_toggle_button_set_active (tb, TRUE);
}


#define FIND_LABELWIDTH 80

static void
sp_find_new_searchfield (GtkWidget *dlg, GtkWidget *vb, const gchar *label, const gchar *id, const gchar *tip)
{
#if GTK_CHECK_VERSION(3,0,0)
    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(hb), FALSE);
#else
    GtkWidget *hb = gtk_hbox_new (FALSE, 0);
#endif
    GtkWidget *l = gtk_label_new_with_mnemonic (label);
    gtk_widget_set_size_request (l, FIND_LABELWIDTH, -1);
    gtk_misc_set_alignment (GTK_MISC (l), 1.0, 0.5);
    gtk_box_pack_start (GTK_BOX (hb), l, FALSE, FALSE, 0);

    GtkWidget *tf = gtk_entry_new ();
    gtk_entry_set_max_length (GTK_ENTRY (tf), 64);
    gtk_box_pack_start (GTK_BOX (hb), tf, TRUE, TRUE, 0);
    g_object_set_data (G_OBJECT (dlg), id, tf);
    gtk_widget_set_tooltip_text (tf, tip);
    g_signal_connect ( G_OBJECT (tf), "activate", G_CALLBACK (sp_find_dialog_find), dlg );
    gtk_label_set_mnemonic_widget   (GTK_LABEL(l), tf);

    gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);
}

static void
sp_find_new_button (GtkWidget *dlg, GtkWidget *hb, const gchar *label, const gchar *tip, void (*function) (GObject *, GObject *))
{
    GtkWidget *b = gtk_button_new_with_mnemonic (label);
    gtk_widget_set_tooltip_text (b, tip);
    gtk_box_pack_start (GTK_BOX (hb), b, TRUE, TRUE, 0);
    g_signal_connect ( G_OBJECT (b), "clicked", G_CALLBACK (function), dlg );
    gtk_widget_show (b);
}

static void
toggle_alltypes (GtkToggleButton *tb, gpointer data)
{
    GtkWidget *alltypes_pane =  GTK_WIDGET (g_object_get_data(G_OBJECT (data), "all-pane"));
    if (gtk_toggle_button_get_active (tb)) {
        gtk_widget_hide (alltypes_pane);
    } else {
        gtk_widget_show_all (alltypes_pane);

        // excplicit toggle to make sure its handler gets called, no matter what was the original state
        gtk_toggle_button_toggled (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "shapes")));
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "shapes")), TRUE);

        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "paths")), TRUE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "texts")), TRUE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "groups")), TRUE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "clones")), TRUE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "images")), TRUE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "offsets")), TRUE);
    }
    sp_find_squeeze_window();
}

static void
toggle_shapes (GtkToggleButton *tb, gpointer data)
{
    GtkWidget *shapes_pane =  GTK_WIDGET (g_object_get_data(G_OBJECT (data), "shapes-pane"));
    if (gtk_toggle_button_get_active (tb)) {
        gtk_widget_hide (shapes_pane);
    } else {
        gtk_widget_show_all (shapes_pane);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "rects")), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "ellipses")), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "stars")), FALSE);
        gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (g_object_get_data(G_OBJECT (data), "spirals")), FALSE);
    }
    sp_find_squeeze_window();
}


static GtkWidget *
sp_find_types_checkbox (GtkWidget *w, const gchar *data, gboolean active,
                        const gchar *tip,
                        const gchar *label,
                        void (*toggled)(GtkToggleButton *, gpointer))
{
#if GTK_CHECK_VERSION(3,0,0)
    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(hb), FALSE);
#else
    GtkWidget *hb = gtk_hbox_new (FALSE, 0);
#endif
    gtk_widget_show (hb);

    {
        GtkWidget *b  = gtk_check_button_new_with_label (label);
        gtk_widget_show (b);
        gtk_toggle_button_set_active ((GtkToggleButton *) b, active);
        g_object_set_data (G_OBJECT (w), data, b);
        gtk_widget_set_tooltip_text (b, tip);
        if (toggled)
            g_signal_connect (G_OBJECT (b), "toggled", G_CALLBACK (toggled), w);
        gtk_box_pack_start (GTK_BOX (hb), b, FALSE, FALSE, 0);
    }

    return hb;
}

static GtkWidget *
sp_find_types_checkbox_indented (GtkWidget *w, const gchar *data, gboolean active,
                                 const gchar *tip,
                                 const gchar *label,
                                 void (*toggled)(GtkToggleButton *, gpointer), guint indent)
{
#if GTK_CHECK_VERSION(3,0,0)
    GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_set_homogeneous(GTK_BOX(hb), FALSE);
#else
    GtkWidget *hb = gtk_hbox_new (FALSE, 0);
#endif
    gtk_widget_show (hb);

    { // empty label for indent
        GtkWidget *l = gtk_label_new ("");
        gtk_widget_show (l);
        gtk_widget_set_size_request (l, FIND_LABELWIDTH + indent, -1);
        gtk_box_pack_start (GTK_BOX (hb), l, FALSE, FALSE, 0);
    }

    GtkWidget *c = sp_find_types_checkbox (w, data, active, tip, label, toggled);
    gtk_box_pack_start (GTK_BOX (hb), c, FALSE, FALSE, 0);

    return hb;
}


static GtkWidget *
sp_find_types ()
{
#if GTK_CHECK_VERSION(3,0,0)
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_set_homogeneous(GTK_BOX(vb), FALSE);
#else
    GtkWidget *vb = gtk_vbox_new (FALSE, 4);
#endif
    gtk_widget_show (vb);

    {
#if GTK_CHECK_VERSION(3,0,0)
	GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_set_homogeneous(GTK_BOX(hb), FALSE);
#else
        GtkWidget *hb = gtk_hbox_new (FALSE, 0);
#endif
        gtk_widget_show (hb);

        {
            GtkWidget *l = gtk_label_new_with_mnemonic (_("T_ype: "));
            gtk_widget_show (l);
            gtk_widget_set_size_request (l, FIND_LABELWIDTH, -1);
            gtk_misc_set_alignment (GTK_MISC (l), 1.0, 0.5);
            gtk_box_pack_start (GTK_BOX (hb), l, FALSE, FALSE, 0);
        }

        GtkWidget *alltypes = sp_find_types_checkbox (vb, "all", TRUE, _("Search in all object types"), _("All types"), toggle_alltypes);
        gtk_box_pack_start (GTK_BOX (hb), alltypes, FALSE, FALSE, 0);

        gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);
    }

    {
#if GTK_CHECK_VERSION(3,0,0)
        GtkWidget *vb_all = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_set_homogeneous(GTK_BOX(vb_all), FALSE);
#else
        GtkWidget *vb_all = gtk_vbox_new (FALSE, 0);
#endif
        gtk_widget_show (vb_all);

        {
            GtkWidget *c = sp_find_types_checkbox_indented (vb, "shapes", FALSE, _("Search all shapes"), _("All shapes"), toggle_shapes, 10);
            gtk_box_pack_start (GTK_BOX (vb_all), c, FALSE, FALSE, 0);
        }


        {
#if GTK_CHECK_VERSION(3,0,0)
            GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_box_set_homogeneous(GTK_BOX(hb), FALSE);
#else
            GtkWidget *hb = gtk_hbox_new (FALSE, 0);
#endif
            gtk_widget_show (hb);

            { // empty label for alignment
                GtkWidget *l = gtk_label_new ("");
                gtk_widget_show (l);
                gtk_widget_set_size_request (l, FIND_LABELWIDTH + 20, -1);
                gtk_box_pack_start (GTK_BOX (hb), l, FALSE, FALSE, 0);
            }

            {
                GtkWidget *c = sp_find_types_checkbox (vb, "rects", FALSE, _("Search rectangles"), _("Rectangles"), NULL);
                gtk_box_pack_start (GTK_BOX (hb), c, FALSE, FALSE, 0);
            }

            {
                GtkWidget *c = sp_find_types_checkbox (vb, "ellipses", FALSE, _("Search ellipses, arcs, circles"), _("Ellipses"), NULL);
                gtk_box_pack_start (GTK_BOX (hb), c, FALSE, FALSE, 0);
            }

            {
                GtkWidget *c = sp_find_types_checkbox (vb, "stars", FALSE, _("Search stars and polygons"), _("Stars"), NULL);
                gtk_box_pack_start (GTK_BOX (hb), c, FALSE, FALSE, 0);
            }

            {
                GtkWidget *c = sp_find_types_checkbox (vb, "spirals", FALSE, _("Search spirals"), _("Spirals"), NULL);
                gtk_box_pack_start (GTK_BOX (hb), c, FALSE, FALSE, 0);
            }

            g_object_set_data (G_OBJECT (vb), "shapes-pane", hb);

            gtk_box_pack_start (GTK_BOX (vb_all), hb, FALSE, FALSE, 0);
            gtk_widget_hide (hb);
        }

        {
            // TRANSLATORS: polyline is a set of connected straight line segments
            // http://www.w3.org/TR/SVG11/shapes.html#PolylineElement
            GtkWidget *c = sp_find_types_checkbox_indented (vb, "paths", TRUE, _("Search paths, lines, polylines"), _("Paths"), NULL, 10);
            gtk_box_pack_start (GTK_BOX (vb_all), c, FALSE, FALSE, 0);
        }

        {
            GtkWidget *c = sp_find_types_checkbox_indented (vb, "texts", TRUE, _("Search text objects"), _("Texts"), NULL, 10);
            gtk_box_pack_start (GTK_BOX (vb_all), c, FALSE, FALSE, 0);
        }

        {
            GtkWidget *c = sp_find_types_checkbox_indented (vb, "groups", TRUE, _("Search groups"), _("Groups"), NULL, 10);
            gtk_box_pack_start (GTK_BOX (vb_all), c, FALSE, FALSE, 0);
        }

        {
            GtkWidget *c = sp_find_types_checkbox_indented (vb, "clones", TRUE, _("Search clones"),
                                                            //TRANSLATORS: "Clones" is a noun indicating type of object to find
                                                            C_("Find dialog","Clones"), NULL, 10);
            gtk_box_pack_start (GTK_BOX (vb_all), c, FALSE, FALSE, 0);
        }

        {
            GtkWidget *c = sp_find_types_checkbox_indented (vb, "images", TRUE, _("Search images"), _("Images"), NULL, 10);
            gtk_box_pack_start (GTK_BOX (vb_all), c, FALSE, FALSE, 0);
        }

        {
            GtkWidget *c = sp_find_types_checkbox_indented (vb, "offsets", TRUE, _("Search offset objects"), _("Offsets"), NULL, 10);
            gtk_box_pack_start (GTK_BOX (vb_all), c, FALSE, FALSE, 0);
        }

        gtk_box_pack_start (GTK_BOX (vb), vb_all, FALSE, FALSE, 0);
        g_object_set_data (G_OBJECT (vb), "all-pane", vb_all);
        gtk_widget_hide (vb_all);
    }

    return vb;
}


GtkWidget *
sp_find_dialog_old (void)
{
    if  (!dlg)
    {
        gchar title[500];
        sp_ui_dialog_title_string (Inkscape::Verb::get(SP_VERB_DIALOG_FIND), title);
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();

        dlg = sp_window_new (title, TRUE);
        if (x == -1000 || y == -1000) {
            x = prefs->getInt(prefs_path + "x", -1000);
            y = prefs->getInt(prefs_path + "y", -1000);
        }
        if (w ==0 || h == 0) {
            w = prefs->getInt(prefs_path + "w", 0);
            h = prefs->getInt(prefs_path + "h", 0);
        }
        
        prefs->setInt(prefs_path + "visible", 1);

//        if (x<0) x=0;
//        if (y<0) y=0;

        if (w && h)
            gtk_window_resize ((GtkWindow *) dlg, w, h);
        if (x >= 0 && y >= 0 && (x < (gdk_screen_width()-MIN_ONSCREEN_DISTANCE)) && (y < (gdk_screen_height()-MIN_ONSCREEN_DISTANCE))) {
            gtk_window_move ((GtkWindow *) dlg, x, y);
        } else {
            gtk_window_set_position(GTK_WINDOW(dlg), GTK_WIN_POS_CENTER);
        }

        sp_transientize (dlg);
        wd.win = dlg;
        wd.stop = 0;
        g_signal_connect   ( G_OBJECT (INKSCAPE), "activate_desktop", G_CALLBACK (sp_transientize_callback), &wd );

        g_signal_connect ( G_OBJECT (dlg), "event", G_CALLBACK (sp_dialog_event_handler), dlg);

        g_signal_connect ( G_OBJECT (dlg), "destroy", G_CALLBACK (sp_find_dialog_destroy), NULL );
        g_signal_connect ( G_OBJECT (dlg), "delete_event", G_CALLBACK (sp_find_dialog_delete), dlg);
        g_signal_connect   ( G_OBJECT (INKSCAPE), "shut_down", G_CALLBACK (sp_find_dialog_delete), dlg);

        g_signal_connect   ( G_OBJECT (INKSCAPE), "dialogs_hide", G_CALLBACK (sp_dialog_hide), dlg);
        g_signal_connect   ( G_OBJECT (INKSCAPE), "dialogs_unhide", G_CALLBACK (sp_dialog_unhide), dlg);

        gtk_container_set_border_width (GTK_CONTAINER (dlg), 4);

        /* Toplevel vbox */
#if GTK_CHECK_VERSION(3,0,0)
        GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_set_homogeneous(GTK_BOX(vb), FALSE);
#else
        GtkWidget *vb = gtk_vbox_new (FALSE, 0);
#endif
        gtk_container_add (GTK_CONTAINER (dlg), vb);

        sp_find_new_searchfield (dlg, vb, _("_Text:"), "text", _("Find objects by their text content (exact or partial match)"));
        sp_find_new_searchfield (dlg, vb, _("_ID:"), "id", _("Find objects by the value of the id attribute (exact or partial match)"));
        sp_find_new_searchfield (dlg, vb, _("_Style:"), "style", _("Find objects by the value of the style attribute (exact or partial match)"));
        sp_find_new_searchfield (dlg, vb, _("_Attribute:"), "attr", _("Find objects by the name of an attribute (exact or partial match)"));

        gtk_widget_show_all (vb);

        GtkWidget *types = sp_find_types ();
        g_object_set_data (G_OBJECT (dlg), "types", types);
        gtk_box_pack_start (GTK_BOX (vb), types, FALSE, FALSE, 0);

        {
#if GTK_CHECK_VERSION(3,0,0)
            GtkWidget *w = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
#else
            GtkWidget *w = gtk_hseparator_new ();
#endif
            gtk_widget_show (w);
            gtk_box_pack_start (GTK_BOX (vb), w, FALSE, FALSE, 3);

            {
            GtkWidget *b  = gtk_check_button_new_with_mnemonic (_("Search in s_election"));
            gtk_widget_show (b);
            gtk_toggle_button_set_active ((GtkToggleButton *) b, FALSE);
            g_object_set_data (G_OBJECT (dlg), "inselection", b);
            gtk_widget_set_tooltip_text (b, _("Limit search to the current selection"));
            gtk_box_pack_start (GTK_BOX (vb), b, FALSE, FALSE, 0);
            }

            {
            GtkWidget *b  = gtk_check_button_new_with_mnemonic (_("Search in current _layer"));
            gtk_widget_show (b);
            gtk_toggle_button_set_active ((GtkToggleButton *) b, FALSE);
            g_object_set_data (G_OBJECT (dlg), "inlayer", b);
            gtk_widget_set_tooltip_text (b, _("Limit search to the current layer"));
            gtk_box_pack_start (GTK_BOX (vb), b, FALSE, FALSE, 0);
            }

            {
            GtkWidget *b  = gtk_check_button_new_with_mnemonic (_("Include _hidden"));
            gtk_widget_show (b);
            gtk_toggle_button_set_active ((GtkToggleButton *) b, FALSE);
            g_object_set_data (G_OBJECT (dlg), "includehidden", b);
            gtk_widget_set_tooltip_text (b, _("Include hidden objects in search"));
            gtk_box_pack_start (GTK_BOX (vb), b, FALSE, FALSE, 0);
            }

            {
            GtkWidget *b  = gtk_check_button_new_with_mnemonic (_("Include l_ocked"));
            gtk_widget_show (b);
            gtk_toggle_button_set_active ((GtkToggleButton *) b, FALSE);
            g_object_set_data (G_OBJECT (dlg), "includelocked", b);
            gtk_widget_set_tooltip_text (b, _("Include locked objects in search"));
            gtk_box_pack_start (GTK_BOX (vb), b, FALSE, FALSE, 0);
            }
        }

        {
#if GTK_CHECK_VERSION(3,0,0)
            GtkWidget *hb = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_box_set_homogeneous(GTK_BOX(hb), FALSE);
#else
            GtkWidget *hb = gtk_hbox_new (FALSE, 0);
#endif
            gtk_widget_show (hb);
            gtk_box_pack_start (GTK_BOX (vb), hb, FALSE, FALSE, 0);

            // TRANSLATORS: "Clear" is a verb here
            sp_find_new_button (dlg, hb, _("_Clear"), _("Clear values"), sp_find_dialog_reset);
            sp_find_new_button (dlg, hb, _("_Find"), _("Select objects matching all of the fields you filled in"), sp_find_dialog_find);
        }
    }

    gtk_widget_show((GtkWidget *) dlg);
    gtk_window_present ((GtkWindow *) dlg);
    sp_find_dialog_reset (NULL, G_OBJECT (dlg));

    return dlg;
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
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
