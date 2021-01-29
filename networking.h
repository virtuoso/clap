#ifndef __CLAP_NETWORKING_H__
#define __CLAP_NETWORKING_H__

enum mode {
    CLIENT = 0,
    SERVER,
    LISTEN,
};

struct networking_config {
    const char  *server_ip;
    unsigned int server_port;
    unsigned int server_wsport;
};

int networking_init(struct networking_config *cfg, enum mode mode);
void networking_poll(void);
void networking_done(void);
void networking_broadcast_restart(void);

#endif /* __CLAP_NETWORKING_H__ */