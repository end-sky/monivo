/* Shared interfaces for Monivo's small, hackable C modules. */
#ifndef MONIVO_BROWSER_H
#define MONIVO_BROWSER_H

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#define HOME_URI "monivo://home"

extern GtkWidget *win, *entry, *engine_box, *status, *dark_btn, *ns_btn;
extern GtkWidget *tab_box, *tab_scroller, *content_stack;
extern WebKitWebView *view;
extern WebKitWebContext *ctx;
extern WebKitUserContentManager *ucm;
extern gboolean use_custom_ua;
extern int ns_mode;

void focus_entry(void);
void ns_toggle(void);
void tabs_initialize(void);
void tabs_add(gboolean activate, gboolean load_home);
void tabs_close_active(void);
void tabs_cycle(int direction);
void tabs_filters_ready(const char *initial_uri);
void tabs_update_widths(void);
void tabs_on_scroller_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data);
void tabs_on_new_tab_button(GtkButton *button, gpointer data);
gboolean keybinds_on_key(GtkWidget *widget, GdkEventKey *event, gpointer data);

#endif /* MONIVO_BROWSER_H */
