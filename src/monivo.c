/* Monivo - minimal privacy-first browser (GTK3 + WebKitGTK 4.1 / libsoup 3). */
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string.h>
#include <time.h>

#include "config_gen.h" /* engines[], HOME_HTML, WIN_W, ... (from config.lua) */

#define HOME_URI "monivo://home"
#ifndef DATADIR
#define DATADIR "/usr/local/share/monivo"
#endif

static GtkWidget *win, *entry, *engine_box, *status, *dark_btn, *ns_btn;
static GtkWidget *tab_box, *tab_scroller, *content_stack;
static WebKitWebView *view; /* currently selected view */
static WebKitWebContext *ctx;
static WebKitUserContentManager *ucm;

typedef struct {
    WebKitWebView *view;
    GtkWidget *container, *select_btn, *label, *close_btn;
    char *stack_name;
    gboolean load_when_ready;
} BrowserTab;

static GPtrArray *tabs;
static BrowserTab *active_tab, *initial_tab;
static guint next_tab_id = 1;
static gboolean use_custom_ua = FALSE;
static gboolean filters_ready = FALSE;
static WebKitUserContentFilterStore *store;
static WebKitUserContentFilter *ns_filter[2], *ns_added;
static int ns_mode = NOSCRIPT_MODE - 1; /* 0 = mode 1, 1 = mode 2 */
static int pending;                     /* async filter jobs still running */
static char *start_uri;
static gboolean dark = START_DARK;

static BrowserTab *add_tab(gboolean activate, gboolean load_home);
static void select_tab(BrowserTab *tab);
static void close_tab(BrowserTab *tab);
static void update_tab_widths(void);
static gboolean scroll_tabs_to_end(gpointer data);
static void on_new_tab_button(GtkButton *button, gpointer data);
static void on_load_changed(WebKitWebView *v, WebKitLoadEvent ev, gpointer data);
static void on_title(WebKitWebView *v, GParamSpec *p, gpointer data);
static gboolean on_load_failed(WebKitWebView *v, WebKitLoadEvent ev, const char *uri,
                               GError *err, gpointer data);
static gboolean on_policy(WebKitWebView *v, WebKitPolicyDecision *d,
                          WebKitPolicyDecisionType type, gpointer data);

/* ---------- address bar: URL or search ---------- */

static char *resolve_input(const char *text)
{
    char *s = g_strstrip(g_strdup(text));
    char *uri;

    if (!*s) {
        g_free(s);
        return NULL;
    }
    if (!strchr(s, ' ')) {
        if (!strcmp(s, "monivo:about"))
            uri = g_strdup(HOME_URI);
        else if (strstr(s, "://") || g_str_has_prefix(s, "about:") ||
                 g_str_has_prefix(s, "data:") || g_str_has_prefix(s, "file:"))
            uri = g_strdup(s);
        else if (g_str_has_prefix(s, "localhost"))
            uri = g_strconcat("http://", s, NULL);
        else if (strchr(s, '.'))
            uri = g_strconcat("https://", s, NULL);
        else
            uri = NULL;
        if (uri) {
            g_free(s);
            return uri;
        }
    }

    int i = gtk_combo_box_get_active(GTK_COMBO_BOX(engine_box));
    const Engine *e = &engines[i < 0 ? DEFAULT_ENGINE : i];
    char *q = g_uri_escape_string(s, NULL, FALSE);
    uri = g_strconcat(e->prefix, q, e->suffix, NULL);
    g_free(q);
    g_free(s);
    return uri;
}

static void on_activate(GtkEntry *e, gpointer u)
{
    char *uri = resolve_input(gtk_entry_get_text(e));
    if (uri) {
        webkit_web_view_load_uri(view, uri);
        gtk_widget_grab_focus(GTK_WIDGET(view));
        g_free(uri);
    }
}

static void focus_entry(void)
{
    gtk_widget_grab_focus(entry);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}

/* ---------- homepage (monivo:// scheme, so back/forward work) ---------- */

static void on_scheme(WebKitURISchemeRequest *req, gpointer u)
{
    const char *p = strstr(HOME_HTML, "@THEME@");
    char *html = g_strdup_printf("%.*s%s%s", (int)(p - HOME_HTML), HOME_HTML,
                                 dark ? "dark" : "light", p + 7);
    gsize len = strlen(html);
    GInputStream *s = g_memory_input_stream_new_from_data(html, len, g_free);
    webkit_uri_scheme_request_finish(req, s, len, "text/html");
    g_object_unref(s);
}

static void toggle_dark(void)
{
    const char *uri = webkit_web_view_get_uri(view);
    dark = !dark;
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", dark, NULL);
    gtk_button_set_label(GTK_BUTTON(dark_btn), dark ? "Light" : "Dark");
    if (uri && g_str_has_prefix(uri, "monivo://"))
        webkit_web_view_reload_bypass_cache(view);
}

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
                BrowserTab *tab = add_tab(TRUE, FALSE);
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

static void update_tab_widths(void)
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

static void on_tab_scroller_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data)
{
    update_tab_widths();
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
    close_tab((BrowserTab *)data);
}

static BrowserTab *add_tab(gboolean activate, gboolean load_home)
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
    update_tab_widths();
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

static void close_tab(BrowserTab *tab)
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

    g_signal_handlers_disconnect_by_data(tab->view, tab);
    g_signal_handlers_disconnect_by_data(tab->select_btn, tab);
    g_signal_handlers_disconnect_by_data(tab->close_btn, tab);
    webkit_web_view_stop_loading(tab->view);
    gtk_container_remove(GTK_CONTAINER(content_stack), GTK_WIDGET(tab->view));
    gtk_container_remove(GTK_CONTAINER(tab_box), tab->container);
    g_ptr_array_remove_index(tabs, index);

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
    update_tab_widths();
}

/* ---------- downloads ---------- */

static char *download_dir(void)
{
    const char *d = DOWNLOAD_DIR;
    if (!d)
        d = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    return d ? g_strdup(d) : g_build_filename(g_get_home_dir(), "Downloads", NULL);
}

static gboolean on_decide_destination(WebKitDownload *dl, const char *suggested, gpointer u)
{
    char *dir = download_dir();
    char *base = g_path_get_basename(suggested && *suggested ? suggested : "download");
    char *path = g_build_filename(dir, base, NULL);

    g_mkdir_with_parents(dir, 0755);
    /* never overwrite: "name.ext" -> "name (1).ext" */
    for (int n = 1; g_file_test(path, G_FILE_TEST_EXISTS) && n < 1000; n++) {
        char *dot = strrchr(base, '.');
        char *stem = dot && dot != base ? g_strndup(base, dot - base) : g_strdup(base);
        char *name = g_strdup_printf("%s (%d)%s", stem, n, dot && dot != base ? dot : "");
        g_free(path);
        path = g_build_filename(dir, name, NULL);
        g_free(stem);
        g_free(name);
    }
    char *uri = g_filename_to_uri(path, NULL, NULL);
    webkit_download_set_destination(dl, uri);
    g_free(uri);
    g_free(path);
    g_free(base);
    g_free(dir);
    return TRUE;
}

static void dl_label(WebKitDownload *dl, const char *fmt)
{
    const char *dest = webkit_download_get_destination(dl); /* a file:// URI */
    char *path = dest ? g_filename_from_uri(dest, NULL, NULL) : NULL;
    char *file = path ? g_path_get_basename(path) : g_strdup("file");
    g_free(path);
    char *text = g_strdup_printf(fmt, file);
    gtk_label_set_text(GTK_LABEL(status), text);
    g_free(text);
    g_free(file);
}

static void on_dl_progress(WebKitDownload *dl, GParamSpec *p, gpointer u)
{
    char fmt[48];
    g_snprintf(fmt, sizeof fmt, "\342\206\223 %%s  %d%%%%",
               (int)(webkit_download_get_estimated_progress(dl) * 100));
    dl_label(dl, fmt);
}

static void on_dl_finished(WebKitDownload *dl, gpointer u) { dl_label(dl, "\342\234\223 saved: %s"); }
static void on_dl_failed(WebKitDownload *dl, GError *e, gpointer u) { dl_label(dl, "\342\234\227 failed: %s"); }

static void on_download_started(WebKitWebContext *c, WebKitDownload *dl, gpointer u)
{
    g_signal_connect(dl, "decide-destination", G_CALLBACK(on_decide_destination), NULL);
    g_signal_connect(dl, "notify::estimated-progress", G_CALLBACK(on_dl_progress), NULL);
    g_signal_connect(dl, "finished", G_CALLBACK(on_dl_finished), NULL);
    g_signal_connect(dl, "failed", G_CALLBACK(on_dl_failed), NULL);
}

/* ---------- content filtering: NoScript modes + adblock ---------- */

static void report(const char *what, GError *e)
{
    g_printerr("monivo: %s: %s\n", what, e ? e->message : "failed");
    gtk_label_set_text(GTK_LABEL(status), what);
    g_clear_error(&e);
}

/* Apply the active NoScript mode: WebKit settings + its content-rule list. */
static void ns_apply(gboolean reload)
{
    if (!NOSCRIPT)
        return;
    WebKitSettings *s = webkit_web_view_get_settings(view);
    webkit_settings_set_enable_javascript(s, NS_SET[ns_mode].js);
    webkit_settings_set_enable_webgl(s, NS_SET[ns_mode].webgl);
    webkit_settings_set_enable_media(s, NS_SET[ns_mode].media);

    if (ns_added) {
        webkit_user_content_manager_remove_filter(ucm, ns_added);
        ns_added = NULL;
    }
    if (ns_filter[ns_mode]) {
        webkit_user_content_manager_add_filter(ucm, ns_filter[ns_mode]);
        ns_added = ns_filter[ns_mode];
    }
    char label[8];
    g_snprintf(label, sizeof label, "NS %d", ns_mode + 1);
    gtk_button_set_label(GTK_BUTTON(ns_btn), label);
    if (reload)
        webkit_web_view_reload(view);
}

static void ns_toggle(void)
{
    if (NOSCRIPT) {
        ns_mode ^= 1;
        ns_apply(TRUE);
    }
}

/* The first page loads only after all rule lists are installed. */
static void filter_job_done(void)
{
    if (--pending > 0)
        return;
    ns_apply(FALSE);
    filters_ready = TRUE;
    if (initial_tab && initial_tab->view)
        webkit_web_view_load_uri(initial_tab->view, start_uri);
    for (guint i = 0; tabs && i < tabs->len; i++) {
        BrowserTab *tab = g_ptr_array_index(tabs, i);
        if (tab != initial_tab && tab->load_when_ready) {
            tab->load_when_ready = FALSE;
            webkit_web_view_load_uri(tab->view, HOME_URI);
        }
    }
    g_free(start_uri);
    start_uri = NULL;
}

static void on_ns_saved(GObject *o, GAsyncResult *r, gpointer slot)
{
    GError *e = NULL;
    WebKitUserContentFilter *f = webkit_user_content_filter_store_save_finish(store, r, &e);
    if (f)
        ns_filter[GPOINTER_TO_INT(slot)] = f;
    else
        report("noscript rules failed", e);
    filter_job_done();
}

static void on_ab_saved(GObject *o, GAsyncResult *r, gpointer u)
{
    GError *e = NULL;
    WebKitUserContentFilter *f = webkit_user_content_filter_store_save_from_file_finish(store, r, &e);
    if (f)
        webkit_user_content_manager_add_filter(ucm, f);
    else
        report("adblock rules failed", e);
    filter_job_done();
}

static void on_ab_loaded(GObject *o, GAsyncResult *r, gpointer u)
{
    GError *e = NULL;
    WebKitUserContentFilter *f = webkit_user_content_filter_store_load_finish(store, r, &e);
    if (f) {
        webkit_user_content_manager_add_filter(ucm, f);
        filter_job_done();
        return;
    }
    g_clear_error(&e);

    /* not compiled yet (first run or lists changed): compile adblock.json */
    const char *dir = g_getenv("MONIVO_DATA");
    char *path = g_build_filename(dir ? dir : DATADIR, "adblock.json", NULL);
    GFile *file = g_file_new_for_path(path);
    webkit_user_content_filter_store_save_from_file(store, ADBLOCK_ID, file, NULL, on_ab_saved, NULL);
    g_object_unref(file);
    g_free(path);
}

static void setup_filters(void)
{
    pending = 1; /* guard so the start page waits for every job below */

    /* compiled rules are cached here; it holds no browsing data */
    char *dir = g_build_filename(g_get_user_cache_dir(), "monivo", "filters", NULL);
    g_mkdir_with_parents(dir, 0700);
    store = webkit_user_content_filter_store_new(dir);
    g_free(dir);

    if (NOSCRIPT) {
        for (int i = 0; i < 2; i++) {
            if (!NS_RULES[i])
                continue;
            GBytes *b = g_bytes_new_static(NS_RULES[i], strlen(NS_RULES[i]));
            pending++;
            webkit_user_content_filter_store_save(store, i ? "noscript-2" : "noscript-1", b, NULL,
                                                  on_ns_saved, GINT_TO_POINTER(i));
            g_bytes_unref(b);
        }
    }
    if (ADBLOCK) {
        pending++;
        webkit_user_content_filter_store_load(store, ADBLOCK_ID, NULL, on_ab_loaded, NULL);
    }
    filter_job_done();
}

/* ---------- keyboard ---------- */

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer u)
{
    guint m = e->state & gtk_accelerator_get_default_mod_mask();
    double z = webkit_web_view_get_zoom_level(view);

    if (m == GDK_CONTROL_MASK) {
        switch (e->keyval) {
        case GDK_KEY_l: focus_entry(); return TRUE;
        case GDK_KEY_h: webkit_web_view_load_uri(view, HOME_URI); return TRUE;
        case GDK_KEY_r: webkit_web_view_reload(view); return TRUE;
        case GDK_KEY_t: add_tab(TRUE, TRUE); return TRUE;
        case GDK_KEY_w: close_tab(active_tab); return TRUE;
        case GDK_KEY_Tab:
            if (tabs && tabs->len > 1 && active_tab) {
                guint i = 0;
                while (i < tabs->len && g_ptr_array_index(tabs, i) != active_tab) i++;
                select_tab(g_ptr_array_index(tabs, (i + 1) % tabs->len));
            }
            return TRUE;
        case GDK_KEY_q: gtk_main_quit(); return TRUE;
        case GDK_KEY_plus: case GDK_KEY_equal: webkit_web_view_set_zoom_level(view, z + 0.1); return TRUE;
        case GDK_KEY_minus: webkit_web_view_set_zoom_level(view, z > 0.2 ? z - 0.1 : z); return TRUE;
        case GDK_KEY_0: webkit_web_view_set_zoom_level(view, 1.0); return TRUE;
        }
    } else if (m == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
        if (e->keyval == GDK_KEY_J || e->keyval == GDK_KEY_j) { ns_toggle(); return TRUE; }
    } else if (m == GDK_MOD1_MASK) {
        if (e->keyval == GDK_KEY_Left) { webkit_web_view_go_back(view); return TRUE; }
        if (e->keyval == GDK_KEY_Right) { webkit_web_view_go_forward(view); return TRUE; }
    } else if (!m && e->keyval == GDK_KEY_F5) {
        webkit_web_view_reload(view);
        return TRUE;
    }
    return FALSE;
}

/* ---------- UI ---------- */

static void on_home(GtkButton *b, gpointer u) { webkit_web_view_load_uri(view, HOME_URI); }
static void on_back(GtkButton *b, gpointer u) { webkit_web_view_go_back(view); }
static void on_fwd(GtkButton *b, gpointer u) { webkit_web_view_go_forward(view); }
static void on_dark(GtkButton *b, gpointer u) { toggle_dark(); }
static void on_ns(GtkButton *b, gpointer u) { ns_toggle(); }
static void on_engine(GtkComboBox *b, gpointer u) { focus_entry(); }

static GtkWidget *button(GtkWidget *bar, const char *label, GCallback cb)
{
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(b, FALSE);
    g_signal_connect(b, "clicked", cb, NULL);
    gtk_box_pack_start(GTK_BOX(bar), b, FALSE, FALSE, 0);
    return b;
}

static void apply_css(void)
{
    GtkCssProvider *p = gtk_css_provider_new();
    gtk_css_provider_load_from_data(p,
        ".bar{padding:6px;border-bottom:1px solid alpha(@theme_fg_color,.25)}"
        ".bar entry,.bar button,.bar combobox button{border-radius:0;box-shadow:none}"
        ".tab-container{border-right:1px solid alpha(@theme_fg_color,.18)}"
        ".tab-button,.tab-close{border-radius:0;box-shadow:none;padding:3px 2px;min-height:22px}"
        ".tab-active{background-color:alpha(@theme_selected_bg_color,.22);border-bottom:2px solid @theme_selected_bg_color}"
        ".tab-close{padding-left:2px;padding-right:2px}", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

static void on_new_tab_button(GtkButton *button, gpointer data)
{
    add_tab(TRUE, TRUE);
}

static gboolean confirm_experimental_user_agent(void)
{
    if (!EXPERIMENTAL_UA_ENABLED)
        return FALSE;
    GtkWidget *dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
        "Experimental User-Agent override is enabled");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
        "This only changes the string websites see; it does not turn WebKit into Chromium. "
        "Some sites may break, render incorrectly, or behave unexpectedly. It may also make "
        "your browser easier to identify rather than improve privacy.\n\n"
        "Use the configured User-Agent for this run?");
    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
        "Disable for this run", GTK_RESPONSE_CANCEL,
        "Use anyway", GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_ACCEPT;
}

int main(int argc, char **argv)
{
    /* Fingerprinting: WebKit's web processes inherit TZ, so Date/Intl report this zone */
    if (TIMEZONE) {
        g_setenv("TZ", TIMEZONE, TRUE);
        tzset();
    }
    gtk_init(&argc, &argv);
    use_custom_ua = confirm_experimental_user_agent();

    /* ephemeral context: no cookies, cache or history on disk */
    ctx = webkit_web_context_new_ephemeral();
    webkit_web_context_register_uri_scheme(ctx, "monivo", on_scheme, NULL, NULL);
    webkit_cookie_manager_set_accept_policy(webkit_web_context_get_cookie_manager(ctx),
                                            WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
    g_signal_connect(ctx, "download-started", G_CALLBACK(on_download_started), NULL);
    if (N_LANGS) /* Accept-Language header, navigator.language(s) and Intl default locale */
        webkit_web_context_set_preferred_languages(ctx, LANGS);

    ucm = webkit_user_content_manager_new();
    tabs = g_ptr_array_new();
    if (CANVAS_MODE) { /* runs in every frame before page scripts; seed is fresh each launch */
        char *js = g_strdup_printf("%s\n(%u,%d);", CANVAS_JS, g_random_int(), CANVAS_MODE);
        WebKitUserScript *us = webkit_user_script_new(js, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                                      WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START, NULL, NULL);
        webkit_user_content_manager_add_script(ucm, us);
        webkit_user_script_unref(us);
        g_free(js);
    }

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Monivo");
    gtk_window_set_default_size(GTK_WINDOW(win), WIN_W, WIN_H);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    tab_scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(tab_scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(tab_scroller), GTK_SHADOW_NONE);
    gtk_container_add(GTK_CONTAINER(tab_scroller), tab_box);
    g_signal_connect(tab_scroller, "size-allocate", G_CALLBACK(on_tab_scroller_allocate), NULL);
    GtkWidget *tabs_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(tabs_row), tab_scroller, TRUE, TRUE, 0);
    GtkWidget *plus_btn = gtk_button_new_with_label("+");
    gtk_button_set_relief(GTK_BUTTON(plus_btn), GTK_RELIEF_NONE);
    gtk_widget_set_size_request(plus_btn, 36, 30);
    gtk_widget_set_can_focus(plus_btn, FALSE);
    gtk_widget_set_tooltip_text(plus_btn, "New tab (Ctrl+T)");
    gtk_box_pack_end(GTK_BOX(tabs_row), plus_btn, FALSE, FALSE, 0);

    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(bar), "bar");
    button(bar, "H", G_CALLBACK(on_home));
    button(bar, "<", G_CALLBACK(on_back));
    button(bar, ">", G_CALLBACK(on_fwd));

    engine_box = gtk_combo_box_text_new();
    for (int i = 0; i < N_ENGINES; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(engine_box), engines[i].name);
    gtk_combo_box_set_active(GTK_COMBO_BOX(engine_box), DEFAULT_ENGINE);
    gtk_widget_set_can_focus(engine_box, FALSE);
    g_signal_connect(engine_box, "changed", G_CALLBACK(on_engine), NULL);
    gtk_box_pack_start(GTK_BOX(bar), engine_box, FALSE, FALSE, 0);

    entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Search or URL...");
    g_signal_connect(entry, "activate", G_CALLBACK(on_activate), NULL);
    gtk_box_pack_start(GTK_BOX(bar), entry, TRUE, TRUE, 0);

    status = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(status), 28);
    gtk_box_pack_start(GTK_BOX(bar), status, FALSE, FALSE, 4);

    if (NOSCRIPT) {
        ns_btn = button(bar, "NS", G_CALLBACK(on_ns));
        gtk_widget_set_tooltip_text(ns_btn, "NoScript mode: 1 = scripts on, 2 = static only (Ctrl+Shift+J)");
    }
    dark_btn = button(bar, dark ? "Light" : "Dark", G_CALLBACK(on_dark));

    content_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(content_stack), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_box_pack_start(GTK_BOX(box), tabs_row, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), content_stack, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), box);

    g_signal_connect(plus_btn, "clicked", G_CALLBACK(on_new_tab_button), NULL);

    apply_css();
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", dark, NULL);

    initial_tab = add_tab(TRUE, FALSE);
    start_uri = argc > 1 ? resolve_input(argv[1]) : NULL;
    if (!start_uri)
        start_uri = g_strdup(HOME_URI);
    setup_filters(); /* loads start_uri once rules are installed */

    gtk_widget_show_all(win);
    update_tab_widths();
    gtk_main();
    return 0;
}
