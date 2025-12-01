#ifndef HANDLERS_H
#define HANDLERS_H

void handle_register(int fd, char *username, char *password);
void handle_login(int fd, char *username, char *password);

#endif
