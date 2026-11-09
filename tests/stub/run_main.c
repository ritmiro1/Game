// Драйвер: запускает настоящую main() игры на заглушках raylib.
#include <stdio.h>
int game_main(void);
int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Прогон настоящего main() ===\n");
    int rc = game_main();
    printf("main() завершилась с кодом %d\n", rc);
    return rc;
}
