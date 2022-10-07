/*
 *
 *
 *
 */

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>

#define BUFFER_SIZE 1024

int main( int argc, char *argv[], char **envp ) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server> <port>\n\r", argv[0]);
        return 1;
    }

    struct addrinfo *res;

    if(getaddrinfo(argv[1], argv[2], NULL, &res) != 0) {
        fprintf(stderr, "Error: getaddrinfo() failed.\n\r");
        return 1;
    }

    struct addrinfo *addr;
    int sck_fd = socket(AF_INET, SOCK_STREAM, 0);

    printf("Attempting to connect...\n\r");

    for(addr = res; addr != NULL; addr = addr->ai_next) {
        if(connect(sck_fd, addr->ai_addr, addr->ai_addrlen) >= 0) {
            printf("Connected successfully!\n\r");
            break;
        } else if (addr->ai_next == NULL) {
            printf("Failed to connect.\n\r");
            return 1;
        }
    }

    freeaddrinfo(res);

    initscr();

    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);
    noecho();

    WINDOW *topbar = subwin(stdscr, 1, COLS, 0, 0);
    WINDOW *output = subwin(stdscr, LINES-2, COLS, 1, 0);
    WINDOW *input  = subwin(stdscr, 1, COLS, LINES - 1, 0);

    if (topbar == NULL || output == NULL || input == NULL) {
        fprintf(stderr, "subwin() failed to allocate memory!\n\r");
        return 1;
    }

    scrollok(output, TRUE);

    if (has_colors()) {
        if (start_color() == OK) {
            /* TODO - improve this and handle all fg/bg color pairs */
            init_pair(0, COLOR_WHITE, COLOR_BLACK);
            init_pair(1, COLOR_RED, COLOR_BLACK);
            init_pair(2, COLOR_GREEN, COLOR_BLACK);
            init_pair(3, COLOR_YELLOW, COLOR_BLACK);
            init_pair(4, COLOR_BLUE, COLOR_BLACK);
            init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
            init_pair(6, COLOR_CYAN, COLOR_BLACK);
            init_pair(7, COLOR_WHITE, COLOR_BLACK);
        }
    }

    /* TODO - improve these buffer names */
    char inp_buf[BUFFER_SIZE];
    char comm_buf[BUFFER_SIZE];
    size_t user_max_length = 0;
    char out_buf[BUFFER_SIZE];
    char last_chunk[BUFFER_SIZE];
    int out_ptr = 0;
    int inChar, i, num_read;

    fd_set fds;
    FD_ZERO(&fds);

    memset(comm_buf, '\0', BUFFER_SIZE);
    memset(inp_buf, '\0', BUFFER_SIZE);
    memset(out_buf, '\0', BUFFER_SIZE);
    memset(last_chunk, '\0', BUFFER_SIZE);

    while (1) {
        /* TODO - convert from select() to poll() */
        FD_SET(0, &fds);
        FD_SET(sck_fd, &fds);

        if (select(sck_fd+1, &fds, NULL, NULL, NULL) == -1) {
            endwin();
            fprintf(stderr, "Error during select()\n\r");
            return 1;
        }

        /* TODO - does not detect idling out when afk */
        if (FD_ISSET(sck_fd, &fds)) {
            if (last_chunk[0] != '\0') {
                strncpy(inp_buf, last_chunk, strlen(last_chunk));

                num_read = read(sck_fd,
                                inp_buf + strlen(last_chunk),
                                (BUFFER_SIZE - strlen(last_chunk)) );

                memset(last_chunk, '\0', BUFFER_SIZE);
            } else {
                num_read = read(sck_fd, inp_buf, BUFFER_SIZE);
            }

            if (num_read == -1) {
                printf("error read()'ing\n\r");
                return 1;
            } else if (num_read == 0) {
                printf( "Connection closed.\n\n\r");
                break;
            }

            memset(out_buf, '\0', BUFFER_SIZE);
            out_ptr = 0;

            for(i = 0; i < num_read; i++) {
                /* catch telnet protocol commands and ignore them */
                if(inp_buf[i] == (char) 255) {
                    /* 251 and 253 require skipping two bytes */
                    if (inp_buf[i+1] == (char) 251 ||
                        inp_buf[i+1] == (char) 253) {
                        i += 2;
                    } else {
                        /* others only require one */
                        i++;
                    }
                /* next catch colors and other text attributes */
                } else if (inp_buf[i] == 27) {
                    int s = i;
                    /* flush existing buffer */
                    if (out_ptr > 0) {
                        wprintw(output, "%s", out_buf);
                        memset(out_buf, '\0', BUFFER_SIZE);
                        out_ptr = 0;
                    }

                    /* TODO - this color parsing logic is a massive hack */
                    while (inp_buf[i] != 'm') {
                        if (i == num_read) {
                            strncpy(last_chunk, inp_buf+s, i-s+1);
                    /* TODO - this should probably break all the way out */
                            break;
                        }

                        i++;
                    }
                    if (last_chunk[0] != '\0') break;

                    /* TODO - actually make sure start_color() worked */
                    wattrset(output, COLOR_PAIR(atoi(&inp_buf[i-1])));

                    if (inp_buf[s+2] != '0') {
                        wattron(output, A_BOLD);
                    } else {
                        wattron(output, A_NORMAL);
                    }
                } else {
                    /* not telnet command or a color code, must be data */

                    if (out_ptr < BUFFER_SIZE - 1) {
                        out_buf[out_ptr++] = inp_buf[i];
                    } else {
                        wprintw(output, "%s", out_buf);
                        memset(out_buf, '\0', BUFFER_SIZE);
                        out_ptr = 0;
                        mvwprintw(topbar, 0, 0, "BLOOP!        ");

                        out_buf[out_ptr++] = inp_buf[i];

                    }
                }
            }

            wprintw(output, "%s", out_buf);
        }

        if (FD_ISSET(0, &fds)) {
            if ((inChar = getch()) != ERR) {
                if (inChar == '\n') {
                    comm_buf[strlen(comm_buf)] = '\n';

                    if ((i = write(sck_fd, comm_buf, strlen(comm_buf))) == -1) {
                        endwin();
                        fprintf(stderr, "Error write()ing to socket\n\r");
                        return 1;
                    }

                    waddstr(output, comm_buf);
                    wclear(input);
                    memset(comm_buf, '\0', BUFFER_SIZE);
                } else if (inChar == KEY_BACKSPACE || inChar == KEY_DC || inChar == 127) {
                    if (strlen(comm_buf) > 0) {
                        comm_buf[strlen(comm_buf)-1] = '\0';
                        mvwdelch(input, 0, strlen(comm_buf));
                    }
                /* TODO - catch arrows and react sensibly */
                } else {
                    wprintw(input, "%c", inChar);
                    comm_buf[strlen(comm_buf)] = inChar;
                }
            }
        }

        wrefresh(topbar);
        wrefresh(output);
        wrefresh(input);
    }

    endwin();

    return 0;
}
