extern void imp_init (int argc, char **argv);
extern void imp_send_message (uint8_t *data, int length);
extern void imp_receive_message (uint8_t *data, int *length);
extern void imp_host_ready (int flag);
extern void (*imp_imp_ready) (int flag);

