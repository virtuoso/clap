#ifndef __CLAP_CLAP_H__
#define __CLAP_CLAP_H__

struct clap_config {
    unsigned long   debug   : 1,
                    quiet   : 1;
};

int clap_init(struct clap_config *cfg, int argc, char **argv, char **envp);
void clap_done(int status);
int clap_restart(void);

#endif /* __CLAP_CLAP_H__ */
