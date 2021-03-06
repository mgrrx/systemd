/* SPDX-License-Identifier: LGPL-2.1+ */

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include "alloc-util.h"
#include "crypt-util.h"
#include "hexdecoct.h"
#include "log.h"
#include "string-util.h"
#include "terminal-util.h"

static char *arg_root_hash = NULL;
static char *arg_data_what = NULL;
static char *arg_hash_what = NULL;

static int help(void) {
        _cleanup_free_ char *link = NULL;
        int r;

        r = terminal_urlify_man("systemd-veritysetup@.service", "8", &link);
        if (r < 0)
                return log_oom();

        printf("%s attach VOLUME DATADEVICE HASHDEVICE ROOTHASH\n"
               "%s detach VOLUME\n\n"
               "Attaches or detaches an integrity protected block device.\n"
               "\nSee the %s for details.\n"
               , program_invocation_short_name
               , program_invocation_short_name
               , link
        );

        return 0;
}

int main(int argc, char *argv[]) {
        _cleanup_(crypt_freep) struct crypt_device *cd = NULL;
        int r;

        if (argc <= 1) {
                r = help();
                goto finish;
        }

        if (argc < 3) {
                log_error("This program requires at least two arguments.");
                r = -EINVAL;
                goto finish;
        }

        log_set_target(LOG_TARGET_AUTO);
        log_parse_environment();
        log_open();

        umask(0022);

        if (streq(argv[1], "attach")) {
                _cleanup_free_ void *m = NULL;
                crypt_status_info status;
                size_t l;

                if (argc < 6) {
                        log_error("attach requires at least two arguments.");
                        r = -EINVAL;
                        goto finish;
                }

                r = unhexmem(argv[5], strlen(argv[5]), &m, &l);
                if (r < 0) {
                        log_error("Failed to parse root hash.");
                        goto finish;
                }

                r = crypt_init(&cd, argv[4]);
                if (r < 0) {
                        log_error_errno(r, "Failed to open verity device %s: %m", argv[4]);
                        goto finish;
                }

                crypt_set_log_callback(cd, cryptsetup_log_glue, NULL);

                status = crypt_status(cd, argv[2]);
                if (IN_SET(status, CRYPT_ACTIVE, CRYPT_BUSY)) {
                        log_info("Volume %s already active.", argv[2]);
                        r = 0;
                        goto finish;
                }

                r = crypt_load(cd, CRYPT_VERITY, NULL);
                if (r < 0) {
                        log_error_errno(r, "Failed to load verity superblock: %m");
                        goto finish;
                }

                r = crypt_set_data_device(cd, argv[3]);
                if (r < 0) {
                        log_error_errno(r, "Failed to configure data device: %m");
                        goto finish;
                }

                r = crypt_activate_by_volume_key(cd, argv[2], m, l, CRYPT_ACTIVATE_READONLY);
                if (r < 0) {
                        log_error_errno(r, "Failed to set up verity device: %m");
                        goto finish;
                }

        } else if (streq(argv[1], "detach")) {

                r = crypt_init_by_name(&cd, argv[2]);
                if (r == -ENODEV) {
                        log_info("Volume %s already inactive.", argv[2]);
                        goto finish;
                } else if (r < 0) {
                        log_error_errno(r, "crypt_init_by_name() failed: %m");
                        goto finish;
                }

                crypt_set_log_callback(cd, cryptsetup_log_glue, NULL);

                r = crypt_deactivate(cd, argv[2]);
                if (r < 0) {
                        log_error_errno(r, "Failed to deactivate: %m");
                        goto finish;
                }

        } else {
                log_error("Unknown verb %s.", argv[1]);
                r = -EINVAL;
                goto finish;
        }

        r = 0;

finish:
        free(arg_root_hash);
        free(arg_data_what);
        free(arg_hash_what);

        return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
