// Драйвер: запускает настоящую main() игры на заглушках raylib.
#include <stdio.h>
int game_main(int argc, char **argv);
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Прогон настоящего main() ===\n");
    int rc = game_main(argc, argv);
    printf("main() завершилась с кодом %d\n", rc);
    return rc;
}
