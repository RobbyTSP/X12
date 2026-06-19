#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "xf86.h"
#include "xf86Opt.h"
#include "x12conf.h"

static char *
trim(char *str)
{
    char *end;

    while (isspace((unsigned char)*str))
        str++;

    if (*str == 0)
        return str;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end))
        end--;

    end[1] = '\0';
    return str;
}

static const char *
resolve_alias(const char *key)
{
    if (xf86NameCmp(key, "VSync") == 0)
        return "PageFlip";
    if (xf86NameCmp(key, "GSync") == 0 || xf86NameCmp(key, "FreeSync") == 0 || xf86NameCmp(key, "VRR") == 0)
        return "VariableRefresh";
    return key;
}

static void
parse_file(int scrnIndex, const char *path, OptionInfoPtr options)
{
    FILE *fp = fopen(path, "r");
    char line[512];

    if (!fp)
        return;

    xf86DrvMsg(scrnIndex, X_INFO, "[X12] Loading configuration from %s\n", path);

    while (fgets(line, sizeof(line), fp)) {
        char *p = trim(line);
        char *eq, *key, *val;
        const char *mapped_key;
        OptionInfoPtr opt;
        Bool is_true = FALSE;
        Bool is_bool = FALSE;

        // Skip comments and empty lines
        if (p[0] == '\0' || p[0] == '#' || p[0] == ';')
            continue;

        eq = strchr(p, '=');
        if (!eq)
            continue;

        *eq = '\0';
        key = trim(p);
        val = trim(eq + 1);

        if (key[0] == '\0' || val[0] == '\0')
            continue;

        mapped_key = resolve_alias(key);

        // Find the option in the modesetting options table
        opt = options;
        while (opt && opt->name) {
            if (xf86NameCmp(opt->name, mapped_key) == 0) {
                break;
            }
            opt++;
        }

        if (!opt || !opt->name) {
            xf86DrvMsg(scrnIndex, X_WARNING, "[X12] Unknown config option: %s (mapped to %s)\n", key, mapped_key);
            continue;
        }

        switch (opt->type) {
            case OPTV_BOOLEAN:
                is_bool = TRUE;
                if (xf86NameCmp(val, "true") == 0 || xf86NameCmp(val, "yes") == 0 ||
                    xf86NameCmp(val, "on") == 0 || strcmp(val, "1") == 0) {
                    is_true = TRUE;
                } else if (xf86NameCmp(val, "false") == 0 || xf86NameCmp(val, "no") == 0 ||
                           xf86NameCmp(val, "off") == 0 || strcmp(val, "0") == 0) {
                    is_true = FALSE;
                } else {
                    xf86DrvMsg(scrnIndex, X_WARNING, "[X12] Invalid boolean value '%s' for option %s\n", val, key);
                    is_bool = FALSE;
                }

                if (is_bool) {
                    opt->value.boolean = is_true;
                    opt->found = TRUE;
                    xf86DrvMsg(scrnIndex, X_CONFIG, "[X12] Option %s set to %s\n", opt->name, is_true ? "True" : "False");
                }
                break;

            case OPTV_STRING:
            case OPTV_ANYSTR:
                opt->value.str = strdup(val);
                opt->found = TRUE;
                xf86DrvMsg(scrnIndex, X_CONFIG, "[X12] Option %s set to \"%s\"\n", opt->name, val);
                break;

            case OPTV_INTEGER:
                opt->value.num = atoi(val);
                opt->found = TRUE;
                xf86DrvMsg(scrnIndex, X_CONFIG, "[X12] Option %s set to %ld\n", opt->name, opt->value.num);
                break;

            case OPTV_REAL:
                opt->value.realnum = atof(val);
                opt->found = TRUE;
                xf86DrvMsg(scrnIndex, X_CONFIG, "[X12] Option %s set to %f\n", opt->name, opt->value.realnum);
                break;

            default:
                xf86DrvMsg(scrnIndex, X_WARNING, "[X12] Option %s has unsupported type %d\n", opt->name, opt->type);
                break;
        }
    }

    fclose(fp);
}

void
ms_parse_x12_config(int scrnIndex, void *options)
{
    char user_conf_path[512];
    const char *env_path = getenv("X12_CONFIG_PATH");
    const char *home = getenv("HOME");
    Bool user_conf_found = FALSE;

    // 0. Try X12_CONFIG_PATH (primarily for testing inside sandbox)
    if (env_path) {
        FILE *fp = fopen(env_path, "r");
        if (fp) {
            fclose(fp);
            parse_file(scrnIndex, env_path, (OptionInfoPtr)options);
            return;
        }
    }

    // 1. Try ~/.config/x12.conf
    if (home) {
        snprintf(user_conf_path, sizeof(user_conf_path), "%s/.config/x12.conf", home);
        FILE *fp = fopen(user_conf_path, "r");
        if (fp) {
            fclose(fp);
            parse_file(scrnIndex, user_conf_path, (OptionInfoPtr)options);
            user_conf_found = TRUE;
        }
    }

    // 2. Try /etc/x12.conf if user config not loaded
    if (!user_conf_found) {
        FILE *fp = fopen("/etc/x12.conf", "r");
        if (fp) {
            fclose(fp);
            parse_file(scrnIndex, "/etc/x12.conf", (OptionInfoPtr)options);
        }
    }
}
