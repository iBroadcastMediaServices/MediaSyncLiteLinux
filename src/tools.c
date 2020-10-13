/* tools.c ---
 *
 * Filename: tools.c
 * Description:
 * Author: Andrey Andruschenko
 * Maintainer:
 * Created: Пн июл  6 17:01:44 2015 (+0300)
 * Version:
 * Last-Updated:
 *           By:
 *     Update #: 578
 * URL:
 * Keywords:
 * Compatibility:
 *
 */

#define _GNU_SOURCE
#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pwd.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <openssl/md5.h>
#include <jansson.h>
#include <curl/curl.h>
#include <gtk/gtk.h>

#include "ibmsl.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

char* get_file_md5_hash(const char*);
void *login_to_service(void*);
void *scan_dirs(void*);

static mem_ch_t* request(const char*, const char*, const char*);
static size_t wc_cb(void*, size_t, size_t, void*);
static int nftw_cb(const char *, const struct stat*, int, struct FTW*);

extern gboolean set_button_state(gpointer);
extern gboolean login_win_close(gpointer);
extern gboolean set_progress_fraction(gpointer);
extern gboolean set_label_text(gpointer);
extern gboolean finished_screen_show(gpointer);
extern gboolean file_list_show(gpointer);

extern lf_thread_data_t *fs;
extern pthread_mutex_t uploaded_mtx;
extern pthread_mutex_t files_mtx;
extern pthread_mutex_t up_sbar_mtx;
extern pthread_mutex_t rt_mtx;
extern gboolean uploading_pause;

profile_data_t profile = {
    .login = NULL,
    .login_len = __MAX_LOGIN_LEN_,
    .pass = NULL,
    .pass_len = __MAX_PASS_LEN_,
    .uploaders = __DEFAULT_UPLOADERS_,
    .user_id = NULL,
    .token = NULL,
    .scan_dir = NULL,
    .supported_exts = NULL,
    .md5s = NULL,
    .running_threads = 0
};

f_list_t files = {
    .idx = 0,
    .count = 0,
    .skipped = 0,
    .uploaded = 0,
    .errored = 0,
    .list = NULL
};

static mem_ch_t* request(const char *url, const char *data, const char *type) {

    CURL *curl = NULL;
    CURLcode status;
    struct curl_slist *headers = NULL;
    char *c_type = NULL;
    long code;
    mem_ch_t *chunk;

    chunk = malloc(sizeof(mem_ch_t));

    chunk->memory = malloc(1);
    chunk->size = 0;

    curl = curl_easy_init();

    if(!curl)
        return NULL;

    curl_easy_setopt(curl, CURLOPT_URL, url);

    __MALLOC(c_type, char*, sizeof(char) * (strlen(type) + strlen("Content-Type: ") + 1));
    sprintf(c_type, "Content-Type: %s", type);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wc_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);

    headers = curl_slist_append(headers, __UA_);
    headers = curl_slist_append(headers, c_type);

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);

    status = curl_easy_perform(curl);

    if(status != 0) {
        g_critical("error: unable to POST data to %s:", url);
        g_critical("%s", curl_easy_strerror(status));
        return NULL;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

    free(c_type);

    curl_easy_cleanup(curl);

    curl_slist_free_all(headers);


    if(code != 200) {

        return NULL;
    } else {

        return chunk;
    }
}

/* Write memory callback function*/
static size_t wc_cb(void *contents, size_t size, size_t nmemb,
                    void *userp) {

    size_t realsize = size * nmemb;
    mem_ch_t *mem = (mem_ch_t *)userp;

    mem->memory = realloc(mem->memory, mem->size + realsize + 1);
    if(mem->memory == NULL) {
        g_critical("not enough memory (realloc returned NULL)");
        return 0;
    }

    char *memory = mem->memory;
    memcpy(&memory[mem->size], contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

char* get_file_md5_hash(const char *fname) {

    int fd, i;
    struct stat statbuf;
    char *file_buffer;
    unsigned char result[MD5_DIGEST_LENGTH];
    char *hash, buf[3];

    if((fd = open(fname, O_RDONLY)) < 0) {
        g_warning("%s : %s", fname, strerror(errno));
        return NULL;
    }

    if(fstat(fd, &statbuf) < 0)
        exit(EXIT_FAILURE);

    __MALLOC(hash, char*, sizeof(char) * (MD5_DIGEST_LENGTH * 2 + 1));

    memset((void *)hash, 0, (MD5_DIGEST_LENGTH * 2 + 1));

    file_buffer = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);

    MD5((unsigned char*) file_buffer, statbuf.st_size, result);
    munmap(file_buffer, statbuf.st_size);

    close(fd);

    for(i=0;i<MD5_DIGEST_LENGTH;i++) {
        sprintf(buf, "%02x",result[i]);
        strncat(hash, buf, strlen(buf));
    }

    return hash;
}

void *login_to_service(void *arg) {

    g_message("login()");

    lf_thread_data_t *ptr = (lf_thread_data_t *)arg;
    json_t *root;
    json_error_t error;
    mem_ch_t *response;
    char *data = NULL, *ent_value = NULL;
    size_t i,j;

    __STRNCPY(ptr->sbar->text, "Connecting to iBroadcast...", __MAX_SBAR_TEXT_LEN_);

    gdk_threads_add_idle(set_label_text, ptr->sbar);

    ptr->btn->state = TRUE;

/* Build request JSON object */
    if((root = json_pack("{ss, ss, ss, ss, ss, si}", "mode", "status", "email_address", profile.login, \
                         "password", profile.pass, "version", ".1", "client", "ibroadcast-c-uploader",\
                         "supported_types", 1)) == NULL) {
        __STRNCPY(ptr->sbar->text, "Failed to build JSON object!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        g_critical("Failed to build JSON object!\n");
        return FALSE;
    }

    if((data = json_dumps(root, JSON_INDENT(4))) == NULL) {
        __STRNCPY(ptr->sbar->text, "JSON string build error!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        g_critical("JSON string build error\n");
        return FALSE;
    }
    json_decref(root);

/* Send request */
    if((response = request(__JSON_API_URL_, data, "application/json")) == NULL) {
        __STRNCPY(ptr->sbar->text, "Error when retrieve data from service!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        g_critical("Error when retrieve data from service!\n");
        return FALSE;
    }

    /*Parse response*/
    if((root = json_loads(response->memory, 0, &error)) == NULL) {
        __STRNCPY(ptr->sbar->text, "Error in JSON response!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        g_critical("error in reply: on line %d: %s\n", error.line, error.text);
        return FALSE;
    }

    free(data);
    free(response->memory);
    free(response);

    if(!json_is_object(root)) {
        __STRNCPY(ptr->sbar->text, "Error in JSON response!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        g_critical("Malformed JSON object:\n%s\n", json_dumps(root, JSON_INDENT(4)));
        return FALSE;
    }

    if(json_boolean_value(json_object_get(root, "result")) == 0) {
        __STRNCPY(ptr->sbar->text, json_string_value(json_object_get(root, "message")), __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        return FALSE;
    }

    if((ent_value = (char*)json_string_value(json_object_get(json_object_get(root, "user"), "token"))) == NULL) {
        __STRNCPY(ptr->sbar->text, "Can't get auth token from JSON object!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        g_critical("Can't get auth token from JSON object!\n");
        return FALSE;
    }
    __MALLOC(profile.token, char*, sizeof(char) * (strlen(ent_value) + 1));
    strcpy(profile.token, ent_value);

    if((ent_value = (char*)json_string_value(json_object_get(json_object_get(root, "user"), "id"))) == NULL) {
        __STRNCPY(ptr->sbar->text, "Can't get user ID from JSON object!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        g_critical("Can't get user ID from JSON object!\n");
        free(profile.token);
        return FALSE;
    }
    __MALLOC(profile.user_id, char*, sizeof(char) * (strlen(ent_value) + 1));
    strcpy(profile.user_id, ent_value);

    __STRNCPY(ptr->sbar->text, "Success. Retrieving supported extensions...", __MAX_SBAR_TEXT_LEN_);
    gdk_threads_add_idle(set_label_text, ptr->sbar);

    __MALLOC(profile.supported_exts, char**, sizeof(char*) * (json_array_size(json_object_get(root, "supported")) + 1));

    for(i = 0; i < json_array_size(json_object_get(root, "supported")); i++) {
        if((ent_value = (char*)json_string_value(json_object_get(json_array_get(json_object_get(root, "supported"), i), "extension"))) == NULL) {
            __STRNCPY(ptr->sbar->text, "Can't get extention from JSON object!", __MAX_SBAR_TEXT_LEN_);
            gdk_threads_add_idle(set_label_text, ptr->sbar);
            gdk_threads_add_idle(set_button_state, ptr->btn);
            g_critical("Can't get extention from JSON object!");
            free(profile.token);
            free(profile.user_id);
            return FALSE;
        }

        __MALLOC(profile.supported_exts[i], char*, sizeof(char) * (strlen(ent_value) + 1));
        strcpy(profile.supported_exts[i], ent_value);
    }

    profile.supported_exts[json_array_size(json_object_get(root, "supported"))] = NULL;
    json_decref(root);

    /* Get list of MD5 hashes from iBroadcast.com */
    __STRNCPY(ptr->sbar->text, "Get file list from iBroadcast.com...", __MAX_SBAR_TEXT_LEN_);
    gdk_threads_add_idle(set_label_text, ptr->sbar);

    __MALLOC(data, char*, sizeof(char) * (strlen(profile.user_id) + strlen(profile.token) + strlen("user_id=") + strlen("token=") + 2));
    sprintf(data, "user_id=%s&token=%s",profile.user_id, profile.token);

    if((response = request(__UPLOAD_URL_, data, "application/x-www-form-urlencoded")) == NULL) {
        __STRNCPY(ptr->sbar->text, "Error when retrieve MD5 list from service!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        free(profile.token);
        free(profile.user_id);
        g_critical("Error when retrieve MD5 list from service!\n");
        return FALSE;
    }

    if((root = json_loads(response->memory, 0, &error)) == NULL) {
        __STRNCPY(ptr->sbar->text, "Reply parsing error!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        free(profile.token);
        free(profile.user_id);
        g_critical("error in reply: on line %d: %s", error.line, error.text);
        return FALSE;
    }

    free(data);
    free(response->memory);
    free(response);

    if(!json_is_object(root)) {
        __STRNCPY(ptr->sbar->text, "Malformed JSON object!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        free(profile.token);
        free(profile.user_id);
        g_critical("Malformed JSON object:\n%s", json_dumps(root, JSON_INDENT(4)));
        return FALSE;
    }

    if(json_boolean_value(json_object_get(root, "result")) == 0) {
        __STRNCPY(ptr->sbar->text, json_string_value(json_object_get(root, "message")), __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        free(profile.token);
        free(profile.user_id);
        g_critical("%s\n", json_string_value(json_object_get(root, "message")));
        return FALSE;
    }

    __MALLOC(profile.md5s, char**, sizeof(char*) * (json_array_size(json_object_get(root, "md5")) + 1));
    j = 0;

    for(i = 0; i < json_array_size(json_object_get(root, "md5")); i++) {
        /* In some cases server return JSON object with null values. Maybe it's error or server not yet
         * calculate MD5? */
        if(json_is_null(json_array_get(json_object_get(root, "md5"), i)))
            continue;
        if((ent_value = (char *)json_string_value(json_array_get(json_object_get(root, "md5"), i))) == NULL) {
            g_critical("Can't get md5 from JSON object!");
            g_critical("%s", json_dumps(root, JSON_INDENT(4)));
            continue;
        }
        __MALLOC(profile.md5s[j], char*, sizeof(char) * (strlen(ent_value) + 1));
        strcpy(profile.md5s[j], ent_value);
        j++;
    }
    profile.md5s[j] = NULL;
    json_decref(root);

    gdk_threads_add_idle(login_win_close, ptr);
    
    g_message(" - success");

    return NULL;
}


char *get_start_dir(void) {

    struct passwd *pw;
    char *homedir;

    if((pw = getpwuid(getuid())) == NULL) {
        g_critical("Error when get home directory. %s", strerror(errno));
        return NULL;
    }

    __MALLOC(homedir, char*, sizeof(char) * (strlen(pw->pw_dir) + strlen(__MUSIC_DIR) + 1));
    sprintf(homedir, "%s%s", pw->pw_dir, __MUSIC_DIR);

    if(access(homedir, F_OK) != 0)
        strcpy(homedir, pw->pw_dir);

    return homedir;
}

void *scan_dirs(void *arg) {

    g_message("Scanning directory: %s", profile.scan_dir);

    sf_thread_data_t *ptr = (sf_thread_data_t *)arg;

    ptr->btn->state = TRUE;

    __STRNCPY(ptr->sbar->text, "Walk directories...", __MAX_SBAR_TEXT_LEN_);

    gdk_threads_add_idle(set_label_text, ptr->sbar);

    files.count = 0;
    files.skipped = 0;
    files.uploaded = 0;
    files.remaining = 0;
    files.idx = 0;
    files.errored = 0;

    if(nftw(profile.scan_dir, nftw_cb, 20, 0) == -1) {
        __STRNCPY(ptr->sbar->text, "Error when directory traversal!", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(set_button_state, ptr->btn);
        g_critical("Error when directory traversal!\n");
        return NULL;
    }

    gdk_threads_add_idle(set_button_state, ptr->btn);

    if(files.count > 0) {
        __STRNCPY(ptr->sbar->text, " ", __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, ptr->sbar);
        gdk_threads_add_idle(file_list_show, NULL);
        return NULL;
    }

    if(files.skipped > 0)
        sprintf(ptr->sbar->text, "Done. Found: %zd files, %zd will be skipped (already uploaded).", files.count, files.skipped);
    else
        sprintf(ptr->sbar->text, "Done. No supported files found.");

    gdk_threads_add_idle(set_label_text, ptr->sbar);

    return NULL;
}

static int nftw_cb(const char *fpath, const struct stat *sb,
                   int tflag, struct FTW *ftwbuf) {

    char *ext = NULL, *md5 = NULL;
    gchar *filename;
    gsize writen;
    size_t i = 0, j = 0, k = 0;
    GError *error = NULL;

    filename = g_filename_to_utf8(fpath, -1, NULL, &writen, &error);

    if(error != NULL) {
        g_warning("Error when scan: %s", error->message);
        g_error_free(error);
    } else {
        __STRNCPY(fs->sbar->text, filename, __MAX_SBAR_TEXT_LEN_);
        gdk_threads_add_idle(set_label_text, fs->sbar);
    }

    if(tflag == FTW_F && access(fpath, R_OK) == 0) {

        for(i=0;profile.supported_exts[i] != NULL;i++) {

            ext = (char *)(fpath + (size_t)(strlen(fpath) - strlen(profile.supported_exts[i])));
            /* Skip, if file extension is not supported */
            if(strcasecmp(ext, profile.supported_exts[i]) != 0)
                continue;
            if((md5 = get_file_md5_hash(fpath)) == NULL)
                return 0;
            /* Skip file if it exists on server */
            for(k = 0; profile.md5s[k] != NULL; k++) {
                if(strcasecmp(md5, profile.md5s[k]) == 0) {
                    files.skipped++;
                    free(md5);
                    return 0;
                }
            }
            /* Skip, if file already exists in file list */
            for(j = 0; j < files.count; j++)
                if(strcasecmp(files.list[j]->md5, md5) == 0) {
                    free(md5);
                    return 0;
                }

            if((files.list = (f_info_t **)realloc(files.list, (files.count + 1) * sizeof(f_info_t *))) == NULL) {
                g_critical("Memory allocation error when extend filelist memory!");
                exit(EXIT_FAILURE);
            }
            __MALLOC(files.list[files.count], f_info_t*, sizeof(f_info_t));
            __MALLOC(files.list[files.count]->name, char*, sizeof(char) * (strlen(fpath) + 1));

            strcpy(files.list[files.count]->name, fpath);
            files.list[files.count]->md5 =  md5;

            files.count++;
            free(filename);
            return 0;
        }
    } else
        if(tflag == FTW_F) {
            __STRNCPY(fs->sbar->text, "Can't read file", __MAX_SBAR_TEXT_LEN_);
            gdk_threads_add_idle(set_label_text, fs->sbar);
        } else {
            __STRNCPY(fs->sbar->text, "Can't scan directory", __MAX_SBAR_TEXT_LEN_);
            gdk_threads_add_idle(set_label_text, fs->sbar);
        }

    free(filename);
    return 0;
}

static int progress_cb(void *arg, double dltotal, double dlnow,
                       double ultotal, double ulnow) {

    progress_t *p = (progress_t*)arg;
    double curtime = 0;

    curl_easy_getinfo(p->curl, CURLINFO_TOTAL_TIME, &curtime);

    if((curtime - p->lastruntime) >= MINIMAL_PROGRESS_FUNCTIONALITY_INTERVAL) {
        /* Will update UI only after MINIMAL_PROGRESS_FUNCTIONALITY_INTERVAL */
        if(ultotal > 0 && (p->progress->value = (gdouble)(ulnow/ultotal)) >= 0.01)
            gdk_threads_add_idle(set_progress_fraction, p->progress);
        p->lastruntime = curtime;
    }

    if(uploading_pause == TRUE && p->is_active == TRUE) {
        curl_easy_pause(p->curl, CURLPAUSE_ALL);
        p->is_active = FALSE;
    } else if(uploading_pause == FALSE && p->is_active == FALSE) {
        curl_easy_pause(p->curl, CURLPAUSE_CONT);
        p->is_active = TRUE;
    }

    return CURLE_OK;
}

void *upload_to_ibroadcast(void *arg) {

    g_message("Thread: begin_upload()");

    uploader_data_t *tc = (uploader_data_t *)arg;
    int code;
    size_t i;
    json_t *root;
    json_error_t j_error;
    CURL *curl = NULL;
    CURLcode status;
    progress_t p_data;
    GError *error = NULL;
    gsize writen;
    gchar *message;

    struct curl_httppost* post = NULL;
    struct curl_httppost* last = NULL;
    struct curl_slist *headers = NULL;

    mem_ch_t *chunk;

    chunk = malloc(sizeof(mem_ch_t));

    curl = curl_easy_init();

    if(!curl) {
        g_critical("libcurl easy_init failure!");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&rt_mtx);
    profile.running_threads++;
    pthread_mutex_unlock(&rt_mtx);

    /* Data for a progress indicator */
    p_data.progress = tc->progress;
    p_data.curl = curl;
    p_data.is_active = TRUE;

    curl_easy_setopt(curl, CURLOPT_URL, __UPLOAD_URL_);

    headers = curl_slist_append(headers, __UA_);
    headers = curl_slist_append(headers, __UPLOAD_TYPE_);

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wc_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);

    do {
        
        g_message("upload(%i, %s)", (int)files.idx, files.list[files.idx]->name);
        
        chunk->memory = malloc(1);
        chunk->size = 0;

        // Get next file
        pthread_mutex_lock(&files_mtx);

        i = files.idx;
        ++files.idx;

        // Skip over nulls

        if(files.list[i] == NULL) {
            g_message(" - file is null, skipping");
            continue;
        }
        
        
        __MALLOC(tc->name_label->text, char*, sizeof(char) *(strlen(files.list[i]->name) + 1));
        strcpy(tc->name_label->text, files.list[i]->name);
        free(files.list[i]->name);
        free(files.list[i]->md5);
        free(files.list[i]);
        files.list[i] = NULL;

        pthread_mutex_unlock(&files_mtx);

        if(tc->name_label->text == NULL) {
            g_message(" - file path is null, skipping");
            continue;
        }

        gdk_threads_add_idle(set_label_text, tc->name_label);

        tc->progress->value = 0.0;
        p_data.lastruntime = 0;

        gdk_threads_add_idle(set_progress_fraction, tc->progress);

        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_cb);
        curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, (void *)&p_data);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "file",
                     CURLFORM_FILE, tc->name_label->text, CURLFORM_END);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "file_path",
                     CURLFORM_COPYCONTENTS, tc->name_label->text, CURLFORM_END);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "method",
                     CURLFORM_COPYCONTENTS, __METHOD_, CURLFORM_END);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "user_id",
                     CURLFORM_COPYCONTENTS, profile.user_id, CURLFORM_END);

        curl_formadd(&post, &last, CURLFORM_COPYNAME, "token",
                     CURLFORM_COPYCONTENTS, profile.token, CURLFORM_END);

        curl_easy_setopt(curl, CURLOPT_HTTPPOST, post);

        status = curl_easy_perform(curl);
        
        
        if(status == 0) {

            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);

            if((root = json_loads(chunk->memory, 0, &j_error)) == NULL) {
                g_critical("Error parsing JSON: %s", j_error.text);
                
                __STRNCPY(tc->sbar->text, "Error in service reply!", __MAX_SBAR_TEXT_LEN_);
                gdk_threads_add_idle(set_label_text, tc->sbar);

                pthread_mutex_lock(&rt_mtx);
                profile.running_threads--;
                if(profile.running_threads == 0)
                    gdk_threads_add_idle(finished_screen_show, NULL);
                pthread_mutex_unlock(&rt_mtx);

                return NULL;
            }

            if(!json_is_object(root)) {
                g_critical("Malformed JSON object:\n%s\n", json_dumps(root, JSON_INDENT(4)));
                __STRNCPY(tc->sbar->text, "Malformed JSON object!", __MAX_SBAR_TEXT_LEN_);
                gdk_threads_add_idle(set_label_text, tc->sbar);

                pthread_mutex_lock(&rt_mtx);
                profile.running_threads--;
                if(profile.running_threads == 0)
                    gdk_threads_add_idle(finished_screen_show, NULL);
                pthread_mutex_unlock(&rt_mtx);

                return NULL;
            }

            message = g_filename_to_utf8(json_string_value(json_object_get(root, "message")), -1, NULL, &writen, &error);
            
            g_message(" - JSON response message: %s", message);

            if(error != NULL) {
                g_warning("%s", error->message);
                g_error_free(error);
                error = NULL;
            } else if(message != NULL) {
                pthread_mutex_lock(&up_sbar_mtx);
                __STRNCPY(tc->sbar->text, message, __MAX_SBAR_TEXT_LEN_);
                gdk_threads_add_idle(set_label_text, tc->sbar);
                pthread_mutex_unlock(&up_sbar_mtx);
                free(message);
            }

            pthread_mutex_lock(&uploaded_mtx);
            sprintf(tc->count_l->text, __COUNT_LABEL_TEMPLATE_, ++files.uploaded, --files.remaining, files.skipped, files.errored);
            gdk_threads_add_idle(set_label_text, tc->count_l);
            pthread_mutex_unlock(&uploaded_mtx);

            json_decref(root);
        } else {
            pthread_mutex_lock(&up_sbar_mtx);
            __STRNCPY(tc->sbar->text, curl_easy_strerror(status), __MAX_SBAR_TEXT_LEN_);
            gdk_threads_add_idle(set_label_text, tc->sbar);
            pthread_mutex_unlock(&up_sbar_mtx);
            g_critical("%s", curl_easy_strerror(status));
            
            pthread_mutex_lock(&uploaded_mtx);
            ++files.errored;
            pthread_mutex_unlock(&uploaded_mtx);
        }

        curl_formfree(post);

        free(chunk->memory);
        free(tc->name_label->text);
        tc->name_label->text = NULL;

        post = NULL;
        last = NULL;

    } while(files.idx < files.count);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    free(chunk);
    free(tc->progress);
    free(tc);

    pthread_mutex_lock(&rt_mtx);
    profile.running_threads--;
    if(profile.running_threads == 0)
        gdk_threads_add_idle(finished_screen_show, NULL);
    pthread_mutex_unlock(&rt_mtx);

    return NULL;
}

/* tools.c ends here */
