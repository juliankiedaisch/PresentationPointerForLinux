/*
 * wacom-cursor – Wacom Pen Cursor Overlay für GNOME/Wayland
 *
 * Zeichnet ein transparentes, klick-durchlässiges Fadenkreuz an der
 * Wacom-Pen-Position. Nutzt XWayland, da GNOME/Mutter kein
 * wlr-layer-shell unterstützt.
 *
 * Build:  make
 * Run:    ./wacom-cursor
 * Stop:   Ctrl+C  oder  systemctl --user stop wacom-cursor
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>

/* ── Konfiguration ── */
#define DEFAULT_RADIUS 8
#define BORDER_LW      2.0     /* Randstärke              */
#define WIN_PAD        4

static int g_radius = DEFAULT_RADIUS;
#define WIN_HALF   (g_radius + (int)BORDER_LW + WIN_PAD)
#define WIN_SIZE   (WIN_HALF * 2)

/* ── Monitor-Geometrie ── */
typedef struct { int x, y, w, h; } MonGeo;

/* ── Globaler Zustand ── */
static volatile sig_atomic_t g_running = 1;
static volatile int g_pen_near = 0;
static volatile int g_abs_x = 0, g_abs_y = 0;
static int g_pipe[2];

/* X11-Kontext (für Tray-Callbacks) */
static Display *g_dpy    = NULL;
static Window   g_win    = 0;
static Visual  *g_vis    = NULL;
static MonGeo   g_mon    = {0};
static int      g_depth  = 0;
static Colormap g_cmap   = 0;
static const char *g_dev_path = NULL;

/* Liste der Monitor-Namen für Tray-Menü */
#define MAX_MONITORS 16
static char  *g_mon_names[MAX_MONITORS];
static MonGeo g_mon_geos[MAX_MONITORS];
static int    g_mon_count = 0;
static char  *g_active_mon_name = NULL;

static void on_signal(int s) {
    (void)s;
    g_running = 0;
    char c = 'q';
    if (write(g_pipe[1], &c, 1) < 0) { /* ignore */ }
    gtk_main_quit();
}

/* ════════════════════════════════════════════════
 *  Wacom Pen Device finden
 * ════════════════════════════════════════════════ */

static char *find_wacom_pen(void)
{
    FILE *f = fopen("/proc/bus/input/devices", "r");
    if (!f) return NULL;

    /* /proc-Dateien haben keine echte Größe → zeilenweise lesen */
    size_t cap = 0, len = 0;
    char *buf = NULL;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        if (len + n + 1 > cap) {
            cap = (len + n + 1) * 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); fclose(f); return NULL; }
            buf = tmp;
        }
        memcpy(buf + len, line, n);
        len += n;
    }
    fclose(f);
    if (!buf) return NULL;
    buf[len] = '\0';

    char *result = NULL;
    char *block = buf;
    while (block && *block) {
        char *next = strstr(block, "\n\n");
        if (next) { *next = '\0'; next += 2; }
        if (strstr(block, "Wacom") && strstr(block, "Pen")) {
            char *h = strstr(block, "H: Handlers=");
            if (h) {
                char *ev = strstr(h, "event");
                if (ev) {
                    if (asprintf(&result, "/dev/input/event%d",
                                 atoi(ev + 5)) < 0)
                        result = NULL;
                    break;
                }
            }
        }
        block = next;
    }
    free(buf);
    return result;
}

/* ════════════════════════════════════════════════
 *  XRandR: Monitor-Geometrie ermitteln
 * ════════════════════════════════════════════════ */

static int find_monitor(Display *dpy, int scr, const char *name, MonGeo *out)
{
    XRRScreenResources *res = XRRGetScreenResources(dpy, RootWindow(dpy, scr));
    if (!res) return 0;

    int found = 0;
    RROutput primary = XRRGetOutputPrimary(dpy, RootWindow(dpy, scr));

    printf("Monitore:\n");
    for (int i = 0; i < res->noutput && g_mon_count < MAX_MONITORS; i++) {
        XRROutputInfo *oi = XRRGetOutputInfo(dpy, res, res->outputs[i]);
        if (!oi || oi->connection != RR_Connected || !oi->crtc) {
            if (oi) XRRFreeOutputInfo(oi);
            continue;
        }
        XRRCrtcInfo *ci = XRRGetCrtcInfo(dpy, res, oi->crtc);
        if (!ci) { XRRFreeOutputInfo(oi); continue; }

        int is_primary = (res->outputs[i] == primary);
        printf("  %s %dx%d+%d+%d%s\n", oi->name,
               ci->width, ci->height, ci->x, ci->y,
               is_primary ? " [primary]" : "");

        /* Monitor-Liste für Tray-Menü speichern */
        int mi = g_mon_count;
        g_mon_names[mi] = strdup(oi->name);
        g_mon_geos[mi] = (MonGeo){ci->x, ci->y, (int)ci->width, (int)ci->height};
        g_mon_count++;

        /* Treffer: expliziter Name oder Primary als Fallback */
        if (!found) {
            if (name && strcmp(name, oi->name) == 0) {
                out->x = ci->x; out->y = ci->y;
                out->w = (int)ci->width; out->h = (int)ci->height;
                found = 1;
            } else if (!name && is_primary) {
                out->x = ci->x; out->y = ci->y;
                out->w = (int)ci->width; out->h = (int)ci->height;
                found = 1;
            }
        }

        /* Falls kein Primary gesetzt und kein Name: ersten verbundenen nehmen */
        if (!found && !name && i == 0) {
            out->x = ci->x; out->y = ci->y;
            out->w = (int)ci->width; out->h = (int)ci->height;
        }

        XRRFreeCrtcInfo(ci);
        XRRFreeOutputInfo(oi);
    }

    /* Fallback: erster Monitor wurde oben gesetzt */
    if (!found && !name) found = 1;

    XRRFreeScreenResources(res);
    return found;
}

/* ════════════════════════════════════════════════
 *  32-bit ARGB Visual (für Transparenz)
 * ════════════════════════════════════════════════ */

static Visual *find_argb_visual(Display *d, int scr, int *depth_out)
{
    XVisualInfo t;
    memset(&t, 0, sizeof t);
    t.screen = scr;
    t.depth  = 32;
    t.class  = TrueColor;
    int n;
    XVisualInfo *vi = XGetVisualInfo(d,
        VisualScreenMask | VisualDepthMask | VisualClassMask, &t, &n);
    if (!vi || n == 0) return NULL;
    *depth_out = 32;
    Visual *v = vi[0].visual;
    XFree(vi);
    return v;
}

/* ════════════════════════════════════════════════
 *  Cursor zeichnen (gefüllter Kreis)
 * ════════════════════════════════════════════════ */

static void draw_cursor(Display *dpy, Window win, Visual *vis)
{
    int ws = WIN_SIZE;
    cairo_surface_t *sf =
        cairo_xlib_surface_create(dpy, win, vis, ws, ws);
    cairo_t *cr = cairo_create(sf);

    /* Hintergrund transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    double c = WIN_HALF;
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Hellrote Füllung */
    cairo_arc(cr, c, c, g_radius, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, 1.0, 0.4, 0.4, 0.45);
    cairo_fill_preserve(cr);

    /* Dunkelroter Rand */
    cairo_set_source_rgba(cr, 0.6, 0.0, 0.0, 0.8);
    cairo_set_line_width(cr, BORDER_LW);
    cairo_stroke(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(sf);
}

/* ════════════════════════════════════════════════
 *  evdev-Lesethread
 * ════════════════════════════════════════════════ */

typedef struct { const char *path; } RdArgs;

static void *reader_thread(void *arg)
{
    RdArgs *a = arg;
    int fd = open(a->path, O_RDONLY);
    if (fd < 0) { perror(a->path); return NULL; }

    struct libevdev *dev = NULL;
    if (libevdev_new_from_fd(fd, &dev) < 0) {
        fprintf(stderr, "libevdev init fehlgeschlagen\n");
        close(fd);
        return NULL;
    }

    int max_x = libevdev_get_abs_maximum(dev, ABS_X);
    int max_y = libevdev_get_abs_maximum(dev, ABS_Y);
    if (max_x <= 0) max_x = 1;
    if (max_y <= 0) max_y = 1;

    /* Aktuelle Position holen falls Pen schon in Nähe */
    g_pen_near = libevdev_get_event_value(dev, EV_KEY, BTN_TOOL_PEN);
    if (g_pen_near) {
        MonGeo m = g_mon;
        g_abs_x = m.x + (int)((long long)libevdev_get_event_value(dev, EV_ABS, ABS_X)
                        * m.w / max_x);
        g_abs_y = m.y + (int)((long long)libevdev_get_event_value(dev, EV_ABS, ABS_Y)
                        * m.h / max_y);
        char c = 'u';
        if (write(g_pipe[1], &c, 1) < 0) { /* ignore */ }
    }

    struct input_event ev;
    while (g_running) {
        int rc = libevdev_next_event(dev,
            LIBEVDEV_READ_FLAG_NORMAL | LIBEVDEV_READ_FLAG_BLOCKING, &ev);
        if (rc < 0 && rc != -EAGAIN) break;
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            while (libevdev_next_event(dev, LIBEVDEV_READ_FLAG_SYNC, &ev)
                   == LIBEVDEV_READ_STATUS_SYNC)
                ;
            continue;
        }

        if (ev.type == EV_ABS) {
            MonGeo m = g_mon;  /* bei jedem Event aktuell lesen */
            if (ev.code == ABS_X)
                g_abs_x = m.x + (int)((long long)ev.value * m.w / max_x);
            else if (ev.code == ABS_Y)
                g_abs_y = m.y + (int)((long long)ev.value * m.h / max_y);
        } else if (ev.type == EV_KEY && ev.code == BTN_TOOL_PEN) {
            g_pen_near = ev.value;
        }

        if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            char c = 'u';
            if (write(g_pipe[1], &c, 1) < 0) { /* ignore */ }
        }
    }

    libevdev_free(dev);
    close(fd);
    return NULL;
}

/* ════════════════════════════════════════════════
 *  Overlay-Fenster neu erstellen (bei Radius/Monitor-Wechsel)
 * ════════════════════════════════════════════════ */

static void recreate_overlay(void)
{
    if (g_win) {
        XUnmapWindow(g_dpy, g_win);
        XDestroyWindow(g_dpy, g_win);
    }

    int ws = WIN_SIZE;
    XSetWindowAttributes wa;
    memset(&wa, 0, sizeof wa);
    wa.override_redirect = True;
    wa.colormap      = g_cmap;
    wa.border_pixel  = 0;
    wa.background_pixel = 0;

    g_win = XCreateWindow(g_dpy, RootWindow(g_dpy, DefaultScreen(g_dpy)),
        -ws, -ws, ws, ws,
        0, g_depth, InputOutput, g_vis,
        CWOverrideRedirect | CWColormap | CWBorderPixel | CWBackPixel,
        &wa);

    XserverRegion empty = XFixesCreateRegion(g_dpy, NULL, 0);
    XFixesSetWindowShapeRegion(g_dpy, g_win, ShapeInput, 0, 0, empty);
    XFixesDestroyRegion(g_dpy, empty);

    XSelectInput(g_dpy, g_win, ExposureMask | StructureNotifyMask);
    XMapWindow(g_dpy, g_win);
    draw_cursor(g_dpy, g_win, g_vis);
    XFlush(g_dpy);
}

/* ════════════════════════════════════════════════
 *  Tray-Icon Callbacks
 * ════════════════════════════════════════════════ */

static void on_tray_quit(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    g_running = 0;
    char c = 'q';
    if (write(g_pipe[1], &c, 1) < 0) { /* ignore */ }
    gtk_main_quit();
}

static void on_tray_radius(GtkMenuItem *item, gpointer data)
{
    (void)item;
    int r = GPOINTER_TO_INT(data);
    if (r == g_radius) return;
    g_radius = r;
    printf("Radius: %d\n", g_radius);
    recreate_overlay();
}

static void on_tray_monitor(GtkMenuItem *item, gpointer data)
{
    (void)item;
    int idx = GPOINTER_TO_INT(data);
    if (idx < 0 || idx >= g_mon_count) return;
    g_mon = g_mon_geos[idx];
    free(g_active_mon_name);
    g_active_mon_name = strdup(g_mon_names[idx]);
    printf("Monitor: %s %dx%d+%d+%d\n",
           g_active_mon_name, g_mon.w, g_mon.h, g_mon.x, g_mon.y);
}

static GtkWidget *create_tray_menu(void)
{
    GtkWidget *menu = gtk_menu_new();

    /* Radius-Untermenü */
    GtkWidget *radius_item = gtk_menu_item_new_with_label("Cursorgröße");
    GtkWidget *radius_menu = gtk_menu_new();
    int sizes[] = {4, 8, 12, 16, 24, 32, 48};
    for (int i = 0; i < (int)(sizeof(sizes)/sizeof(sizes[0])); i++) {
        char label[32];
        snprintf(label, sizeof(label), "%d px", sizes[i]);
        GtkWidget *mi = gtk_menu_item_new_with_label(label);
        g_signal_connect(mi, "activate", G_CALLBACK(on_tray_radius),
                         GINT_TO_POINTER(sizes[i]));
        gtk_menu_shell_append(GTK_MENU_SHELL(radius_menu), mi);
    }
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(radius_item), radius_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), radius_item);

    /* Monitor-Untermenü */
    if (g_mon_count > 1) {
        GtkWidget *mon_item = gtk_menu_item_new_with_label("Bildschirm");
        GtkWidget *mon_menu = gtk_menu_new();
        for (int i = 0; i < g_mon_count; i++) {
            char label[128];
            snprintf(label, sizeof(label), "%s (%dx%d)",
                     g_mon_names[i], g_mon_geos[i].w, g_mon_geos[i].h);
            GtkWidget *mi = gtk_menu_item_new_with_label(label);
            g_signal_connect(mi, "activate", G_CALLBACK(on_tray_monitor),
                             GINT_TO_POINTER(i));
            gtk_menu_shell_append(GTK_MENU_SHELL(mon_menu), mi);
        }
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mon_item), mon_menu);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mon_item);
    }

    /* Trennlinie + Beenden */
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    GtkWidget *quit = gtk_menu_item_new_with_label("Beenden");
    g_signal_connect(quit, "activate", G_CALLBACK(on_tray_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit);

    gtk_widget_show_all(menu);
    return menu;
}

/* ════════════════════════════════════════════════
 *  X11 main-loop als GSource (für GTK-Integration)
 * ════════════════════════════════════════════════ */

typedef struct {
    GSource source;
    GPollFD xfd;
    GPollFD pipefd;
} XSource;

static gboolean x_prepare(GSource *src, gint *timeout)
{
    (void)src;
    *timeout = 100;
    return XPending(g_dpy) > 0;
}

static gboolean x_check(GSource *src)
{
    XSource *xs = (XSource *)src;
    return (xs->xfd.revents & G_IO_IN) || (xs->pipefd.revents & G_IO_IN)
           || XPending(g_dpy) > 0;
}

static gboolean x_dispatch(GSource *src, GSourceFunc cb, gpointer data)
{
    (void)src; (void)cb; (void)data;
    static int old_x = -9999, old_y = -9999, old_near = 0;

    /* X-Events */
    while (XPending(g_dpy)) {
        XEvent xe;
        XNextEvent(g_dpy, &xe);
        if (xe.type == Expose)
            draw_cursor(g_dpy, g_win, g_vis);
    }

    /* Pipe leeren */
    { char b[64]; while (read(g_pipe[0], b, sizeof(b)) > 0); }

    if (!g_running) {
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }

    int near = g_pen_near;
    int cx = g_abs_x, cy = g_abs_y;
    int wh = WIN_HALF;
    int ws = WIN_SIZE;

    if (!near && old_near) {
        XMoveWindow(g_dpy, g_win, -ws, -ws);
        XFlush(g_dpy);
        old_x = -ws; old_y = -ws;
    } else if (near && (cx != old_x || cy != old_y || !old_near)) {
        XMoveWindow(g_dpy, g_win, cx - wh, cy - wh);
        XFlush(g_dpy);
        old_x = cx; old_y = cy;
    }
    old_near = near;

    return G_SOURCE_CONTINUE;
}

static GSourceFuncs x_source_funcs = {
    x_prepare, x_check, x_dispatch, NULL, NULL, NULL
};

/* ════════════════════════════════════════════════
 *  main
 * ════════════════════════════════════════════════ */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Verwendung: %s [OPTIONEN]\n"
        "\n"
        "  --output NAME   Zielmonitor (z.B. DP-1, HDMI-1)\n"
        "                  Standard: Primary Monitor\n"
        "  --radius N      Cursor-Radius in Pixel (Standard: %d)\n"
        "  --list          Monitore auflisten und beenden\n"
        "  --help          Diese Hilfe\n", prog, DEFAULT_RADIUS);
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);

    const char *output_name = NULL;
    int list_only = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
            output_name = argv[++i];
        else if (strcmp(argv[i], "--radius") == 0 && i + 1 < argc) {
            g_radius = atoi(argv[++i]);
            if (g_radius < 2) g_radius = 2;
            if (g_radius > 200) g_radius = 200;
        }
        else if (strcmp(argv[i], "--list") == 0)
            list_only = 1;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
            { usage(argv[0]); return 0; }
        else
            { usage(argv[0]); return 1; }
    }

    if (pipe2(g_pipe, O_NONBLOCK | O_CLOEXEC) < 0) {
        perror("pipe"); return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    char *dev_path = find_wacom_pen();
    if (!dev_path) {
        fprintf(stderr,
            "Wacom Pen Device nicht gefunden – Tablet angeschlossen?\n");
        return 1;
    }
    g_dev_path = dev_path;
    printf("Wacom Pen: %s\n", dev_path);

    /* ── X11-Verbindung (XWayland auf GNOME) ── */
    g_dpy = XOpenDisplay(NULL);
    if (!g_dpy) {
        fprintf(stderr,
            "X Display nicht verfügbar.\n"
            "Hinweis: XWayland muss aktiviert sein (Standard bei GNOME).\n");
        free(dev_path);
        return 1;
    }

    int scr = DefaultScreen(g_dpy);
    int sw  = DisplayWidth(g_dpy, scr);
    int sh  = DisplayHeight(g_dpy, scr);
    printf("X11 Screen: %dx%d\n", sw, sh);

    /* Monitor-Geometrie per XRandR */
    g_mon = (MonGeo){0, 0, sw, sh};
    if (!find_monitor(g_dpy, scr, output_name, &g_mon)) {
        fprintf(stderr, "Monitor '%s' nicht gefunden.\n", output_name);
        XCloseDisplay(g_dpy);
        free(dev_path);
        return 1;
    }
    if (list_only) {
        XCloseDisplay(g_dpy);
        free(dev_path);
        return 0;
    }
    if (output_name)
        g_active_mon_name = strdup(output_name);
    printf("Zielmonitor: %dx%d+%d+%d\n", g_mon.w, g_mon.h, g_mon.x, g_mon.y);

    g_vis = find_argb_visual(g_dpy, scr, &g_depth);
    if (!g_vis) {
        fprintf(stderr, "Kein 32-bit ARGB Visual – Compositing aktiv?\n");
        XCloseDisplay(g_dpy);
        free(dev_path);
        return 1;
    }

    g_cmap = XCreateColormap(g_dpy, RootWindow(g_dpy, scr),
                             g_vis, AllocNone);

    /* Overlay-Fenster erstellen */
    recreate_overlay();

    /* ── evdev-Thread ── */
    static RdArgs ra;
    ra.path = dev_path;
    pthread_t thr;
    pthread_create(&thr, NULL, reader_thread, &ra);

    /* ── Tray-Icon ── */
    AppIndicator *indicator = app_indicator_new(
        "wacom-cursor", "input-tablet",
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
    app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(indicator, "Wacom Cursor");
    app_indicator_set_menu(indicator, GTK_MENU(create_tray_menu()));

    /* ── X11 + Pipe als GSource in GTK-Mainloop ── */
    XSource *xs = (XSource *)g_source_new(&x_source_funcs, sizeof(XSource));
    xs->xfd.fd = ConnectionNumber(g_dpy);
    xs->xfd.events = G_IO_IN;
    xs->pipefd.fd = g_pipe[0];
    xs->pipefd.events = G_IO_IN;
    g_source_add_poll((GSource *)xs, &xs->xfd);
    g_source_add_poll((GSource *)xs, &xs->pipefd);
    g_source_attach((GSource *)xs, NULL);

    /* ── GTK Hauptschleife ── */
    gtk_main();

    printf("\nBeende\n");
    g_source_destroy((GSource *)xs);
    XDestroyWindow(g_dpy, g_win);
    XCloseDisplay(g_dpy);
    free(dev_path);
    free(g_active_mon_name);
    for (int i = 0; i < g_mon_count; i++) free(g_mon_names[i]);
    close(g_pipe[0]);
    close(g_pipe[1]);
    return 0;
}
