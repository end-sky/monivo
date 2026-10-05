/* Monivo - minimal privacy-first browser (GTK3 + WebKitGTK 4.1 / libsoup 3). */
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <string.h>

#include "config_gen.h" /* engines[], HOME_HTML, WIN_W, ... (from config.lua) */

#define HOME_URI "monivo://home"

static GtkWidget *win, *entry, *engine_box, *status, *dark_btn;
static WebKitWebView *view;
static gboolean dark = START_DARK;

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

static void on_load_changed(WebKitWebView *v, WebKitLoadEvent ev, gpointer u)
{
    if (ev != WEBKIT_LOAD_COMMITTED)
        return;
    const char *uri = webkit_web_view_get_uri(v);
    if (!uri || g_str_has_prefix(uri, "monivo://")) {
        gtk_entry_set_text(GTK_ENTRY(entry), "");
        focus_entry();
    } else {
        gtk_entry_set_text(GTK_ENTRY(entry), uri);
    }
}

static void on_title(WebKitWebView *v, GParamSpec *p, gpointer u)
{
    const char *t = webkit_web_view_get_title(v);
    char *s = (t && *t) ? g_strconcat(t, " - Monivo", NULL) : g_strdup("Monivo");
    gtk_window_set_title(GTK_WINDOW(win), s);
    g_free(s);
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
        /* single window: open target=_blank / window.open in the same view */
        WebKitNavigationAction *a =
            webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(d));
        webkit_web_view_load_uri(v, webkit_uri_request_get_uri(webkit_navigation_action_get_request(a)));
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
        case GDK_KEY_q: gtk_main_quit(); return TRUE;
        case GDK_KEY_plus: case GDK_KEY_equal: webkit_web_view_set_zoom_level(view, z + 0.1); return TRUE;
        case GDK_KEY_minus: webkit_web_view_set_zoom_level(view, z > 0.2 ? z - 0.1 : z); return TRUE;
        case GDK_KEY_0: webkit_web_view_set_zoom_level(view, 1.0); return TRUE;
        }
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
        ".bar entry,.bar button,.bar combobox button{border-radius:0;box-shadow:none}", -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(p), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(p);
}

int main(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    /* ephemeral context: no cookies, cache or history on disk */
    WebKitWebContext *ctx = webkit_web_context_new_ephemeral();
    webkit_web_context_register_uri_scheme(ctx, "monivo", on_scheme, NULL, NULL);
    webkit_cookie_manager_set_accept_policy(webkit_web_context_get_cookie_manager(ctx),
                                            WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
    g_signal_connect(ctx, "download-started", G_CALLBACK(on_download_started), NULL);

    view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(ctx));
    WebKitSettings *s = webkit_web_view_get_settings(view);
    webkit_settings_set_enable_media(s, TRUE);
    webkit_settings_set_enable_mediasource(s, TRUE);
    webkit_settings_set_enable_webaudio(s, TRUE);
    webkit_settings_set_enable_developer_extras(s, FALSE);
    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), NULL);
    g_signal_connect(view, "load-failed", G_CALLBACK(on_load_failed), NULL);
    g_signal_connect(view, "decide-policy", G_CALLBACK(on_policy), NULL);
    g_signal_connect(view, "notify::title", G_CALLBACK(on_title), NULL);

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), "Monivo");
    gtk_window_set_default_size(GTK_WINDOW(win), WIN_W, WIN_H);
    g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);

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

    dark_btn = button(bar, dark ? "Light" : "Dark", G_CALLBACK(on_dark));

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(view), TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win), box);

    apply_css();
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", dark, NULL);

    char *start = argc > 1 ? resolve_input(argv[1]) : NULL;
    webkit_web_view_load_uri(view, start ? start : HOME_URI);
    g_free(start);

    gtk_widget_show_all(win);
    gtk_main();
    return 0;
}
