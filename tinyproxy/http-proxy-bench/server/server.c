#include "mongoose.h"

static void event_handler(struct mg_connection *c, int ev, void *ev_data) {
    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        mg_http_reply(c, 200, 
            "Content-Type: text/plain\r\n", 
            "%.*s", (int)hm->body.len, hm->body.buf);
    }
}

int main(void) {
    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, "http://127.0.0.1:8080", event_handler, &mgr);
    for (;;) {
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);
    return 0;
}
