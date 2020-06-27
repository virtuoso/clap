#ifndef __CLAP_CLAP_H__
#define __CLAP_CLAP_H__

struct clap_config {
    unsigned long   debug   : 1;
};

int clap_init(struct clap_config *cfg);
void clap_done(int status);

#endif /* __CLAP_CLAP_H__ */
