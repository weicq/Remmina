/*
 * Remmina - The GTK+ Remote Desktop Client
 * Copyright (C) 2009 - Vic Lee 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, 
 * Boston, MA 02111-1307, USA.
 */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <panel-applet.h>
#include "config.h"
#include "remminaappletmenuitem.h"
#include "remminaavahi.h"
#include "remminaappletutil.h"

#define REMMINA_APPLET_FACTORY_IID "OAFIID:Remmina_Applet_Factory"
#define REMMINA_APPLET_IID         "OAFIID:Remmina_Applet"

typedef struct _RemminaAppletData
{
    PanelApplet *applet;

    GtkWidget *image;
    GtkWidget *popup_menu;
    guint32 popup_time;

    gint prev_size;

    gchar *menu_group;
    gint menu_group_count;
    GtkWidget *menu_group_widget;

    RemminaAvahi *avahi;
} RemminaAppletData;

static void
remmina_applet_destroy (GtkWidget *widget, RemminaAppletData *appdata)
{
    remmina_avahi_free (appdata->avahi);
    g_free (appdata);
}

static void
remmina_applet_size_allocate (GtkWidget *widget, GtkAllocation *allocation, RemminaAppletData *appdata)
{
    gint size = 0;
    gint icon_size;

    switch (panel_applet_get_orient (appdata->applet))
    {
    case PANEL_APPLET_ORIENT_UP:
    case PANEL_APPLET_ORIENT_DOWN:
        size = allocation->height;
        break;
    case PANEL_APPLET_ORIENT_LEFT:
    case PANEL_APPLET_ORIENT_RIGHT:
        size = allocation->width;
        break;
    }

    if (size < 10) return;
    if (size == appdata->prev_size) return;

    appdata->prev_size = size;

    /* Fit the exact available icon sizes */
    if (size < 22) icon_size = 16;
    else if (size < 24) icon_size = 22;
    else if (size < 32) icon_size = 24;
    else if (size < 48) icon_size = 32;
    else icon_size = 48;
    gtk_image_set_pixel_size (GTK_IMAGE (appdata->image), icon_size);
}

static void
remmina_applet_menu_open_main (RemminaAppletMenuItem *menuitem, gpointer data)
{
    remmina_applet_util_launcher (REMMINA_LAUNCH_MAIN, NULL, NULL, NULL);
}

static void
remmina_applet_menu_open_pref (RemminaAppletMenuItem *menuitem, gpointer data)
{
    remmina_applet_util_launcher (REMMINA_LAUNCH_PREF, NULL, NULL, NULL);
}

static void
remmina_applet_menu_open_quick (RemminaAppletMenuItem *menuitem, GdkEventButton *event, RemminaAppletData *appdata)
{
    remmina_applet_util_launcher (REMMINA_LAUNCH_QUICK, NULL, NULL, NULL);
}

static void
remmina_applet_menu_open_file (RemminaAppletMenuItem *menuitem, GdkEventButton *event, RemminaAppletData *appdata)
{
    remmina_applet_util_launcher (event->button == 3 ? REMMINA_LAUNCH_EDIT : REMMINA_LAUNCH_FILE, menuitem->filename, NULL, NULL);
}

static void
remmina_applet_menu_open_discovered (RemminaAppletMenuItem *menuitem, GdkEventButton *event, RemminaAppletData *appdata)
{
    remmina_applet_util_launcher (event->button == 3 ? REMMINA_LAUNCH_NEW : REMMINA_LAUNCH_QUICK, NULL, menuitem->name, menuitem->protocol);
}

static void
remmina_applet_popdown_menu (GtkWidget *widget, RemminaAppletData *appdata)
{
    appdata->popup_menu = NULL;
    gtk_widget_set_state (GTK_WIDGET (appdata->applet), GTK_STATE_NORMAL);
    if (gtk_get_current_event_time () - appdata->popup_time <= 500)
    {
        remmina_applet_menu_open_main (NULL, NULL);
    }
}

static void
remmina_applet_popup_menu_position (GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer data)
{
    GdkScreen *screen;
    GtkRequisition req;
    gint tx, ty;
    RemminaAppletData *appdata;

    appdata = (RemminaAppletData*) data;
    gdk_window_get_origin (GTK_WIDGET (appdata->applet)->window, &tx, &ty);
    gtk_widget_size_request (GTK_WIDGET (menu), &req);

    switch (panel_applet_get_orient (appdata->applet))
    {
    case PANEL_APPLET_ORIENT_UP:
        ty -= req.height;
        break;
    case PANEL_APPLET_ORIENT_DOWN:
        ty += GTK_WIDGET (appdata->applet)->allocation.height;
        break;
    case PANEL_APPLET_ORIENT_LEFT:
        tx -= req.width;
        break;
    case PANEL_APPLET_ORIENT_RIGHT:
        tx += GTK_WIDGET (appdata->applet)->allocation.width;
        break;
    }

    screen = gdk_screen_get_default ();
    tx = MIN (gdk_screen_get_width (screen) - req.width - 1, tx);
    ty = MIN (gdk_screen_get_height (screen) - req.height - 1, ty);

    *x = tx;
    *y = ty;
    *push_in = TRUE;
}

static void
remmina_applet_popup_menu_update_group (RemminaAppletData* appdata)
{
    gchar *label;

    if (appdata->menu_group)
    {
        if (!remmina_applet_util_get_pref_boolean ("applet_hide_count"))
        {
            label = g_strdup_printf ("%s (%i)", appdata->menu_group, appdata->menu_group_count);
            gtk_label_set_text (GTK_LABEL (gtk_bin_get_child (GTK_BIN (appdata->menu_group_widget))), label);
            g_free (label);
        }
        appdata->menu_group = NULL;
        appdata->menu_group_count = 0;
    }
}

static void
remmina_applet_popup_menu_add_item (gpointer data, gpointer user_data)
{
    RemminaAppletData *appdata;
    RemminaAppletMenuItem *item;
    GtkWidget *submenu;
    GtkWidget *image;

    appdata = (RemminaAppletData*) user_data;
    item = REMMINA_APPLET_MENU_ITEM (data);

    if (item->group && item->group[0] != '\0')
    {
        if (!appdata->menu_group || g_strcmp0 (appdata->menu_group, item->group) != 0)
        {
            remmina_applet_popup_menu_update_group (appdata);

            appdata->menu_group = item->group;

            appdata->menu_group_widget = gtk_image_menu_item_new_with_label (appdata->menu_group);
            gtk_widget_show (appdata->menu_group_widget);
            gtk_menu_shell_append (GTK_MENU_SHELL (appdata->popup_menu), appdata->menu_group_widget);

            image = gtk_image_new_from_icon_name ((item->item_type == REMMINA_APPLET_MENU_ITEM_DISCOVERED ?
                "folder-remote" : "folder"), GTK_ICON_SIZE_MENU);
            gtk_widget_show (image);
            gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (appdata->menu_group_widget), image);

            submenu = gtk_menu_new ();
            gtk_widget_show (submenu);
            gtk_menu_item_set_submenu (GTK_MENU_ITEM (appdata->menu_group_widget), submenu);
        }
        else
        {
            submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (appdata->menu_group_widget));
        }
        gtk_menu_shell_append (GTK_MENU_SHELL (submenu), GTK_WIDGET (item));
        appdata->menu_group_count++;
    }
    else
    {
        remmina_applet_popup_menu_update_group (appdata);
        gtk_menu_shell_append (GTK_MENU_SHELL (appdata->popup_menu), GTK_WIDGET (item));
    }
}

static void
remmina_applet_popup_menu (GtkWidget *widget, GdkEventButton *event, RemminaAppletData *appdata)
{
    GtkWidget *menuitem;
    GPtrArray *menuitem_array;
    gint button, event_time;
    gchar dirname[MAX_PATH_LEN];
    gchar filename[MAX_PATH_LEN];
    GDir *dir;
    const gchar *name;
    gboolean quick_ontop;
    GHashTableIter iter;
    gchar *tmp;

    quick_ontop = remmina_applet_util_get_pref_boolean ("applet_quick_ontop");

    appdata->popup_time = gtk_get_current_event_time ();
    appdata->popup_menu = gtk_menu_new ();

    if (quick_ontop)
    {
        menuitem = remmina_applet_menu_item_new (REMMINA_APPLET_MENU_ITEM_QUICK);
        g_signal_connect (G_OBJECT (menuitem), "button-press-event",
            G_CALLBACK (remmina_applet_menu_open_quick), appdata);
        gtk_widget_show (menuitem);
        gtk_menu_shell_append (GTK_MENU_SHELL (appdata->popup_menu), menuitem);

        menuitem = gtk_separator_menu_item_new ();
        gtk_widget_show (menuitem);
        gtk_menu_shell_append (GTK_MENU_SHELL (appdata->popup_menu), menuitem);
    }

    g_snprintf (dirname, MAX_PATH_LEN, "%s/.remmina", g_get_home_dir ());
    dir = g_dir_open (dirname, 0, NULL);
    if (dir != NULL)
    {
        menuitem_array = g_ptr_array_new ();

        /* Iterate all remote desktop profiles */
        while ((name = g_dir_read_name (dir)) != NULL)
        {
            if (!g_str_has_suffix (name, ".remmina")) continue;
            g_snprintf (filename, MAX_PATH_LEN, "%s/%s", dirname, name);

            menuitem = remmina_applet_menu_item_new (REMMINA_APPLET_MENU_ITEM_FILE, filename);
            g_signal_connect (G_OBJECT (menuitem), "button-press-event",
                G_CALLBACK (remmina_applet_menu_open_file), appdata);
            gtk_widget_show (menuitem);

            g_ptr_array_add (menuitem_array, menuitem);
        }

        /* Iterate all discovered services from Avahi */
        if (appdata->avahi)
        {
            g_hash_table_iter_init (&iter, appdata->avahi->discovered_services);
            while (g_hash_table_iter_next (&iter, NULL, (gpointer*) &tmp))
            {
                menuitem = remmina_applet_menu_item_new (REMMINA_APPLET_MENU_ITEM_DISCOVERED, tmp);
                g_signal_connect (G_OBJECT (menuitem), "button-press-event",
                    G_CALLBACK (remmina_applet_menu_open_discovered), appdata);
                gtk_widget_show (menuitem);

                g_ptr_array_add (menuitem_array, menuitem);
            }
        }

        g_ptr_array_sort_with_data (menuitem_array, remmina_applet_menu_item_compare, NULL);

        appdata->menu_group = NULL;
        appdata->menu_group_count = 0;
        g_ptr_array_foreach (menuitem_array, remmina_applet_popup_menu_add_item, appdata);

        remmina_applet_popup_menu_update_group (appdata);

        g_ptr_array_free (menuitem_array, FALSE);
    }

    if (!quick_ontop)
    {
        menuitem = gtk_separator_menu_item_new ();
        gtk_widget_show (menuitem);
        gtk_menu_shell_append (GTK_MENU_SHELL (appdata->popup_menu), menuitem);

        menuitem = remmina_applet_menu_item_new (REMMINA_APPLET_MENU_ITEM_QUICK);
        g_signal_connect (G_OBJECT (menuitem), "button-press-event",
            G_CALLBACK (remmina_applet_menu_open_quick), appdata);
        gtk_widget_show (menuitem);
        gtk_menu_shell_append (GTK_MENU_SHELL (appdata->popup_menu), menuitem);
    }

    if (event)
    {
        button = event->button;
        event_time = event->time;
    }
    else
    {
        button = 0;
        event_time = gtk_get_current_event_time ();
    }

    gtk_widget_set_state (GTK_WIDGET (appdata->applet), GTK_STATE_SELECTED);
    g_signal_connect (G_OBJECT (appdata->popup_menu), "deactivate", G_CALLBACK (remmina_applet_popdown_menu), appdata);

    gtk_menu_attach_to_widget (GTK_MENU (appdata->popup_menu), widget, NULL);
    gtk_widget_realize (appdata->popup_menu);
    gtk_menu_popup (GTK_MENU (appdata->popup_menu), NULL, NULL,
        remmina_applet_popup_menu_position, appdata, button, event_time);
}

static gboolean
remmina_applet_button_press_event (GtkWidget *widget, GdkEventButton *event, RemminaAppletData *appdata)
{
    if (event->button == 1)
    {
        switch (event->type)
        {
        case GDK_BUTTON_PRESS:
            remmina_applet_popup_menu (widget, event, appdata);
            break;
        default:
            break;
        }
        return TRUE;
    }
    return FALSE;
}

static void
remmina_applet_menu_enable_avahi (BonoboUIComponent *uic,
    const char                  *path,
    Bonobo_UIComponent_EventType type,
    const char                  *state,
    gpointer                     userdata)
{
    RemminaAppletData *appdata = (RemminaAppletData*) userdata;

    if (!appdata->avahi) return;

    if (state[0] == '1')
    {
        remmina_applet_util_set_pref_boolean ("applet_enable_avahi", TRUE);
        if (!appdata->avahi->started) remmina_avahi_start (appdata->avahi);
    }
    else
    {
        remmina_applet_util_set_pref_boolean ("applet_enable_avahi", FALSE);
        remmina_avahi_stop (appdata->avahi);
    }
}

static void
remmina_applet_menu_about (BonoboUIComponent *uic, RemminaAppletData *appdata, const char *verb)
{
    remmina_applet_util_launcher (REMMINA_LAUNCH_ABOUT, NULL, NULL, NULL);
}

static const BonoboUIVerb remmina_applet_menu_verbs [] =
{   
    BONOBO_UI_UNSAFE_VERB ("OpenMain", remmina_applet_menu_open_main),
    BONOBO_UI_UNSAFE_VERB ("OpenPref", remmina_applet_menu_open_pref),
    BONOBO_UI_UNSAFE_VERB ("About", remmina_applet_menu_about),
    BONOBO_UI_VERB_END
};

static const gchar *remmina_applet_menu_xml = 
"    <popup name='button3'>"
"      <menuitem name='OpenMain' verb='OpenMain' _label='%s' pixtype='stock' pixname='gtk-execute'  />"
"      <menuitem name='OpenPref' verb='OpenPref' _label='%s' pixtype='stock' pixname='gtk-preferences'  />"
"      <menuitem name='About'    verb='About'    _label='%s' pixtype='stock' pixname='gtk-about' />"
#ifdef HAVE_LIBAVAHI_CLIENT
"      <separator />"
"      <menuitem name='EnableAvahi' type='toggle' id='EnableAvahi' _label='%s' />"
#endif
"    </popup>"
;

static void
remmina_applet_create (PanelApplet *applet)
{
    BonoboUIComponent *popup_component;
    gchar buf[1000];
    RemminaAppletData *appdata;

    appdata = g_new0 (RemminaAppletData, 1);
    appdata->applet = applet;
    appdata->popup_menu = NULL;
    appdata->popup_time = 0;
    appdata->prev_size = 0;
    appdata->avahi = remmina_avahi_new ();

    appdata->image = gtk_image_new_from_icon_name ("remmina", GTK_ICON_SIZE_MENU);
    gtk_widget_show (appdata->image);

    gtk_container_add (GTK_CONTAINER (applet), appdata->image);

    g_signal_connect (G_OBJECT (applet), "destroy", G_CALLBACK (remmina_applet_destroy), appdata);
    g_signal_connect (G_OBJECT (applet), "button-press-event", G_CALLBACK (remmina_applet_button_press_event), appdata);
    g_signal_connect (G_OBJECT (appdata->image), "size-allocate", G_CALLBACK (remmina_applet_size_allocate), appdata);

    g_snprintf (buf, sizeof (buf), remmina_applet_menu_xml, _("Open Main Window"), _("Preferences"),
        _("About")
#ifdef HAVE_LIBAVAHI_CLIENT
        , _("Enable Service Discovery")
#endif
        );
    panel_applet_setup_menu (applet, buf, remmina_applet_menu_verbs, appdata);

    popup_component = panel_applet_get_popup_component (applet);
    bonobo_ui_component_add_listener (popup_component, "EnableAvahi", remmina_applet_menu_enable_avahi, appdata);

    panel_applet_set_flags (applet, PANEL_APPLET_EXPAND_MINOR);
    panel_applet_set_background_widget (applet, GTK_WIDGET (applet));

    gtk_widget_show (GTK_WIDGET (applet));

    if (remmina_applet_util_get_pref_boolean ("applet_enable_avahi"))
    {
        bonobo_ui_component_set_prop (popup_component, "/commands/EnableAvahi", "state", "1", NULL);
        remmina_avahi_start (appdata->avahi);
    }
}

static gboolean
remmina_applet_factory (PanelApplet *applet, const gchar *iid, gpointer data)
{
    if (strcmp (iid, REMMINA_APPLET_IID) != 0) return FALSE;

    bindtextdomain (GETTEXT_PACKAGE, REMMINA_GNOME_LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
        REMMINA_DATADIR G_DIR_SEPARATOR_S "icons");

    remmina_applet_create (applet);

    return TRUE;
}

PANEL_APPLET_BONOBO_FACTORY (REMMINA_APPLET_FACTORY_IID,
                            PANEL_TYPE_APPLET,
                            _("Remmina Remote Desktop Client Applet"),
                            "0", 
                            remmina_applet_factory, 
                            NULL)
