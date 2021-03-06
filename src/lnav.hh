/**
 * Copyright (c) 2007-2012, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file lnav.hh
 */

#ifndef lnav_hh
#define lnav_hh

#include "config.h"

#include <signal.h>
#include <sys/time.h>

#include <map>
#include <set>
#include <list>
#include <stack>
#include <memory>

#include "base/future_util.hh"
#include "safe/safe.h"
#include "logfile.hh"
#include "hist_source.hh"
#include "statusview_curses.hh"
#include "listview_curses.hh"
#include "top_status_source.hh"
#include "bottom_status_source.hh"
#include "doc_status_source.hh"
#include "grep_highlighter.hh"
#include "db_sub_source.hh"
#include "textfile_sub_source.hh"
#include "log_vtab_impl.hh"
#include "readline_curses.hh"
#include "xterm_mouse.hh"
#include "piper_proc.hh"
#include "term_extra.hh"
#include "curl_looper.hh"
#include "relative_time.hh"
#include "log_format_loader.hh"
#include "spectro_source.hh"
#include "command_executor.hh"
#include "plain_text_source.hh"
#include "input_dispatcher.hh"
#include "filter_sub_source.hh"
#include "files_sub_source.hh"
#include "filter_status_source.hh"
#include "preview_status_source.hh"
#include "sql_util.hh"
#include "archive_manager.hh"

/** The command modes that are available while viewing a file. */
typedef enum {
    LNM_PAGING,
    LNM_FILTER,
    LNM_FILES,
    LNM_COMMAND,
    LNM_SEARCH,
    LNM_SEARCH_FILTERS,
    LNM_SEARCH_FILES,
    LNM_CAPTURE,
    LNM_SQL,
    LNM_EXEC,
    LNM_USER,
} ln_mode_t;

enum {
    LNB_SYSLOG,
    LNB__MAX,

    LNB_TIMESTAMP,
    LNB_HELP,
    LNB_HEADLESS,
    LNB_QUIET,
    LNB_ROTATED,
    LNB_RECURSIVE,
    LNB_CHECK_CONFIG,
    LNB_INSTALL,
    LNB_UPDATE_FORMATS,
    LNB_VERBOSE,
    LNB_SECURE_MODE,
};

/** Flags set on the lnav command-line. */
typedef enum {
    LNF_SYSLOG    = (1L << LNB_SYSLOG),

    LNF_ROTATED   = (1L << LNB_ROTATED),
    LNF_RECURSIVE = (1L << LNB_RECURSIVE),

    LNF_TIMESTAMP = (1L << LNB_TIMESTAMP),
    LNF_HELP      = (1L << LNB_HELP),
    LNF_HEADLESS  = (1L << LNB_HEADLESS),
    LNF_QUIET     = (1L << LNB_QUIET),
    LNF_CHECK_CONFIG = (1L << LNB_CHECK_CONFIG),
    LNF_INSTALL   = (1L << LNB_INSTALL),
    LNF_UPDATE_FORMATS = (1L << LNB_UPDATE_FORMATS),
    LNF_VERBOSE = (1L << LNB_VERBOSE),
    LNF_SECURE_MODE = (1L << LNB_SECURE_MODE),

    LNF__ALL      = (LNF_SYSLOG|LNF_HELP),
} lnav_flags_t;

/** The different views available. */
typedef enum {
    LNV_LOG,
    LNV_TEXT,
    LNV_HELP,
    LNV_HISTOGRAM,
    LNV_DB,
    LNV_SCHEMA,
    LNV_PRETTY,
    LNV_SPECTRO,

    LNV__MAX
} lnav_view_t;

extern const char *lnav_view_strings[LNV__MAX + 1];

extern const char *lnav_zoom_strings[];

/** The status bars. */
typedef enum {
    LNS_TOP,
    LNS_BOTTOM,
    LNS_FILTER,
    LNS_FILTER_HELP,
    LNS_DOC,
    LNS_PREVIEW,

    LNS__MAX
} lnav_status_t;

typedef std::pair<int, int>                      ppid_time_pair_t;
typedef std::pair<ppid_time_pair_t, std::string> session_pair_t;

class input_state_tracker : public log_state_dumper {
public:
    input_state_tracker() : ist_index(0) {
        memset(this->ist_recent_key_presses, 0, sizeof(this->ist_recent_key_presses));
    };

    void log_state() {
        log_info("recent_key_presses: index=%d", this->ist_index);
        for (int lpc = 0; lpc < COUNT; lpc++) {
            log_msg_extra(" 0x%x (%c)", this->ist_recent_key_presses[lpc],
                    this->ist_recent_key_presses[lpc]);
        }
        log_msg_extra_complete();
    };

    void push_back(int ch) {
        this->ist_recent_key_presses[this->ist_index % COUNT] = ch;
        this->ist_index = (this->ist_index + 1) % COUNT;
    };

private:
    static const int COUNT = 10;

    int ist_recent_key_presses[COUNT];
    size_t ist_index;
};

struct key_repeat_history {
    int krh_key{0};
    int krh_count{0};
    vis_line_t krh_start_line{0_vl};
    struct timeval krh_last_press_time{0, 0};

    void update(int ch, vis_line_t top) {
        struct timeval now, diff;

        gettimeofday(&now, NULL);
        timersub(&now, &this->krh_last_press_time, &diff);
        if (diff.tv_sec >= 1 || diff.tv_usec > (750 * 1000)) {
            this->krh_key = 0;
            this->krh_count = 0;
        }
        this->krh_last_press_time = now;

        if (this->krh_key == ch) {
            this->krh_count += 1;
        } else {
            this->krh_key = ch;
            this->krh_count = 1;
            this->krh_start_line = top;
        }
    };
};

struct scan_progress {
    std::list<archive_manager::extract_progress> sp_extractions;
};

using safe_scan_progress = safe::Safe<scan_progress>;

struct file_collection {
    std::map<std::string, std::string> fc_name_to_errors;
    std::map<std::string, logfile_open_options> fc_file_names;
    std::vector<std::shared_ptr<logfile>> fc_files;
    int fc_files_generation{0};
    std::vector<std::pair<std::shared_ptr<logfile>, std::string>>
        fc_renamed_files;
    std::set<std::string> fc_closed_files;
    std::map<std::string, std::string> fc_other_files;
    std::shared_ptr<safe_scan_progress> fc_progress;
    size_t fc_largest_path_length{0};

    file_collection()
        : fc_progress(std::make_shared<safe::Safe<scan_progress>>()) {}

    void clear() {
        this->fc_name_to_errors.clear();
        this->fc_file_names.clear();
        this->fc_files.clear();
        this->fc_closed_files.clear();
        this->fc_other_files.clear();
    }
    file_collection rescan_files(bool required = false);
    void expand_filename(future_queue<file_collection> &fq, const std::string& path, logfile_open_options &loo, bool required);
    std::future<file_collection>
    watch_logfile(const std::string& filename, logfile_open_options &loo, bool required);
    void merge(const file_collection &other);
    void close_file(const std::shared_ptr<logfile> &lf);
    void regenerate_unique_file_names();
};

struct _lnav_data {
    std::map<std::string, std::list<session_pair_t>> ld_session_id;
    time_t                                  ld_session_time;
    time_t                                  ld_session_load_time;
    const char *                            ld_program_name;
    const char *                            ld_debug_log_name;

    std::list<std::string>                  ld_commands;
    bool                                    ld_cmd_init_done;
    bool                                    ld_session_loaded;
    std::vector<ghc::filesystem::path>      ld_config_paths;
    file_collection                         ld_active_files;
    std::list<std::pair<std::string, int> > ld_files_to_front;
    std::string                             ld_pt_search;
    time_t                                  ld_pt_min_time;
    time_t                                  ld_pt_max_time;
    bool                                    ld_stdout_used;
    sig_atomic_t                            ld_looping;
    sig_atomic_t                            ld_winched;
    sig_atomic_t                            ld_child_terminated;
    unsigned long                           ld_flags;
    WINDOW *                                ld_window;
    ln_mode_t                               ld_mode;
    ln_mode_t                               ld_last_config_mode{LNM_FILTER};

    statusview_curses                       ld_status[LNS__MAX];
    top_status_source                       ld_top_source;
    bottom_status_source                    ld_bottom_source;
    filter_status_source                    ld_filter_status_source;
    filter_help_status_source               ld_filter_help_status_source;
    doc_status_source                       ld_doc_status_source;
    preview_status_source                   ld_preview_status_source;
    bool                                    ld_preview_hidden;
    listview_curses::action::broadcaster    ld_scroll_broadcaster;
    listview_curses::action::broadcaster    ld_view_stack_broadcaster;

    plain_text_source                       ld_help_source;

    plain_text_source                       ld_doc_source;
    textview_curses                         ld_doc_view;
    filter_sub_source                       ld_filter_source;
    textview_curses                         ld_filter_view;
    files_sub_source                        ld_files_source;
    files_overlay_source                    ld_files_overlay;
    textview_curses                         ld_files_view;
    plain_text_source                       ld_example_source;
    textview_curses                         ld_example_view;
    plain_text_source                       ld_match_source;
    textview_curses                         ld_match_view;
    plain_text_source                       ld_preview_source;
    textview_curses                         ld_preview_view;

    view_stack<textview_curses>             ld_view_stack;
    textview_curses *ld_last_view;
    textview_curses                         ld_views[LNV__MAX];
    std::shared_ptr<grep_proc<vis_line_t>> ld_meta_search;
    vis_line_t                              ld_search_start_line;
    readline_curses *                       ld_rl_view;

    logfile_sub_source                      ld_log_source;
    hist_source2                            ld_hist_source2;
    int                                     ld_zoom_level;
    spectrogram_source ld_spectro_source;

    textfile_sub_source                     ld_text_source;

    std::map<textview_curses *, int>        ld_last_user_mark;
    std::map<textview_curses *, int>        ld_select_start;

    db_label_source                         ld_db_row_source;
    db_overlay_source                       ld_db_overlay;
    std::vector<std::string>                ld_db_key_names;

    std::string                             ld_previous_search;

    vis_line_t                              ld_last_pretty_print_top;

    log_vtab_manager *                      ld_vtab_manager;
    auto_mem<sqlite3, sqlite_close_wrapper> ld_db;

    std::unordered_map<std::string, std::string> ld_table_ddl;

    std::list<pid_t>                        ld_children;
    std::list<std::shared_ptr<piper_proc>>  ld_pipers;
    xterm_mouse ld_mouse;
    term_extra ld_term_extra;

    input_state_tracker ld_input_state;
    input_dispatcher ld_input_dispatcher;

    curl_looper ld_curl_looper;

    relative_time ld_last_relative_time;

    std::map<std::string, std::vector<script_metadata> > ld_scripts;

    exec_context ld_exec_context;

    int ld_fifo_counter;

    struct key_repeat_history ld_key_repeat_history;
};

extern struct _lnav_data lnav_data;

extern readline_context::command_map_t lnav_commands;
extern const int ZOOM_LEVELS[];
extern const ssize_t ZOOM_COUNT;

#define HELP_MSG_1(x, msg) \
    "Press '" ANSI_BOLD(#x) "' " msg

#define HELP_MSG_2(x, y, msg) \
    "Press " ANSI_BOLD(#x) "/" ANSI_BOLD(#y) " " msg

void rebuild_hist();
void rebuild_indexes();
void execute_examples();
attr_line_t eval_example(const help_text &ht, const help_example &ex);

bool ensure_view(textview_curses *expected_tc);
bool toggle_view(textview_curses *toggle_tc);
void layout_views();

bool setup_logline_table(exec_context &ec);

bool rescan_files(bool required = false);
bool update_active_files(const file_collection& new_files);

void wait_for_children();

vis_line_t next_cluster(
        vis_line_t(bookmark_vector<vis_line_t>::*f) (vis_line_t) const,
        bookmark_type_t *bt,
        vis_line_t top);
bool moveto_cluster(vis_line_t(bookmark_vector<vis_line_t>::*f) (vis_line_t) const,
                    bookmark_type_t *bt,
                    vis_line_t top);
void previous_cluster(bookmark_type_t *bt, textview_curses *tc);
vis_line_t search_forward_from(textview_curses *tc);
textview_curses *get_textview_for_mode(ln_mode_t mode);

#endif
