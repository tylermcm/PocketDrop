// GTK 3 host: window, input, drag/drop, and Shell services for the shared UI.
#include "gfx_cairo.h"
#include "../ui/ui.h"
#include <gtk/gtk.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <ctime>
#include <memory>

namespace {

std::string pastedImagePath() {
    std::string dir = util::path_join(plat::app_data_dir(), "Pasted");
    plat::make_dir(dir);
    char stamp[64];
    std::time_t now = std::time(nullptr);
    std::tm tm {};
    localtime_r(&now, &tm);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", &tm);
    return util::path_join(dir, std::string("Pasted image ") + stamp + ".png");
}

struct MenuPick {
    int chosen = 0;
    GMainLoop* loop = nullptr;
};

GtkWidget* buildMenu(const std::vector<MenuItem>& items, MenuPick* pick) {
    GtkWidget* menu = gtk_menu_new();
    for (const auto& item : items) {
        if (item.separator) {
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
            continue;
        }
        GtkWidget* row = item.checked ? gtk_check_menu_item_new_with_label(item.label.c_str())
                                      : gtk_menu_item_new_with_label(item.label.c_str());
        if (item.checked) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(row), TRUE);
        gtk_widget_set_sensitive(row, item.enabled);
        if (!item.submenu.empty()) {
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(row), buildMenu(item.submenu, pick));
        } else if (item.id) {
            g_object_set_data(G_OBJECT(row), "pd-id", GINT_TO_POINTER(item.id));
            g_object_set_data(G_OBJECT(row), "pd-pick", pick);
            g_signal_connect(row, "activate", G_CALLBACK(+[](GtkMenuItem* row, gpointer) {
                                 auto* p = (MenuPick*)g_object_get_data(G_OBJECT(row), "pd-pick");
                                 p->chosen = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "pd-id"));
                                 if (p->loop && g_main_loop_is_running(p->loop)) g_main_loop_quit(p->loop);
                             }),
                             nullptr);
        }
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), row);
    }
    return menu;
}

class LinuxShell : public Shell {
public:
    GtkWidget* window = nullptr;
    GtkWidget* view = nullptr;
    Ui* ui = nullptr;
    guint fastTimer = 0;

    void invalidate() override {
        if (view) gtk_widget_queue_draw(view);
    }

    void setAnimating(bool on) override {
        if (on && !fastTimer) {
            fastTimer = g_timeout_add(33, +[](gpointer data) -> gboolean {
                auto* shell = (LinuxShell*)data;
                if (shell->ui) shell->ui->tick();
                return G_SOURCE_CONTINUE;
            }, this);
        } else if (!on && fastTimer) {
            g_source_remove(fastTimer);
            fastTimer = 0;
        }
    }

    void post(std::function<void()> fn) override {
        g_idle_add_full(G_PRIORITY_DEFAULT,
                        +[](gpointer data) -> gboolean {
                            std::unique_ptr<std::function<void()>> task((std::function<void()>*)data);
                            (*task)();
                            return G_SOURCE_REMOVE;
                        },
                        new std::function<void()>(std::move(fn)), nullptr);
    }

    void clientSize(float& w, float& h) override {
        w = view ? (float)gtk_widget_get_allocated_width(view) : 385;
        h = view ? (float)gtk_widget_get_allocated_height(view) : 722;
    }

    void copyText(const std::string& text) override {
        GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(cb, text.c_str(), (int)text.size());
        gtk_clipboard_store(cb);
    }

    void readClipboard(std::vector<std::string>& paths, std::string& text) override {
        GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        if (gchar** uris = gtk_clipboard_wait_for_uris(cb)) {
            for (int i = 0; uris[i]; i++) {
                GError* error = nullptr;
                gchar* file = g_filename_from_uri(uris[i], nullptr, &error);
                if (file) paths.emplace_back(file);
                g_free(file);
                if (error) g_error_free(error);
            }
            g_strfreev(uris);
            if (!paths.empty()) return;
        }
        if (GdkPixbuf* image = gtk_clipboard_wait_for_image(cb)) {
            std::string path = pastedImagePath();
            GError* error = nullptr;
            if (gdk_pixbuf_save(image, path.c_str(), "png", &error, nullptr)) paths.push_back(path);
            if (error) g_error_free(error);
            g_object_unref(image);
            if (!paths.empty()) return;
        }
        if (gchar* value = gtk_clipboard_wait_for_text(cb)) {
            text = value;
            g_free(value);
        }
    }

    void browse(bool folders, std::function<void(const std::vector<std::string>&)> done) override {
        GtkFileChooserAction action = folders ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER : GTK_FILE_CHOOSER_ACTION_OPEN;
        GtkWidget* dialog = gtk_file_chooser_dialog_new(folders ? "Choose folders to share" : "Choose files to share",
                                                        GTK_WINDOW(window), action, "Cancel", GTK_RESPONSE_CANCEL,
                                                        "Add", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
        std::vector<std::string> list;
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            GSList* files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
            for (GSList* p = files; p; p = p->next) {
                list.emplace_back((const char*)p->data);
                g_free(p->data);
            }
            g_slist_free(files);
        }
        gtk_widget_destroy(dialog);
        if (!list.empty()) done(list);
    }

    void openUrl(const std::string& url) override {
        GError* error = nullptr;
        gtk_show_uri_on_window(GTK_WINDOW(window), url.c_str(), GDK_CURRENT_TIME, &error);
        if (error) g_error_free(error);
    }

    int popupMenu(const std::vector<MenuItem>& items, float, float) override {
        MenuPick pick;
        pick.loop = g_main_loop_new(nullptr, FALSE);
        GtkWidget* menu = buildMenu(items, &pick);
        g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkMenuShell*, gpointer data) {
                             auto* p = (MenuPick*)data;
                             if (p->loop && g_main_loop_is_running(p->loop)) g_main_loop_quit(p->loop);
                         }),
                         &pick);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), nullptr);
        g_main_loop_run(pick.loop);
        g_main_loop_unref(pick.loop);
        pick.loop = nullptr;
        gtk_widget_destroy(menu);
        return pick.chosen;
    }

    void alert(const std::string& title, const std::string& message) override {
        GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK, "%s", message.c_str());
        gtk_window_set_title(GTK_WINDOW(dialog), title.c_str());
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    int loadSetting(const char* key, int def) override {
        std::string path = util::path_join(plat::app_data_dir(), "settings.ini");
        GKeyFile* file = g_key_file_new();
        GError* error = nullptr;
        int value = def;
        if (g_key_file_load_from_file(file, path.c_str(), G_KEY_FILE_NONE, &error)) {
            GError* getError = nullptr;
            int found = g_key_file_get_integer(file, "PocketDrop", key, &getError);
            if (!getError) value = found;
            if (getError) g_error_free(getError);
        }
        if (error) g_error_free(error);
        g_key_file_unref(file);
        return value;
    }

    void saveSetting(const char* key, int value) override {
        std::string path = util::path_join(plat::app_data_dir(), "settings.ini");
        GKeyFile* file = g_key_file_new();
        g_key_file_load_from_file(file, path.c_str(), G_KEY_FILE_NONE, nullptr);
        g_key_file_set_integer(file, "PocketDrop", key, value);
        gsize size = 0;
        gchar* data = g_key_file_to_data(file, &size, nullptr);
        g_file_set_contents(path.c_str(), data, (gssize)size, nullptr);
        g_free(data);
        g_key_file_unref(file);
    }

    void setTopmost(bool on) override {
        if (window) gtk_window_set_keep_above(GTK_WINDOW(window), on);
    }

    std::string cleanPath(const std::string& path) override {
        char resolved[PATH_MAX];
        if (!realpath(path.c_str(), resolved)) return {};
        struct stat st {};
        return stat(resolved, &st) == 0 ? std::string(resolved) : std::string();
    }
};

struct App {
    LinuxShell shell;
    CairoGfx gfx;
    std::unique_ptr<Ui> ui;
    guint slowTimer = 0;
    GdkCursor* pointer = nullptr;
    GdkCursor* arrow = nullptr;
};

void updateCursor(App* app) {
    if (!app->shell.view || !gtk_widget_get_window(app->shell.view)) return;
    gdk_window_set_cursor(gtk_widget_get_window(app->shell.view), app->ui->wantsPointer() ? app->pointer : app->arrow);
}

void receiveDrop(GtkWidget*, GdkDragContext* context, gint, gint, GtkSelectionData* selection, guint info, guint time,
                 gpointer data) {
    auto* app = (App*)data;
    bool accepted = false;
    if (info == 1) {
        std::vector<std::string> paths;
        if (gchar** uris = gtk_selection_data_get_uris(selection)) {
            for (int i = 0; uris[i]; i++) {
                gchar* file = g_filename_from_uri(uris[i], nullptr, nullptr);
                if (file) paths.emplace_back(file);
                g_free(file);
            }
            g_strfreev(uris);
        }
        if (!paths.empty()) {
            app->ui->addPaths(paths);
            accepted = true;
        }
    } else {
        const guchar* raw = gtk_selection_data_get_data(selection);
        int len = gtk_selection_data_get_length(selection);
        if (raw && len > 0) {
            app->ui->addText(std::string((const char*)raw, (size_t)len));
            accepted = true;
        }
    }
    app->ui->setDragOver(false);
    gtk_drag_finish(context, accepted, FALSE, time);
}

void createWindow(App* app, const std::vector<std::string>& initial) {
    GtkSettings* settings = gtk_settings_get_default();
    if (settings) g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, nullptr);

    app->shell.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->shell.window), "PocketDrop");
    gtk_window_set_default_size(GTK_WINDOW(app->shell.window), 385, 750);
    gtk_window_set_resizable(GTK_WINDOW(app->shell.window), FALSE);
    gtk_window_set_position(GTK_WINDOW(app->shell.window), GTK_WIN_POS_CENTER);

    std::string icon = util::path_join(plat::exe_dir(), "PocketDrop.png");
    if (!plat::file_stat(icon).exists) icon = "src/linux/app.png";
    gtk_window_set_icon_from_file(GTK_WINDOW(app->shell.window), icon.c_str(), nullptr);

    app->shell.view = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->shell.view, 385, 722);
    gtk_widget_set_can_focus(app->shell.view, TRUE);
    gtk_widget_add_events(app->shell.view, GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK |
                                                 GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK |
                                                 GDK_KEY_PRESS_MASK);
    gtk_container_add(GTK_CONTAINER(app->shell.window), app->shell.view);

    app->ui = std::make_unique<Ui>(app->shell);
    app->shell.ui = app->ui.get();

    g_signal_connect(app->shell.view, "draw", G_CALLBACK(+[](GtkWidget* view, cairo_t* cr, gpointer data) -> gboolean {
                         auto* a = (App*)data;
                         a->gfx.begin(cr, (float)gtk_widget_get_scale_factor(view));
                         a->ui->paint(a->gfx);
                         return TRUE;
                     }),
                     app);
    g_signal_connect(app->shell.view, "motion-notify-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventMotion* e, gpointer data) -> gboolean {
                         auto* a = (App*)data;
                         a->ui->mouseMove((float)e->x, (float)e->y);
                         updateCursor(a);
                         return TRUE;
                     }),
                     app);
    g_signal_connect(app->shell.view, "leave-notify-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventCrossing*, gpointer data) -> gboolean {
                         auto* a = (App*)data;
                         a->ui->mouseLeave();
                         updateCursor(a);
                         return TRUE;
                     }),
                     app);
    g_signal_connect(app->shell.view, "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton* e, gpointer data) -> gboolean {
                         if (e->button == 1) ((App*)data)->ui->mouseDown((float)e->x, (float)e->y);
                         return TRUE;
                     }),
                     app);
    g_signal_connect(app->shell.view, "button-release-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton* e, gpointer data) -> gboolean {
                         if (e->button == 1) {
                             auto* a = (App*)data;
                             a->ui->mouseUp((float)e->x, (float)e->y);
                             updateCursor(a);
                         }
                         return TRUE;
                     }),
                     app);
    g_signal_connect(app->shell.view, "scroll-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventScroll* e, gpointer data) -> gboolean {
                         double dx = 0;
                         double dy = 0;
                         float lines = 0;
                         if (gdk_event_get_scroll_deltas((GdkEvent*)e, &dx, &dy)) lines = (float)-dy;
                         else if (e->direction == GDK_SCROLL_UP) lines = 1;
                         else if (e->direction == GDK_SCROLL_DOWN) lines = -1;
                         ((App*)data)->ui->wheel(lines);
                         return TRUE;
                     }),
                     app);
    g_signal_connect(app->shell.window, "key-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventKey* e, gpointer data) -> gboolean {
                         if (!(e->state & GDK_CONTROL_MASK)) return FALSE;
                         auto* a = (App*)data;
                         if (e->keyval == GDK_KEY_v || e->keyval == GDK_KEY_V) a->ui->paste();
                         else if (e->keyval == GDK_KEY_o || e->keyval == GDK_KEY_O) a->ui->browse(false);
                         else if (e->keyval == GDK_KEY_c || e->keyval == GDK_KEY_C) a->ui->copyLink();
                         else return FALSE;
                         return TRUE;
                     }),
                     app);

    GtkTargetEntry targets[] = {{(gchar*)"text/uri-list", 0, 1}, {(gchar*)"UTF8_STRING", 0, 2},
                                {(gchar*)"text/plain", 0, 2}};
    gtk_drag_dest_set(app->shell.view, GTK_DEST_DEFAULT_ALL, targets, 3, GDK_ACTION_COPY);
    g_signal_connect(app->shell.view, "drag-motion",
                     G_CALLBACK(+[](GtkWidget*, GdkDragContext* c, gint, gint, guint time, gpointer data) -> gboolean {
                         ((App*)data)->ui->setDragOver(true);
                         gdk_drag_status(c, GDK_ACTION_COPY, time);
                         return TRUE;
                     }),
                     app);
    g_signal_connect(app->shell.view, "drag-leave",
                     G_CALLBACK(+[](GtkWidget*, GdkDragContext*, guint, gpointer data) {
                         ((App*)data)->ui->setDragOver(false);
                     }),
                     app);
    g_signal_connect(app->shell.view, "drag-data-received", G_CALLBACK(receiveDrop), app);

    g_signal_connect(app->shell.window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
                         auto* a = (App*)data;
                         if (a->slowTimer) {
                             g_source_remove(a->slowTimer);
                             a->slowTimer = 0;
                         }
                         if (a->ui) a->ui->shutdown();
                         gtk_main_quit();
                     }),
                     app);

    GdkDisplay* display = gtk_widget_get_display(app->shell.window);
    app->pointer = gdk_cursor_new_from_name(display, "pointer");
    app->arrow = gdk_cursor_new_from_name(display, "default");

    app->ui->start();
    if (!initial.empty()) app->ui->addPaths(initial);
    app->slowTimer = g_timeout_add(500, +[](gpointer data) -> gboolean {
        auto* a = (App*)data;
        if (a->ui) a->ui->tick();
        return G_SOURCE_CONTINUE;
    }, app);

    gtk_widget_show_all(app->shell.window);
    gtk_widget_grab_focus(app->shell.view);
}

} // namespace

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    std::vector<std::string> paths;
    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-') paths.emplace_back(argv[i]);

    App app;
    createWindow(&app, paths);
    gtk_main();
    if (app.pointer) g_object_unref(app.pointer);
    if (app.arrow) g_object_unref(app.arrow);
    app.ui.reset();
    return 0;
}
