/* ibmsl.c ---
 *
 * Filename: ibmsl.c
 * Description:
 * Author: Andrey Andruschenko
 * Maintainer:
 * Created: Пт июл  3 15:52:15 2015 (+0300)
 * Version:
 * Last-Updated:
 *           By:
 *     Update #: 975
 * URL:
 * Keywords:
 * Compatibility:
 *
 */

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <ftw.h>
#include <pthread.h>

#include <jansson.h>
#include <curl/curl.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "ibmsl.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

extern profile_data_t profile;
extern f_list_t files;

extern char* get_file_md5_hash(const char*);
extern void *login_to_service(void*);
extern char *get_start_dir(void);
extern void *scan_dirs(void*);
extern void *upload_to_ibroadcast(void*);

gboolean on_login_button_clicked(GtkButton*, lf_thread_data_t*);
gboolean on_search_button_clicked(GtkButton*, sf_thread_data_t*);
gboolean on_login_win_destroy(GtkWidget*, gpointer);
gboolean on_uploading_win_show(GtkWidget*, gpointer);
gboolean on_finished_win_show(GtkWidget*, gpointer);
gboolean destroy_widget(GtkWidget*, gpointer);
gboolean destroy_object(GtkWidget*, gpointer);

sf_thread_data_t *fs;

gboolean uploading_pause;
app_wins_t *wins;

pthread_mutex_t uploaded_mtx;
pthread_mutex_t files_mtx;
pthread_mutex_t up_sbar_mtx;
pthread_mutex_t rt_mtx;

static GtkWidget* create_ui(const char *path) {

    GtkWidget *fs_button;
    GError* error = NULL;
    GtkBuilder *builder = NULL;
    GdkRGBA win_color, fields_color, font_color;
    finished_data_t *finished_data;
    lf_thread_data_t *tc;
    up_win_data_t *uwd;

    gdk_rgba_parse(&win_color, __WIN_COLOR_);
    gdk_rgba_parse(&fields_color, __FORM_COLOR_);
    gdk_rgba_parse(&font_color, __FONT_COLOR_);

    __MALLOC(wins, app_wins_t*, sizeof(app_wins_t));

    builder = gtk_builder_new();

    if(!gtk_builder_add_from_file(builder, path, &error)) {
        g_critical("Can't load UI description. %s", error->message);
        g_error_free(error);
        return NULL;
    }

    gtk_builder_connect_signals(builder, NULL);

    /* Build splashscreen */
    if(!(wins->splash_win = GTK_WIDGET(gtk_builder_get_object(builder, "splash_win")))) {
        g_critical("Error while retrieve splash_win widget!\n");
        return NULL;
    }

    /* Build logon screen */
    /* Allocate memory for a data passed to login_to_service thread */
    __MALLOC(tc, lf_thread_data_t*, sizeof(lf_thread_data_t));
    __MALLOC(tc->btn, msl_button_t*, sizeof(msl_button_t));
    __MALLOC(tc->sbar, msl_label_t*, sizeof(msl_label_t));
    __MALLOC(tc->sbar->text, char*, sizeof(char) * __MAX_SBAR_TEXT_LEN_);

    if(!(wins->logon_win = GTK_WIDGET(gtk_builder_get_object(builder, "login_win")))) {
        g_critical ("Error while retrieve login_win widget!\n");
        return NULL;
    }

    tc->window = wins->logon_win;

    if(!(tc->sbar->label = GTK_WIDGET(gtk_builder_get_object(builder, "login_status_label")))) {
        g_critical ("Error while retrieve login_status_label widget!\n");
        return NULL;
    }
    gtk_widget_override_color(tc->sbar->label, GTK_STATE_FLAG_NORMAL, &font_color);

    if(!(tc->btn->button = GTK_WIDGET(gtk_builder_get_object(builder, "login_button")))) {
        g_critical ("Error while retrieve login_button widget!\n");
        return NULL;
    }

    g_signal_connect(tc->btn->button, "clicked", G_CALLBACK(on_login_button_clicked), tc);

    gtk_widget_override_background_color(tc->window, GTK_STATE_NORMAL, &win_color);

    /* Build folder selection screen */
    /* Allocate memory for a data passed to directory scan thread */
    __MALLOC(fs, sf_thread_data_t*, sizeof(sf_thread_data_t));
    __MALLOC(fs->btn, msl_button_t*, sizeof(msl_button_t));
    __MALLOC(fs->sbar, msl_label_t*, sizeof(msl_label_t));
    __MALLOC(fs->sbar->text, char*, sizeof(char) * __MAX_SBAR_TEXT_LEN_);

    if(!(wins->selectf_win = GTK_WIDGET(gtk_builder_get_object(builder, "select_folder_win")))) {
        g_critical("Error while retrieve select_folder_win widget!\n");
        return NULL;
    }
    fs->window = wins->selectf_win;

    if(!(fs_button = GTK_WIDGET(gtk_builder_get_object(builder, "filechooser_button_select_folder")))) {
        g_critical("Error while retrieve filechooser_button_select_folder widget!\n");
        return NULL;
    }
    if((profile.scan_dir = get_start_dir()) == NULL) {
        g_critical("Can't get start directory!");
        return NULL;
    }
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fs_button), profile.scan_dir);

    if(!(fs->btn->button = GTK_WIDGET(gtk_builder_get_object(builder, "button_search_select_folder")))) {
        g_critical ("Error while retrieve button_search_select_folder widget!\n");
        return NULL;
    }
    g_signal_connect(fs->btn->button, "clicked", G_CALLBACK(on_search_button_clicked), fs);

    if(!(fs->sbar->label = GTK_WIDGET(gtk_builder_get_object(builder, "selectf_scan_label")))) {
        g_critical("Error while retrieve selectf_scan_label widget!\n");
        return NULL;
    }
    gtk_widget_override_color(fs->sbar->label, GTK_STATE_FLAG_NORMAL, &font_color);

    gtk_widget_override_background_color(wins->selectf_win, GTK_STATE_NORMAL, &win_color);

    /* Build filelist screen */
    if(!(wins->filelist_win = GTK_WIDGET(gtk_builder_get_object(builder, "filelist_win")))) {
        g_critical("Error while retrieve filelist_win widget!\n");
        return NULL;
    }

    gtk_widget_override_background_color(wins->filelist_win, GTK_STATE_NORMAL, &win_color);

    /* Build uploading screen */
    if(!(wins->uploading_win = GTK_WIDGET(gtk_builder_get_object(builder, "uploading_win")))) {
        g_critical("Error while retrieve uploading_win widget!\n");
        return NULL;
    }

    __MALLOC(uwd, up_win_data_t*, sizeof(up_win_data_t));
    __MALLOC(uwd->c_label, msl_label_t*, sizeof(msl_label_t));
    __MALLOC(uwd->c_label->text, char*, sizeof(char) * (strlen("Uploaded:       files,       remaining.       files skipped(already uploaded).        errored.") + 1));
    __MALLOC(uwd->sbar, msl_label_t*, sizeof(msl_label_t));
    __MALLOC(uwd->sbar->text, char*, sizeof(char) * __MAX_SBAR_TEXT_LEN_);

    if(!(uwd->container = GTK_WIDGET(gtk_builder_get_object(builder, "uploading_frame_alignment")))) {
        g_critical("Error while retrieve uploading_frame_alignment widget!\n");
        return NULL;
    }
    if(!(uwd->c_label->label = GTK_WIDGET(gtk_builder_get_object(builder, "uploading_count_label")))) {
        g_critical("Error while retrieve uploading_count_label widget!\n");
        return NULL;
    }
    if(!(uwd->sbar->label = GTK_WIDGET(gtk_builder_get_object(builder, "uploading_status_label")))) {
        g_critical("Error while retrieve uploading_status_label widget!\n");
        return NULL;
    }
    gtk_widget_override_color(uwd->sbar->label, GTK_STATE_FLAG_NORMAL, &font_color);

    g_signal_connect(wins->uploading_win, "show", G_CALLBACK(on_uploading_win_show), uwd);

    gtk_widget_override_background_color(wins->uploading_win, GTK_STATE_NORMAL, &win_color);

    /* Build finished screen */
    if(!(wins->finished_win = GTK_WIDGET(gtk_builder_get_object(builder, "finished_win")))) {
        g_critical("Error while retrieve finished_win widget!\n");
        return NULL;
    }
    __MALLOC(finished_data, finished_data_t*, sizeof(finished_data_t));

    if(!(finished_data->found = GTK_WIDGET(gtk_builder_get_object(builder, "finished_found_label_val")))) {
        g_critical("Error while retrieve finished_found_label_val widget!\n");
        return NULL;
    }
    if(!(finished_data->uploaded = GTK_WIDGET(gtk_builder_get_object(builder, "finished_uploaded_label_val")))) {
        g_critical("Error while retrieve finished_uploaded_label_val widget!\n");
        return NULL;
    }
    if(!(finished_data->skipped = GTK_WIDGET(gtk_builder_get_object(builder, "finished_skipped_label_val")))) {
        g_critical("Error while retrieve finished_skipped_label_val widget!\n");
        return NULL;
    }

    g_signal_connect(wins->finished_win, "show", G_CALLBACK(on_finished_win_show), finished_data);
    gtk_widget_override_background_color(wins->finished_win, GTK_STATE_NORMAL, &win_color);

    g_object_unref(builder);

    return wins->splash_win;
}

gboolean on_splash_win_key_press_event(GtkWidget *window, GdkEvent *event, gpointer ptr) {

    GdkEventKey key;
    char *keyname;

    key = event->key;
    keyname = gdk_keyval_name(key.keyval);

    if(strcasecmp("Escape", keyname) == 0) {
        gtk_main_quit();
    }

    gtk_widget_destroy(window);

    return FALSE;
}

gboolean on_splash_win_destroy(GtkWidget *window, gpointer ptr) {

    gtk_widget_show(wins->logon_win);

    return FALSE;
}

gboolean on_pass_entry_insert_text(GtkEditable* edit, gpointer data) {

    const gchar* content = gtk_entry_get_text(GTK_ENTRY(edit));

    if(profile.pass_len <= strlen(content)) {
        if((profile.pass = (char*)realloc(profile.pass, sizeof(char) *(strlen(content) + 1))) == NULL) {
            g_critical("Memory allocation error!");
            exit(EXIT_FAILURE);
        }
        profile.pass_len = strlen(content) + 1;
    }

    strcpy(profile.pass, content);
    return TRUE;
}

gboolean on_login_entry_insert_text(GtkEditable* edit, gpointer data) {

    const gchar* content = gtk_entry_get_text(GTK_ENTRY(edit));

    if(profile.login_len <= strlen(content)) {
        if((profile.login = (char*)realloc(profile.login, sizeof(char) *(strlen(content) + 1))) == NULL) {
            g_critical("Memory allocation error!");
            exit(EXIT_FAILURE);
        }
        profile.login_len = strlen(content) + 1;
    }

    strcpy(profile.login, content);
    return TRUE;
}

gboolean catch_escape_key_press_event(GtkWidget *window, GdkEvent *event, gpointer ptr) {

    GdkEventKey key;
    char *keyname;

    key = event->key;
    keyname = gdk_keyval_name(key.keyval);

    if(strcasecmp("Escape", keyname) == 0) {
        gtk_main_quit();
    }

    return FALSE;
}

gboolean catch_tab_event(GtkWidget *window, GdkEvent *event, gpointer ptr) {

    GdkEventKey key;
    char *keyname;

    key = event->key;
    keyname = gdk_keyval_name(key.keyval);

    if(strcasecmp("Tab", keyname) == 0) {
        gtk_widget_grab_focus(GTK_WIDGET(ptr));
        return TRUE;
    }
    return FALSE;
}

gboolean on_spin_button_changed(GtkSpinButton *spin, gpointer ptr) {

    GtkAdjustment *adjustment;

    adjustment = gtk_spin_button_get_adjustment(spin);
    profile.uploaders = gtk_adjustment_get_value(adjustment);

    return FALSE;
}

gboolean on_login_button_clicked(GtkButton *button, lf_thread_data_t *ptr) {

    pthread_t thread;
    pthread_attr_t thread_attr;

    if(strlen(profile.login) == 0 || strlen(profile.pass) == 0) {
        gtk_label_set_text(GTK_LABEL(ptr->sbar->label), "One of mandatory fields is empty!");
        exit(EXIT_FAILURE);
    }

    if(pthread_attr_init(&thread_attr)) {
        g_critical( "error while init pthread attr struct");
        exit(EXIT_FAILURE);
    }

    if((pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED))) {
        g_critical("error while set pthread attributes");
        exit(EXIT_FAILURE);
    }

    if(pthread_create(&thread, &thread_attr, login_to_service, ptr)) {
        g_critical("Login thread creation failure: %s", strerror(errno));
        exit(EXIT_FAILURE);
    } else
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);

    return TRUE;
}

gboolean set_button_state(gpointer arg) {

    msl_button_t *ptr = (msl_button_t *)arg;

    ptr->state == FALSE ? gtk_widget_set_sensitive(GTK_WIDGET(ptr->button), FALSE) :\
        gtk_widget_set_sensitive(GTK_WIDGET(ptr->button), TRUE);

    return G_SOURCE_REMOVE;
}

gboolean login_win_close(gpointer arg) {

    lf_thread_data_t *ptr = (lf_thread_data_t *)arg;

    free(ptr->sbar->text);
    free(ptr->sbar);
    free(ptr->btn);
    free(ptr);

    gtk_widget_destroy(wins->logon_win);
    gtk_widget_show(wins->selectf_win);

    return G_SOURCE_REMOVE;
}

gboolean on_folder_selected(GtkFileChooserButton *widget, gpointer ptr) {

    gchar *selected;

    if((selected = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(widget))) == NULL) {
        g_critical("Error while get selected folder!");
        return FALSE;
    }

    if(strlen(profile.scan_dir) <= strlen(selected)) {
        if((profile.scan_dir = realloc(profile.scan_dir, sizeof(char) * (strlen(selected) + 1))) == NULL) {
            g_critical("Memory allocation error!");
            exit(EXIT_FAILURE);
        }
    }

    strcpy(profile.scan_dir, selected);
    free(selected);

    return TRUE;
}

gboolean on_search_button_clicked(GtkButton *button, sf_thread_data_t *ptr) {

    pthread_t thread;
    pthread_attr_t thread_attr;

    if(pthread_attr_init(&thread_attr)) {
        g_critical( "error while init thread attr struct");
        exit(EXIT_FAILURE);
    }

    if((pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_DETACHED))) {
        g_critical("error while set thread attributes");
        exit(EXIT_FAILURE);
    }

    if(pthread_create(&thread, &thread_attr, scan_dirs, ptr)) {
        g_critical("Directory scan thread creation failure: %s", strerror(errno));
        exit(EXIT_FAILURE);
    } else
        gtk_widget_set_sensitive(GTK_WIDGET(button), FALSE);

    return TRUE;
}

gboolean file_list_show(gpointer ptr) {

    gtk_widget_hide(wins->selectf_win);
    gtk_widget_show(wins->filelist_win);

    return G_SOURCE_REMOVE;
}

gboolean on_filelist_win_show(GtkWidget *widget, gpointer arg) {

    size_t i = 0;
    GtkTextBuffer *buffer;

    if((buffer = gtk_text_buffer_new(NULL)) == NULL) {
        g_critical("Can't create text buffer for a file list!");
        exit(EXIT_FAILURE);
    }

    gtk_text_view_set_buffer(GTK_TEXT_VIEW(arg), buffer);

    for(i = 0; i < files.count; i++) {
        gtk_text_buffer_insert_at_cursor(buffer,files.list[i]->name,-1);
        gtk_text_buffer_insert_at_cursor(buffer,"\n",-1);
    }

    return FALSE;
}

gboolean change_lable_on_filelist_show(GtkWidget *widget, gpointer arg) {

    GtkLabel *label = (GtkLabel *)arg;
    char *str;

    __MALLOC(str, char*, strlen(" files where found. Ready to start uploading.") + 8);
    sprintf(str, "%zd%s", files.count, " files where found. Ready to start uploading.");

    gtk_label_set_text(label, str);
    free(str);

    return FALSE;
}

gboolean on_begin_uploading_button_clicked(GtkBuilder *button, gpointer ptr) {

    gtk_widget_hide(wins->filelist_win);
    gtk_widget_show(wins->uploading_win);

    return TRUE;
}

gboolean on_uploading_win_show(GtkWidget *widget, gpointer arg) {

    up_win_data_t *ptr = (up_win_data_t*)arg;
    GtkWidget *pbar, *grid, *inner_grid, *label;
    pthread_t thread;
    uploader_data_t *tc;
    GdkRGBA color;
    int indicators, i = 0;
    PangoFontDescription *font_name;

    indicators = (profile.uploaders < files.count) ? profile.uploaders : files.count;

    files.remaining = files.count;
    files.uploaded = 0;
    uploading_pause = FALSE;

    if((grid = gtk_grid_new()) == NULL) {
        g_critical("Can't create grid!");
        exit(EXIT_FAILURE);
    }

    g_signal_connect(wins->uploading_win, "hide", G_CALLBACK(destroy_widget), grid);

    gtk_label_set_text(GTK_LABEL(ptr->sbar->label), "");

    gdk_rgba_parse(&color, __UPLOADING_FONT_COLOR_);
    font_name = pango_font_description_from_string(__UPLOADING_FONT_);

    gtk_grid_set_row_homogeneous(GTK_GRID(grid),TRUE);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid),TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 20);

    gtk_container_add(GTK_CONTAINER(ptr->container), grid);

    sprintf(ptr->c_label->text, __COUNT_LABEL_TEMPLATE_, files.uploaded, files.count, files.skipped, files.errored);

    gtk_label_set_text(GTK_LABEL(ptr->c_label->label), ptr->c_label->text);

    for(i = 0; i < indicators; i++) {
        /* Create inner grid for a label and progress bar */
        if((inner_grid = gtk_grid_new()) == NULL) {
            g_critical("Can't create grid!");
            exit(EXIT_FAILURE);
        }

        gtk_grid_set_row_homogeneous(GTK_GRID(inner_grid),TRUE);
        gtk_grid_set_column_homogeneous(GTK_GRID(inner_grid),TRUE);

        gtk_grid_set_row_spacing(GTK_GRID(inner_grid), 0);
        gtk_grid_set_column_spacing(GTK_GRID(inner_grid), 0);

        gtk_grid_attach(GTK_GRID(grid), inner_grid, 0, i, 1, 1);

        if((label = gtk_label_new("")) == NULL) {
            g_critical("Can't create %d's label!", i);
            exit(EXIT_FAILURE);
        }

        gtk_label_set_max_width_chars(GTK_LABEL(label), __UPLOADING_LABEL_TEXT_LEN_);
        gtk_widget_set_halign(GTK_WIDGET(label), GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_override_color(label, GTK_STATE_FLAG_NORMAL, &color);
        gtk_widget_override_font(label, font_name);

        if((pbar = gtk_progress_bar_new()) == NULL) {
            g_critical("Can't create %d's progress bar!", i);
            exit(EXIT_FAILURE);
        }

        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(pbar), 0.0);
        gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(pbar), FALSE);
        gtk_widget_set_halign(GTK_WIDGET(pbar), GTK_ALIGN_FILL);

        gtk_grid_attach(GTK_GRID(inner_grid), label, 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(inner_grid), pbar, 0, 1, 1, 1);

        __MALLOC(tc, uploader_data_t*, sizeof(uploader_data_t));
        __MALLOC(tc->progress, msl_progress_t*, sizeof(msl_progress_t));
        __MALLOC(tc->name_label, msl_label_t*, sizeof(msl_label_t));

        tc->name_label->label = label;
        tc->sbar = ptr->sbar;

        tc->progress->indicator = pbar;
        tc->count_l = ptr->c_label;

        if(pthread_create(&thread, NULL, upload_to_ibroadcast, tc)) {
            g_critical("Uploader thread creation failure: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    gtk_widget_show_all(widget);

    return FALSE;
}

gboolean set_progress_fraction(gpointer arg) {

    msl_progress_t *ptr = (msl_progress_t*)arg;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ptr->indicator),ptr->value);

    return G_SOURCE_REMOVE;
}

gboolean set_label_text(gpointer arg) {

    msl_label_t *ptr = (msl_label_t*)arg;

    gtk_label_set_text(GTK_LABEL(ptr->label), ptr->text);

    return G_SOURCE_REMOVE;
}

gboolean on_uploading_pause_click(GtkWidget *widget, gpointer arg) {

    if(uploading_pause == FALSE) {
        gtk_button_set_label(GTK_BUTTON(widget), "   Resume uploading   ");
        uploading_pause = TRUE;
    } else {
        gtk_button_set_label(GTK_BUTTON(widget), "   Pause uploading   ");
        uploading_pause = FALSE;
    }

    return FALSE;
}

gboolean finished_screen_show(gpointer arg) {

    gtk_widget_hide(wins->uploading_win);
    gtk_widget_show(wins->finished_win);

    return G_SOURCE_REMOVE;
}

gboolean on_finished_win_show(GtkWidget *widget, gpointer arg) {

    finished_data_t *ptr = (finished_data_t*)arg;
    char *label_text;

    __MALLOC(label_text, char*, sizeof(char) * 13);

    sprintf(label_text, "%zu files", files.count + files.skipped);
    gtk_label_set_text(GTK_LABEL(ptr->found), label_text);

    sprintf(label_text, "%zu files", files.uploaded);
    gtk_label_set_text(GTK_LABEL(ptr->uploaded), label_text);

    sprintf(label_text, "%zu files", files.skipped);
    gtk_label_set_text(GTK_LABEL(ptr->skipped), label_text);

    free(label_text);

    return FALSE;
}

gboolean on_hide_result_win(GtkWidget *widget, gpointer ptr) {

    files.idx = 0;
    files.count = 0;
    files.skipped = 0;
    files.uploaded = 0;
    files.remaining = 0;
    files.errored = 0;

    return FALSE;
}

gboolean on_done_button_click(GtkWidget *widget, gpointer ptr) {

    gtk_widget_hide(wins->finished_win);
    gtk_widget_show(wins->selectf_win);
    return TRUE;
}

gboolean destroy_widget(GtkWidget *widget, gpointer ptr) {

    gtk_widget_destroy(GTK_WIDGET(ptr));
    return FALSE;
}

int main (int argc, char **argv) {

    int i = 0;
    GtkWidget *window;
    char *resource_path = NULL;
    const char *ui_search_paths[] = {
        "share/ui/",
        "/usr/local/share/mediasynclite/",
        "/usr/share/mediasynclite/",
        "$out/share/mediasynclite/",
        NULL
    };

    __MALLOC(profile.login, char*, sizeof(char) * __MAX_LOGIN_LEN_);
    __MALLOC(profile.pass, char*, sizeof(char) * __MAX_PASS_LEN_);

    if(curl_global_init(CURL_GLOBAL_SSL) != 0) {
        g_critical("libcurl initialization error!");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_init(&uploaded_mtx, NULL);
    pthread_mutex_init(&files_mtx, NULL);
    pthread_mutex_init(&up_sbar_mtx, NULL);
    pthread_mutex_init(&rt_mtx, NULL);

    while(ui_search_paths[i] != NULL) {
        __MALLOC(resource_path, char*, sizeof(char) * (strlen(ui_search_paths[i]) + strlen(__APP_UI_) + 1));
        sprintf(resource_path, "%s%s", ui_search_paths[i], __APP_UI_);
        if(access(resource_path, F_OK | R_OK) == 0)
            break;
        free(resource_path);
        resource_path = NULL;
        i++;
    }

    if(resource_path == NULL) {
        g_critical("UI description not found.");
        exit(EXIT_FAILURE);
    }

    gtk_init(&argc, &argv);

    if((window = create_ui(resource_path)) == NULL)
        exit(EXIT_FAILURE);

    free(resource_path);

    gtk_widget_show(window);
    gtk_main();

    free(resource_path);
    curl_global_cleanup();

    return 0;
}
/* ibmsl.c ends here */
