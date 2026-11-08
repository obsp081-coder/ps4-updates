
/* PS4 Auto Updater v0.3.3
   Local GoldHEN FTP connection test.
   No downloads or system modifications.
*/

#include <orbis/Net.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

#define REPORT_DIR "/data/PS4AutoUpdater"
#define REPORT_FILE REPORT_DIR "/ftp_probe.txt"

int main(void) {
    if (mkdir(REPORT_DIR, 0777) != 0 &&
        errno != EEXIST) {
        return 1;
    }

    FILE *report = fopen(REPORT_FILE, "wb");
    if (!report) return 2;

    fputs("PS4 Auto Updater - FTP diagnostic v0.3.3\n", report);
    fputs("Target: 127.0.0.1:2121\n", report);

    int init_ret = sceNetInit();

    fprintf(report, "sceNetInit: %d (0x%08X)\n",
            init_ret, (uint32_t)init_ret);

    OrbisNetId sock = sceNetSocket(
        "PS4UpdaterProbe",
        ORBIS_NET_AF_INET,
        ORBIS_NET_SOCK_STREAM,
        0
    );

    fprintf(report, "sceNetSocket: %d (0x%08X)\n",
            sock, (uint32_t)sock);

    if (sock < 0) {
        fputs("Result: socket creation failed\n", report);
        fclose(report);
        return 3;
    }

    OrbisNetSockaddr address = {0};
    address.len = (uint8_t)sizeof(address);
    address.sa_family = ORBIS_NET_AF_INET;

    /* Port 2121 */
    address.sa_data[0] = 0x08;
    address.sa_data[1] = 0x49;

    /* Localhost 127.0.0.1 */
    address.sa_data[2] = 127;
    address.sa_data[3] = 0;
    address.sa_data[4] = 0;
    address.sa_data[5] = 1;

    int connection = sceNetConnect(
        sock, &address, sizeof(address)
    );

    fprintf(report, "sceNetConnect: %d (0x%08X)\n",
            connection, (uint32_t)connection);

    if (connection == 0) {
        fputs("Result: CONNECTED\n", report);
    } else {
        fputs("Result: NOT_CONNECTED\n", report);
    }

    sceNetSocketClose(sock);
    fclose(report);

    return connection == 0 ? 0 : 4;
}
