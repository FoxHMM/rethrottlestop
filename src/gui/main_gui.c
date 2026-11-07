

#include "gui.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
#endif
int msr_daemon_run(int intervalMs);

int main(int argc, char** argv) {

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--daemon") == 0) {
            return msr_daemon_run(5000);
        }
    }

    const char* lang = getenv("UV_LANG");
    if (!lang) lang = getenv("LANG");
    if (lang && strncmp(lang, "en", 2) == 0) {
        i18n_set_lang("en");
    } else {
        i18n_set_lang("zh");
    }

    return uv_gui_run(argc, argv);
}
