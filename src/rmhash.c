/**
*  This file is part of rmlint.
*
*  rmlint is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  rmlint is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with rmlint.  If not, see <http://www.gnu.org/licenses/>.
*
** Authors:
 *
 *  - Christopher <sahib> Pahl 2010-2014 (https://github.com/sahib)
 *  - Daniel <SeeSpotRun> T.   2014-2014 (https://github.com/SeeSpotRun)
 *
** Hosted on http://github.com/sahib/rmlint
*
**/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../lib/hasher.h"
#include "../lib/utilities.h"

typedef struct RmHasherTestMainSession {
    gboolean print_in_order;
    GMutex lock;
    RmDigestType digest_type;
    char **paths;
    gint path_index;
    RmDigest **completed_digests_buffer;
    gint verbosity;
} RmHasherTestMainSession;

static void logging_callback(_U const gchar *log_domain,
                             GLogLevelFlags log_level,
                             const gchar *message,
                             gpointer user_data) {
    RmHasherTestMainSession *session = user_data;
    if(session->verbosity >= log_level) {
        fputs(message, stderr);
    }
}

static gboolean rm_hasher_parse_type(_U const char *option_name,
                                     const gchar *value,
                                     RmHasherTestMainSession *session,
                                     GError **error) {
    session->digest_type = rm_string_to_digest_type(value);

    if(session->digest_type == RM_DIGEST_UNKNOWN) {
        g_set_error(error, RM_ERROR_QUARK, 0, _("Unknown hash algorithm: '%s'"), value);
        return FALSE;
    }
    return TRUE;
}

static void rm_hasher_print(RmDigest *digest, char *path) {
    gsize size = rm_digest_get_bytes(digest) * 2 + 1;

    char checksum_str[size];
    memset(checksum_str, '0', size);
    checksum_str[size - 1] = 0;

    rm_digest_hexstring(digest, checksum_str);
    g_print("%s  %s\n", checksum_str, path);
}

static int rm_hasher_callback(_U RmHasher *hasher,
                              RmDigest *digest,
                              RmHasherTestMainSession *session,
                              gpointer index_ptr) {
    gint index = GPOINTER_TO_INT(index_ptr);
    g_mutex_lock(&session->lock);
    {
        if(session->print_in_order) {
            /* add digest in buffer array */
            session->completed_digests_buffer[index] = digest;
            /* check if the next due digest has been completed; if yes then print
             * it (and possibly any following digests) */
            while(session->completed_digests_buffer[session->path_index]) {
                if(session->paths[session->path_index]) {
                    rm_hasher_print(
                        session->completed_digests_buffer[session->path_index],
                        session->paths[session->path_index]);
                    rm_digest_free(
                        session->completed_digests_buffer[session->path_index]);
                }
                session->completed_digests_buffer[session->path_index] = NULL;
                session->path_index++;
            }
        } else {
            rm_hasher_print(digest, session->paths[index]);
        }
    }
    g_mutex_unlock(&session->lock);
    return 0;
}

int main(int argc, char **argv) {
    RmHasherTestMainSession tag;
    g_log_set_default_handler(logging_callback, &tag);

    tag.verbosity = G_LOG_LEVEL_WARNING;

    /* List of paths we got passed (or NULL)   */
    tag.paths = NULL;

    /* Print hashes in the same order as files in command line args */
    tag.print_in_order = TRUE;

    /* Digest type (user option, default SHA1) */
    tag.digest_type = RM_DIGEST_SHA1;
    gint threads = 8;
    gint64 buffer_mbytes = 256;

    ////////////// Option Parsing ///////////////

    const GOptionEntry entries[] = {
        {"digest-type", 'd', 0, G_OPTION_ARG_CALLBACK,
         (GOptionArgFunc)rm_hasher_parse_type, "Digest type [SHA1]", "[TYPE]"},
        {"num-threads", 't', 0, G_OPTION_ARG_INT, &threads,
         _("Number of hashing threads [8]"), NULL},
        {"buffer-mbytes", 'b', 0, G_OPTION_ARG_INT64, &buffer_mbytes,
         _("Megabytes read buffer (default 256 MB)"), NULL},
        {"ignore-order", 'i', G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE,
         &tag.print_in_order, _("Print hashes in order completed, not in order entered "
                                "(reduces memory usage)"),
         NULL},
        {"", 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &tag.paths,
         "Space-separated list of files", "[FILE...]"},
        {NULL}};

    GError *error = NULL;
    GOptionContext *context = g_option_context_new("      Hash a list of files");
    GOptionGroup *main_group =
        g_option_group_new("rmhash", "Hash a list of files", "", &tag, NULL);

    g_option_group_add_entries(main_group, entries);
    g_option_context_set_main_group(context, main_group);
    g_option_context_set_summary(context,
                                 "Multi-threaded file digest (hash) calculator."
                                 "\n  Available digest types:"
                                 "\n    spooky32, spooky64, md5, murmur[128], spooky[128], "
                                 "city[128], sha1, sha256, sha512"
                                 "\n    Also: murmur256, city256, bastard, city512, "
                                 "murmur512, ext, cumulative, paranoid");

    int argc_initial = argc;

    if(!g_option_context_parse(context, &argc, &argv, &error)) {
        /* print g_option error message, followed by help */
        g_printerr("Error: %s\n---------------\n", error->message);
        g_printerr("%s", g_option_context_get_help(context, FALSE, NULL));
        exit(1);
    } else if(argc_initial == 1) {
        /* read paths from stdin */
        char path_buf[PATH_MAX];
        GPtrArray *paths = g_ptr_array_new();

        while(fgets(path_buf, PATH_MAX, stdin)) {
            char *abs_path = realpath(strtok(path_buf, "\n"), NULL);
            g_ptr_array_add(paths, abs_path);
        }

        tag.paths = (char **)g_ptr_array_free(paths, FALSE);
    } 
    
    if(tag.paths == NULL || tag.paths[0] == NULL) {
        g_printerr("Error: no file names provided %p\n", tag.paths);
        exit(1);
    } 

    g_option_context_free(context);

    ////////// Implementation //////

    if(tag.print_in_order) {
        /* allocate buffer to collect results */
        tag.completed_digests_buffer =
            g_slice_alloc0((g_strv_length(tag.paths) + 1) * sizeof(RmDigest *));
        tag.path_index = 0;
    }

    /* initialise structures */
    g_mutex_init(&tag.lock);
    RmHasher *hasher = rm_hasher_new(tag.digest_type,
                                     threads,
                                     FALSE,
                                     4096,
                                     1024 * 1024 * buffer_mbytes,
                                     0,
                                     (RmHasherCallback)rm_hasher_callback,
                                     &tag);

    /* Iterate over paths, pushing to hasher threads */
    for(int i = 0; tag.paths && tag.paths[i]; ++i) {
        /* check it is a regular file */

        RmStat stat_buf;
        if(rm_sys_stat(tag.paths[i], &stat_buf) == -1) {
            rm_log_warning("Cannot stat %s\n", tag.paths[i]);
        } else if(S_ISDIR(stat_buf.st_mode)) {
            rm_log_info("rmhash: %s: Is a directory\n", tag.paths[i]);
        } else if(S_ISREG(stat_buf.st_mode)) {
            RmHasherTask *task = rm_hasher_task_new(hasher, NULL, GINT_TO_POINTER(i));
            rm_hasher_task_hash(task, tag.paths[i], 0, 0, FALSE);
            rm_hasher_task_finish(task);
            continue;
        } else {
            rm_log_warning("warning: %s: Unknown type\n", tag.paths[i]);
        }

        /* dummy callback for failed paths */
        g_free(tag.paths[i]);
        tag.paths[i] = NULL;
        rm_hasher_callback(hasher, GINT_TO_POINTER(1), &tag, GINT_TO_POINTER(i));
    }

    /* wait for all hasher threads to finish... */
    rm_hasher_free(hasher, TRUE);

    /* tidy up */
    if(tag.print_in_order) {
        g_slice_free1((g_strv_length(tag.paths) + 1) * sizeof(RmDigest *),
                      tag.completed_digests_buffer);
    }

    g_strfreev(tag.paths);
    return 0;
}
