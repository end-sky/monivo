/* Tab lifecycle, layout, and per-view navigation. */
#include "browser.h"
#include "config_gen.h"
#include <string.h>

typedef struct {
    WebKitWebView *view;
    GtkWidget *container, *select_btn, *label, *close_btn;
    char *stack_name;
    gboolean load_when_ready;
} BrowserTab;

static GPtrArray *tabs;
static BrowserTab *active_tab, *initial_tab;
static guint next_tab_id = 1;
static gboolean filters_ready = FALSE;

static BrowserTab *add_tab_internal(gboolean activate, gboolean load_home);
static void close_tab_internal(BrowserTab *tab);
static void select_tab(BrowserTab *tab);

/* ---------- navigation callbacks ---------- */

static void update_tab_label(BrowserTab *tab)
{
    if (!tab || !tab->view || !tab->label)
        return;
    const char *title = webkit_web_view_get_title(tab->view);
    const char *uri = webkit_web_view_get_uri(tab->view);
    const char *text = (title && *title) ? title :
                       (!uri || g_str_has_prefix(uri, "monivo://")) ? "New Tab" : uri;
    gtk_label_set_text(GTK_LABEL(tab->label), text);
    gtk_widget_set_tooltip_text(tab->container, text);
}

static void update_window_title(BrowserTab *tab)
{
    const char *t = tab && tab->view ? webkit_web_view_get_title(tab->view) : NULL;
    char *s = (t && *t && strcmp(t, "Monivo")) ? g_strconcat(t, " - Monivo", NULL) : g_strdup("Monivo");
    gtk_window_set_title(GTK_WINDOW(win), s);
    g_free(s);
}

static void on_load_changed(WebKitWebView *v, WebKitLoadEvent ev, gpointer data)
{
    BrowserTab *tab = data;
    if (ev != WEBKIT_LOAD_COMMITTED)
        return;
    const char *uri = webkit_web_view_get_uri(v);
    update_tab_label(tab);
    if (tab != active_tab)
        return;
    if (!uri || g_str_has_prefix(uri, "monivo://")) {
        gtk_entry_set_text(GTK_ENTRY(entry), "");
        focus_entry();
    } else {
        gtk_entry_set_text(GTK_ENTRY(entry), uri);
    }
    update_window_title(tab);
}

static void on_title(WebKitWebView *v, GParamSpec *p, gpointer data)
{
    BrowserTab *tab = data;
    update_tab_label(tab);
    if (tab == active_tab)
        update_window_title(tab);
}

static gboolean on_load_failed(WebKitWebView *v, WebKitLoadEvent ev, const char *uri,
                               GError *err, gpointer u)
{
    /* cancelled loads and loads turned into downloads are not errors */
    if (err->domain == WEBKIT_POLICY_ERROR ||
        g_error_matches(err, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED))
        return FALSE;
    char *m = g_markup_escape_text(err->message, -1);
    char *l = g_markup_escape_text(uri, -1);
    char *html = g_strdup_printf(
        "<html><body style='font:16px system-ui,sans-serif;padding:3em'>"
        "<h3>Can't load page</h3><p>%s</p><p style='color:#888'>%s</p></body></html>", m, l);
    webkit_web_view_load_alternate_html(v, html, uri, NULL);
    g_free(m);
    g_free(l);
    g_free(html);
    return TRUE;
}

static gboolean on_policy(WebKitWebView *v, WebKitPolicyDecision *d,
                          WebKitPolicyDecisionType type, gpointer u)
{
    if (type == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        WebKitNavigationAction *a =
            webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(d));
        /* NoScript: popups the page opens without a user click are dropped. */
        if (!(NOSCRIPT && !NS_SET[ns_mode].popup && !webkit_navigation_action_is_user_gesture(a))) {
            const char *uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(a));
            if (uri && *uri) {
                BrowserTab *tab = add_tab_internal(TRUE, FALSE);
                webkit_web_view_load_uri(tab->view, uri);
            }
        }
        webkit_policy_decision_ignore(d);
        return TRUE;
    }
    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) {
        WebKitURIResponse *r = webkit_response_policy_decision_get_response(WEBKIT_RESPONSE_POLICY_DECISION(d));
        SoupMessageHeaders *h = webkit_uri_response_get_http_headers(r);
        const char *cd = h ? soup_message_headers_get_one(h, "Content-Disposition") : NULL;
        /* anything we can't display, or that asks to be saved, becomes a download */
        if (!webkit_web_view_can_show_mime_type(v, webkit_uri_response_get_mime_type(r)) ||
            (cd && !g_ascii_strncasecmp(cd, "attachment", 10))) {
            webkit_policy_decision_download(d);
            return TRUE;
        }
    }
    return FALSE;
}

/* ---------- tabs ---------- */

static void select_tab(BrowserTab *tab)
{
    if (!tab || !tab->view)
        return;
    active_tab = tab;
    view = tab->view;
    gtk_stack_set_visible_child(GTK_STACK(content_stack), GTK_WIDGET(tab->view));

    for (guint i = 0; i < tabs->len; i++) {
        BrowserTab *item = g_ptr_array_index(tabs, i);
        GtkStyleContext *style = gtk_widget_get_style_context(item->select_btn);
        if (item == tab)
            gtk_style_context_add_class(style, "tab-active");
        else
            gtk_style_context_remove_class(style, "tab-active");
    }

    const char *uri = webkit_web_view_get_uri(tab->view);
    if (!uri || g_str_has_prefix(uri, "monivo://"))
        gtk_entry_set_text(GTK_ENTRY(entry), "");
    else
        gtk_entry_set_text(GTK_ENTRY(entry), uri);
    update_tab_label(tab);
    update_window_title(tab);
    gtk_widget_grab_focus(GTK_WIDGET(tab->view));
}

void tabs_update_widths(void)
{
    if (!tabs || tabs->len == 0 || !tab_scroller)
        return;
    int available = gtk_widget_get_allocated_width(tab_scroller);
    if (available <= 1)
        return;
    int width = (available - 4) / (int)tabs->len;
    if (width > 190) width = 190;
    if (width < 52) width = 52;
    for (guint i = 0; i < tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(tabs, i);
        gtk_widget_set_size_request(tab->container, width, 30);
    }
}

void tabs_on_scroller_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data)
{
    tabs_update_widths();
}

static gboolean scroll_tabs_to_end(gpointer data)
{
    if (tab_scroller) {
        GtkAdjustment *adjustment = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(tab_scroller));
        double end = gtk_adjustment_get_upper(adjustment) - gtk_adjustment_get_page_size(adjustment);
        if (end < gtk_adjustment_get_lower(adjustment))
            end = gtk_adjustment_get_lower(adjustment);
        gtk_adjustment_set_value(adjustment, end);
    }
    return G_SOURCE_REMOVE;
}

static void on_tab_select(GtkButton *button, gpointer data)
{
    select_tab((BrowserTab *)data);
}

static void on_tab_close(GtkButton *button, gpointer data)
{
    close_tab_internal((BrowserTab *)data);
}

static BrowserTab *add_tab_internal(gboolean activate, gboolean load_home)
{
    BrowserTab *tab = g_new0(BrowserTab, 1);
    tab->stack_name = g_strdup_printf("tab-%u", next_tab_id++);
    if (tabs->len > 0) {
        BrowserTab *related = active_tab ? active_tab : g_ptr_array_index(tabs, 0);
        tab->view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_related_view(related->view));
    } else {
        tab->view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
            "web-context", ctx, "user-content-manager", ucm, NULL));
        WebKitSettings *settings = webkit_web_view_get_settings(tab->view);
        if (use_custom_ua)
            webkit_settings_set_user_agent(settings, EXPERIMENTAL_UA);
        webkit_settings_set_enable_media(settings, TRUE);
        webkit_settings_set_enable_mediasource(settings, TRUE);
        webkit_settings_set_enable_webaudio(settings, TRUE);
        webkit_settings_set_enable_developer_extras(settings, FALSE);
    }

    /* Keep WebKit's viewport tied to the window allocation on every resize. */
    gtk_widget_set_hexpand(GTK_WIDGET(tab->view), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(tab->view), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(tab->view), GTK_ALIGN_FILL);
    gtk_widget_set_valign(GTK_WIDGET(tab->view), GTK_ALIGN_FILL);

    tab->container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(tab->container), "tab-container");
    tab->select_btn = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(tab->select_btn), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(tab->select_btn, FALSE);
    gtk_widget_set_tooltip_text(tab->select_btn, "Select tab");
    gtk_style_context_add_class(gtk_widget_get_style_context(tab->select_btn), "tab-button");
    tab->label = gtk_label_new("New Tab");
    gtk_label_set_ellipsize(GTK_LABEL(tab->label), PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(GTK_LABEL(tab->label), TRUE);
    gtk_widget_set_margin_start(tab->label, 6);
    gtk_widget_set_margin_end(tab->label, 3);
    gtk_container_add(GTK_CONTAINER(tab->select_btn), tab->label);
    gtk_box_pack_start(GTK_BOX(tab->container), tab->select_btn, TRUE, TRUE, 0);

    tab->close_btn = gtk_button_new_with_label("×");
    gtk_button_set_relief(GTK_BUTTON(tab->close_btn), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(tab->close_btn, FALSE);
    gtk_widget_set_size_request(tab->close_btn, 22, -1);
    gtk_widget_set_tooltip_text(tab->close_btn, "Close tab");
    gtk_style_context_add_class(gtk_widget_get_style_context(tab->close_btn), "tab-close");
    gtk_box_pack_end(GTK_BOX(tab->container), tab->close_btn, FALSE, FALSE, 0);

    g_signal_connect(tab->select_btn, "clicked", G_CALLBACK(on_tab_select), tab);
    g_signal_connect(tab->close_btn, "clicked", G_CALLBACK(on_tab_close), tab);
    g_signal_connect(tab->view, "load-changed", G_CALLBACK(on_load_changed), tab);
    g_signal_connect(tab->view, "load-failed", G_CALLBACK(on_load_failed), tab);
    g_signal_connect(tab->view, "decide-policy", G_CALLBACK(on_policy), tab);
    g_signal_connect(tab->view, "notify::title", G_CALLBACK(on_title), tab);

    gtk_box_pack_start(GTK_BOX(tab_box), tab->container, FALSE, FALSE, 0);
    gtk_stack_add_named(GTK_STACK(content_stack), GTK_WIDGET(tab->view), tab->stack_name);
    g_ptr_array_add(tabs, tab);
    if (!initial_tab)
        initial_tab = tab;
    tabs_update_widths();
    gtk_widget_show_all(tab->container);
    gtk_widget_show(GTK_WIDGET(tab->view));

    if (activate) {
        select_tab(tab);
        g_idle_add(scroll_tabs_to_end, NULL);
    }
    if (load_home) {
        if (filters_ready)
            webkit_web_view_load_uri(tab->view, HOME_URI);
        else
            tab->load_when_ready = TRUE;
    }
    return tab;
}

static void close_tab_internal(BrowserTab *tab)
{
    if (!tab || !tabs || tabs->len == 0)
        return;
    if (tabs->len == 1) {
        webkit_web_view_load_uri(tab->view, HOME_URI);
        return;
    }

    guint index = 0;
    while (index < tabs->len && g_ptr_array_index(tabs, index) != tab)
        index++;
    if (index == tabs->len)
        return;
    gboolean was_active = (tab == active_tab);
    gboolean was_initial = (tab == initial_tab);

    g_signal_handlers_disconnect_by_data(tab->view, tab);
    g_signal_handlers_disconnect_by_data(tab->select_btn, tab);
    g_signal_handlers_disconnect_by_data(tab->close_btn, tab);
    webkit_web_view_stop_loading(tab->view);
    gtk_container_remove(GTK_CONTAINER(content_stack), GTK_WIDGET(tab->view));
    gtk_container_remove(GTK_CONTAINER(tab_box), tab->container);
    g_ptr_array_remove_index(tabs, index);
    if (was_initial)
        initial_tab = g_ptr_array_index(tabs, index < tabs->len ? index : tabs->len - 1);

    if (was_active) {
        active_tab = NULL;
        view = NULL;
    }
    g_free(tab->stack_name);
    g_free(tab);

    if (was_active) {
        guint next = index < tabs->len ? index : tabs->len - 1;
        select_tab(g_ptr_array_index(tabs, next));
    }
    tabs_update_widths();
}



void tabs_initialize(void)
{
    if (!tabs)
        tabs = g_ptr_array_new();
}

void tabs_add(gboolean activate, gboolean load_home)
{
    if (!tabs)
        tabs_initialize();
    (void)add_tab_internal(activate, load_home);
}

void tabs_close_active(void)
{
    close_tab_internal(active_tab);
}

void tabs_cycle(int direction)
{
    if (!tabs || tabs->len < 2 || !active_tab)
        return;

    guint index = 0;
    while (index < tabs->len && g_ptr_array_index(tabs, index) != active_tab)
        index++;
    if (index == tabs->len)
        return;

    int count = (int)tabs->len;
    int next = ((int)index + (direction % count) + count) % count;
    select_tab(g_ptr_array_index(tabs, (guint)next));
}

void tabs_filters_ready(const char *initial_uri)
{
    filters_ready = TRUE;
    if (initial_tab && initial_tab->view)
        webkit_web_view_load_uri(initial_tab->view, initial_uri ? initial_uri : HOME_URI);

    for (guint i = 0; tabs && i < tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(tabs, i);
        if (tab != initial_tab && tab->load_when_ready) {
            tab->load_when_ready = FALSE;
            webkit_web_view_load_uri(tab->view, HOME_URI);
        }
    }
}

void tabs_on_new_tab_button(GtkButton *button, gpointer data)
{
    tabs_add(TRUE, TRUE);
}
