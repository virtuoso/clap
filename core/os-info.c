// SPDX-License-Identifier: Apache-2.0
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "clap.h"
#include "util.h"

const char *clap_semver_parse(const char *str, clap_semver *v)
{
    unsigned int *comp[] = { &v->major, &v->minor, &v->patch };

    for (int i = 0; i < 3 && isdigit(*str); i++) {
        *comp[i] = strtoul(str, (char **)&str, 10);
        if (*str == '.' || *str == '_')  str++;
    }

    return str;
}

void clap_os_parse_user_agent(clap_os_info *osi, const char *ua)
{
    /* in UA token order: "iPhone OS x like Mac OS X", "Linux; Android x" */
    static const struct { const char *token; clap_os_type os; } os_tokens[] = {
        { "Windows NT ",    CLAP_OS_WINDOWS },
        { "Android ",       CLAP_OS_ANDROID },
        { "iPhone OS ",     CLAP_OS_IOS },
        { "CPU OS ",        CLAP_OS_IOS },      /* iPad */
        { "Mac OS X",       CLAP_OS_MACOS },
        { "Linux",          CLAP_OS_LINUX },
    };
    const char *s;

    if (!ua)    return;

    for (int i = 0; i < array_size(os_tokens); i++) {
        s = strstr(ua, os_tokens[i].token);
        if (!s) continue;

        osi->os = os_tokens[i].os;
        s += strlen(os_tokens[i].token);
        while (*s == ' ')   s++;
        clap_semver_parse(s, &osi->os_version);
        break;
    }

    if ((s = strstr(ua, "Firefox/"))) {
        osi->browser = CLAP_BROWSER_FIREFOX;
        clap_semver_parse(s + strlen("Firefox/"), &osi->browser_version);
    } else if (strstr(ua, "Edg/") || strstr(ua, "OPR/")) {
        osi->browser = CLAP_BROWSER_OTHER;
    } else if ((s = strstr(ua, "Chrome/"))) {
        osi->browser = CLAP_BROWSER_CHROME;
        clap_semver_parse(s + strlen("Chrome/"), &osi->browser_version);
    } else if ((s = strstr(ua, "CriOS/"))) {
        osi->browser = CLAP_BROWSER_CHROME;
        clap_semver_parse(s + strlen("CriOS/"), &osi->browser_version);
    } else if ((s = strstr(ua, "Version/")) && strstr(ua, "Safari/")) {
        osi->browser = CLAP_BROWSER_SAFARI;
        clap_semver_parse(s + strlen("Version/"), &osi->browser_version);
    } else if (ua[0]) {
        osi->browser = CLAP_BROWSER_OTHER;
    }

    if ((s = strstr(ua, "AppleWebKit/")))
        clap_semver_parse(s + strlen("AppleWebKit/"), &osi->webkit_version);

    if (strstr(ua, "iPhone") || strstr(ua, "iPad") || strstr(ua, "Android"))
        osi->mobile = true;
}
