/*
 * Keyboard shortcuts live here on purpose: this file is intended to be easy to hack.
 * To add a shortcut, write a small action function and add an entry to `keybinds`.
 *
 * `modifiers` must be present. `optional_modifiers` allows keyboard-layout modifiers
 * needed to type symbols such as < and >. Use GDK_KEY_* key symbols.
 */
#include "browser.h"

#include <gdk/gdkkeysyms.h>

typedef gboolean (*KeybindAction)(void);
typedef struct {
    guint keyval;
    GdkModifierType modifiers;
    GdkModifierType optional_modifiers;
    KeybindAction action;
} Keybind;

static gboolean focus_address(void) { focus_entry(); return TRUE; }
static gboolean go_home(void) { webkit_web_view_load_uri(view, HOME_URI); return TRUE; }
static gboolean reload_page(void) { webkit_web_view_reload(view); return TRUE; }
static gboolean new_tab(void) { tabs_add(TRUE, TRUE); return TRUE; }
static gboolean close_active_tab(void) { tabs_close_active(); return TRUE; }
static gboolean next_tab(void) { tabs_cycle(1); return TRUE; }
static gboolean previous_tab(void) { tabs_cycle(-1); return TRUE; }
static gboolean quit_browser(void) { gtk_main_quit(); return TRUE; }
static gboolean zoom_in(void)
{
    webkit_web_view_set_zoom_level(view, webkit_web_view_get_zoom_level(view) + 0.1);
    return TRUE;
}
static gboolean zoom_out(void)
{
    double zoom = webkit_web_view_get_zoom_level(view);
    webkit_web_view_set_zoom_level(view, zoom > 0.2 ? zoom - 0.1 : zoom);
    return TRUE;
}
static gboolean zoom_reset(void) { webkit_web_view_set_zoom_level(view, 1.0); return TRUE; }
static gboolean toggle_noscript(void) { ns_toggle(); return TRUE; }
static gboolean go_back(void) { webkit_web_view_go_back(view); return TRUE; }
static gboolean go_forward(void) { webkit_web_view_go_forward(view); return TRUE; }

#define CTRL GDK_CONTROL_MASK
#define SHIFT GDK_SHIFT_MASK
#define ALT GDK_MOD1_MASK
#define BIND(key, mods, extra, fn) { (key), (mods), (extra), (fn) }

/*
 * Default bindings. Add entries here to remap/add shortcuts without touching main.c.
 * Ctrl+< / Ctrl+> often arrive as Ctrl+Shift+comma/period on US-style keyboards;
 * GTK reports the resulting key symbols as GDK_KEY_less / GDK_KEY_greater.
 */
static const Keybind keybinds[] = {
    BIND(GDK_KEY_l,       CTRL,          0,     focus_address),
    BIND(GDK_KEY_h,       CTRL,          0,     go_home),
    BIND(GDK_KEY_r,       CTRL,          0,     reload_page),
    BIND(GDK_KEY_t,       CTRL,          0,     new_tab),
    BIND(GDK_KEY_w,       CTRL,          0,     close_active_tab),
    BIND(GDK_KEY_Tab,     CTRL,          0,     next_tab),
    BIND(GDK_KEY_less,    CTRL,          SHIFT, previous_tab),
    BIND(GDK_KEY_greater, CTRL,          SHIFT, next_tab),
    BIND(GDK_KEY_q,       CTRL,          0,     quit_browser),
    BIND(GDK_KEY_plus,    CTRL,          SHIFT, zoom_in),
    BIND(GDK_KEY_equal,   CTRL,          0,     zoom_in),
    BIND(GDK_KEY_minus,   CTRL,          0,     zoom_out),
    BIND(GDK_KEY_0,       CTRL,          0,     zoom_reset),
    BIND(GDK_KEY_j,       CTRL | SHIFT,  0,     toggle_noscript),
    BIND(GDK_KEY_Left,    ALT,           0,     go_back),
    BIND(GDK_KEY_Right,   ALT,           0,     go_forward),
    BIND(GDK_KEY_F5,      0,             0,     reload_page),
};

#undef CTRL
#undef SHIFT
#undef ALT
#undef BIND

gboolean keybinds_on_key(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    guint keyval = gdk_keyval_to_lower(event->keyval);

    for (guint i = 0; i < G_N_ELEMENTS(keybinds); i++) {
        const Keybind *binding = &keybinds[i];
        guint required = binding->modifiers;
        guint allowed = required | binding->optional_modifiers;

        if (keyval != (guint)gdk_keyval_to_lower(binding->keyval))
            continue;
        if ((modifiers & required) != required)
            continue;
        if ((modifiers & ~allowed) != 0)
            continue;
        return binding->action ? binding->action() : FALSE;
    }
    return FALSE;
}
