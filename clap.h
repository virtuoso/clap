#ifndef __CLAP_CLAP_H__
#define __CLAP_CLAP_H__

struct fps_data {
    struct timespec ts_prev, ts_delta;
    unsigned long   fps_fine, fps_coarse, seconds, count;
};

void clap_fps_calc(struct fps_data *f);

struct clap_config {
    unsigned long   debug   : 1,
                    quiet   : 1;
};

int clap_init(struct clap_config *cfg, int argc, char **argv, char **envp);
void clap_done(int status);
int clap_restart(void);

#endif /* __CLAP_CLAP_H__ */
