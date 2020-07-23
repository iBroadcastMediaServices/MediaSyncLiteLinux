/* ibmsl.h ---
 *
 * Filename: ibmsl.h
 * Description:
 * Author: Andrey Andruschenko
 * Maintainer:
 * Created: Пн июл  6 16:19:11 2015 (+0300)
 * Version:
 * Last-Updated:
 *           By:
 *     Update #: 146
 * URL:
 * Keywords:
 * Compatibility:
 *
 */

#ifndef __IBMSL_H_
#define __IBMSL_H_

#define __APP_UI_            "ui.glade"

#define __JSON_API_URL_      "https://json.ibroadcast.com/s/JSON/"
#define __UPLOAD_URL_        "https://sync.ibroadcast.com"
#define __UPLOAD_TYPE_       "Content-Type: multipart/form-data"
#define __UA_                "User-Agent: MediaSync Lite for Linux v0.4.2"
#define __METHOD_            "MediaSync Lite for Linux v0.4.2"

#define __MUSIC_DIR          "/Music"

#define MINIMAL_PROGRESS_FUNCTIONALITY_INTERVAL     0.4
#define __MAX_LOGIN_LEN_                            64
#define __MAX_PASS_LEN_                             64
#define __DEFAULT_UPLOADERS_                        3
#define __MAX_SBAR_TEXT_LEN_                        75
#define __UPLOADING_LABEL_TEXT_LEN_                 60

#define __UPLOADING_FONT_                           "Arial 10"
#define __UPLOADING_FONT_COLOR_                     "#969696"
#define __WIN_COLOR_                                "#000000"
#define __FONT_COLOR_                               "#dddddd"
#define __FORM_COLOR_                               "#464646"
#define __COUNT_LABEL_TEMPLATE_                     "Uploaded: %zu files, %zu remaining. %zu files skipped (already uploaded). %zu errored."

typedef struct __user_profile_data_ {
    char *login;
    unsigned int login_len;
    char *pass;
    unsigned int pass_len;
    unsigned int uploaders;
    char *user_id;
    char *token;
    char *scan_dir;
    char **supported_exts;
    char **md5s;
    unsigned int running_threads;
} profile_data_t;

typedef struct __memory_chunk_ {
    char *memory;
    size_t size;
} mem_ch_t;

typedef struct __file_info_ {
    char *name;
    char *md5;
} f_info_t;

typedef struct __file_list_ {
    size_t count;
    size_t skipped;
    size_t uploaded;
    size_t remaining;
    size_t idx;
    size_t errored;
    f_info_t **list;
} f_list_t;

typedef struct __msl_progress_t_ {
    gdouble value;
    GtkWidget *indicator;
} msl_progress_t;

typedef struct __msl_spinner_t_ {
    gboolean state;
    GtkWidget *spinner;
} msl_spinner_t;

typedef struct __msl_button_t_ {
    gboolean state;
    GtkWidget *button;
} msl_button_t;

typedef struct __msl_label_t_ {
    char *text;
    GtkWidget *label;
} msl_label_t;

typedef struct __lf_thread_data_t_ {
    msl_label_t *sbar;
    msl_button_t *btn;
    GtkWidget *window;
} lf_thread_data_t;

typedef struct __sf_thread_data_t_ {
    msl_label_t *sbar;
    msl_button_t *btn;
    GtkWidget *window;
} sf_thread_data_t;

typedef struct __app_wins_t_ {
    GtkWidget *splash_win;
    GtkWidget *logon_win;
    GtkWidget *selectf_win;
    GtkWidget *filelist_win;
    GtkWidget *uploading_win;
    GtkWidget *finished_win;
} app_wins_t;

typedef struct __finished_win_data_t_ {
    GtkWidget *uploaded;
    GtkWidget *found;
    GtkWidget *skipped;
} finished_data_t;

typedef struct __up_win_data_t_ {
    GtkWidget *container;
    msl_label_t *c_label;
    msl_label_t *sbar;
} up_win_data_t;

typedef struct __uploader_data_t_ {
    msl_progress_t *progress;
    msl_label_t *count_l;
    msl_label_t *name_label;
    msl_label_t *sbar;
} uploader_data_t;

typedef struct __progress_t_ {
    double lastruntime;
    msl_progress_t *progress;
    gboolean is_active;
    CURL *curl;
} progress_t;

#define __MALLOC(v,t,s)                             \
    if((v = (t)malloc(s)) == NULL) {                \
        g_critical("Memory allocation error!\n");   \
        exit(EXIT_FAILURE); }

#define __STRNCPY(t,f,c)                                \
    if(strlen(f) >= (c-1)) {                            \
        strncpy(t, f, c);                               \
        *((char*)((size_t)(t) + (size_t)(c-3))) = '.';  \
        *((char*)((size_t)(t) + (size_t)(c-2))) = '.';  \
        *((char*)((size_t)(t) + (size_t)(c-1))) = '.';  \
        *((char*)((size_t)(t) + (size_t)c)) = '\0';     \
    } else                                              \
        strcpy(t, f);                                   \

#endif
/* ibmsl.h ends here */
