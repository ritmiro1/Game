// ============================================================================
//  sim_test.c — автономная проверка игровой логики без графики
//  Собирается нативным gcc: gcc -O2 -I src/sim tests/sim_test.c src/sim/*.c
// ============================================================================
#include "core.h"

static Game G;
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  [FAIL] %s\n", msg); failures++; } else { printf("  [ ok ] %s\n", msg); } } while (0)

static void step(int input, Vec3 move, Vec3 look, float dt) {
    SimUpdate(&G, dt, input, move, look);
}

int main(void) {
    printf("=== ТИХИЙ ЗАЛИВ: тест симуляции ===\n");
    SimInit(&G, 20251109u);
    printf("Мир: сид %u, деревья %d, камни %d, камыш %d\n", G.worldSeed,
           G.terrain.treeCount, G.terrain.rockCount, G.terrain.reedCount);
    printf("Причал: %.1f %.1f | Маяк: %.1f %.1f (h=%.1f)\n",
           G.terrain.lm.dockPos.x, G.terrain.lm.dockPos.z,
           G.terrain.lm.lighthouse.x, G.terrain.lm.lighthouse.z, G.terrain.lm.lighthouse.y);

    // --- 1. Игрок стоит на земле, не проваливается
    step(0, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
    float gh = WorldHeight(&G, G.player.pos.x, G.player.pos.z);
    printf("Игрок: y=%.2f земля=%.2f режим=%d\n", G.player.pos.y, gh, (int)G.player.mode);
    CHECK(fabsf(G.player.pos.y - gh) < 0.6f, "игрок стоит на поверхности");

    // --- 2. Ходьба вперёд 3 секунды
    Vec3 p0 = G.player.pos;
    for (int i = 0; i < 180; i++) step(0, v3(0, 0, 1), v3(0, 0, 0), 1.0f / 60.0f);
    float moved = v3len(v3flat(v3sub(G.player.pos, p0)));
    printf("Пройдено за 3 с: %.2f м (режим %d)\n", moved, (int)G.player.mode);
    CHECK(moved > 2.0f, "игрок перемещается по вводу");

    // --- 3. Бег тратит стамину
    float stBefore = G.player.stamina;
    for (int i = 0; i < 120; i++) step(IN_SPRINT, v3(0, 0, 1), v3(0, 0, 0), 1.0f / 60.0f);
    printf("Стамина: %.2f -> %.2f\n", stBefore, G.player.stamina);
    CHECK(G.player.stamina < stBefore, "бег расходует стамину");

    // --- 4. Посадка в лодку
    G.player.pos = v3(G.terrain.lm.boatStart.x, 0.6f, G.terrain.lm.boatStart.z);
    G.player.mode = PM_FOOT;
    for (int i = 0; i < 4; i++) step(IN_BOARD, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
    printf("Режим после F: %d (2 = в лодке)\n", (int)G.player.mode);
    CHECK(G.player.mode == PM_BOAT, "игрок садится в лодку");
    CHECK(G.boat.occupied, "лодка помечена занятой");

    // --- 5. Лодка движется по тяге
    Vec3 b0 = G.boat.pos;
    for (int i = 0; i < 180; i++) step(0, v3(0, 0, 0.9f), v3(0, 0, 0), 1.0f / 60.0f);
    float bmoved = v3len(v3flat(v3sub(G.boat.pos, b0)));
    printf("Лодка прошла: %.2f м, скорость %.2f м/с, крен %.3f\n",
           bmoved, v3len(G.boat.vel), G.boat.roll);
    CHECK(bmoved > 4.0f, "лодка управляется тягой");
    for (int i = 0; i < 120; i++) step(0, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);

    // --- 6. Заброс и поклёвка
    G.player.yaw = G.terrain.lm.boatYaw + 1.2f;
    G.player.pitch = -0.05f;
    int hooked = 0, casts = 0;
    for (int attempt = 0; attempt < 6 && !hooked; attempt++) {
        // нажать ЛКМ (заряд) и отпустить
        step(IN_FISH, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
        for (int i = 0; i < 70; i++) step(IN_FISH, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
        step(0, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
        casts++;
        printf("  заброс %d: фаза %d, глубина %.2f м\n", attempt + 1, (int)G.fish_rod.phase, G.fish_rod.depthAtBobber);
        // ждём поклёвку максимум 40 с
        for (int i = 0; i < 2400; i++) {
            step(0, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
            if (G.fish_rod.biteWindow > 0.0f) break;
            if (G.fish_rod.phase == FISH_IDLE) break;
        }
        if (G.fish_rod.biteWindow > 0.0f) {
            step(IN_FISH, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
            if (G.fish_rod.phase == FISH_HOOKED) hooked = 1;
        }
    }
    printf("Забросов: %d, подсечка: %d, вид: %s, вес %.2f кг\n", casts, hooked,
           hooked ? FishGet(G.fish_rod.hookedSpecies)->name : "-", G.fish_rod.hookedWeight);
    CHECK(hooked == 1, "рыба клюёт и подсекается");

    // --- 7. Вываживание
    if (hooked) {
        int caught = 0;
        for (int i = 0; i < 60 * 90; i++) {
            // Простая стратегия игрока: тянуть, пока натяжение ниже 0.72
            int in = (G.fish_rod.tension < 0.72f) ? IN_FISH : 0;
            step(in, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
            if (G.fishCaught > 0) { caught = 1; break; }
            if (G.fish_rod.phase == FISH_IDLE) break;
        }
        printf("Вываживание: улов=%d, рыб поймано %d, натяжение %.2f, прогресс %.2f\n",
               caught, G.fishCaught, G.fish_rod.tension, G.fish_rod.reelProgress);
        CHECK(caught == 1, "рыба вываживается и попадает в садок");
        CHECK(G.inv.count > 0, "улов есть в инвентаре");
    }

    // --- 8. Достижения и квесты
    for (int i = 0; i < 60; i++) step(0, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
    CHECK(G.ach[0].unlocked, "достижение «Первый улов» открыто");

    // --- 9. Разговор с NPC
    int nn = NpcNearest(&G, 500.0f);
    printf("Ближайший NPC: %d (%s)\n", nn, nn >= 0 ? G.npc[nn].name : "-");
    if (nn >= 0) {
        G.player.pos = G.npc[nn].pos;
        NpcTalkBegin(&G, nn);
        int lines = 0;
        for (int i = 0; i < MAX_DIALOG; i++) if (G.dialogText[i][0]) lines++;
        printf("Реплик в диалоге: %d\n", lines);
        CHECK(lines > 0, "диалог содержит текст");
        NpcTalkEnd(&G);
        CHECK(G.quests[0].state == QS_ACTIVE, "квест выдаётся при разговоре");
    }

    // --- 10. День/ночь и погода
    float h0 = G.env.timeOfDay;
    for (int i = 0; i < 60 * 60; i++) step(0, v3(0, 0, 0), v3(0, 0, 0), 1.0f / 60.0f);
    printf("Время: %.2f -> %.2f, погода %d, ветер %.2f, дождь %.2f\n",
           h0, G.env.timeOfDay, (int)G.env.weather, G.env.wind, G.env.rainAmount);
    CHECK(fabsf(G.env.timeOfDay - h0) > 0.5f, "время суток идёт");

    // --- 11. Сохранение/загрузка
    BuildSavePath(&G);
    const char *path = "/tmp/tb_save_test.json";
    bool saved = SaveGame(&G, path);
    printf("Сохранение: %s (путь по умолчанию: %s)\n", saved ? "да" : "нет", G.savePath);
    CHECK(saved, "состояние сохраняется в файл");
    static Game G2;
    bool loaded = LoadGame(&G2, path);
    CHECK(loaded, "состояние загружается");
    printf("После загрузки: рыб %d, монет %d, время %.2f, квест0 %d/%d\n",
           G2.fishCaught, G2.inv.coins, G2.env.timeOfDay, G2.quests[0].progress, G2.quests[0].targetCount);
    CHECK(G2.fishCaught == G.fishCaught, "счётчик улова совпадает");
    CHECK(G2.inv.coins == G.inv.coins, "монеты совпадают");
    CHECK(fabsf(G2.env.dayFraction - G.env.dayFraction) < 0.01f, "время суток совпадает");
    CHECK(G2.inv.count == G.inv.count, "инвентарь совпадает");

    // --- 12. Продажа рыбы
    int coinsBefore = G.inv.coins;
    InventorySellAllFish(&G);
    printf("Продажа: монет %d -> %d\n", coinsBefore, G.inv.coins);
    CHECK(G.inv.coins >= coinsBefore, "продажа рыбы не уменьшает монеты");

    // --- 13. Магазин
    G.inv.coins = 2000;
    int rods = G.inv.rodTier;
    bool bought = false;
    for (int i = 0; i < ShopItemCount(); i++) {
        if (strstr(ShopItemName(i), "III")) { bought = ShopBuy(&G, i); break; }
    }
    printf("Магазин: удилище %d -> %d, покупка %d\n", rods, G.inv.rodTier, bought);
    CHECK(G.inv.rodTier > rods, "магазин улучшает удилище");

    // --- 14. Долгий прогон (10 минут игрового времени) без падений
    printf("Долгий прогон...\n");
    float maxY = -1e9f, minY = 1e9f;
    for (int i = 0; i < 60 * 600; i++) {
        Vec3 mv = v3(sinf((float)i * 0.01f), 0, cosf((float)i * 0.013f));
        step(0, mv, v3(sinf((float)i * 0.02f) * 0.4f, 0, 0), 1.0f / 60.0f);
        if (G.player.pos.y > maxY) maxY = G.player.pos.y;
        if (G.player.pos.y < minY) minY = G.player.pos.y;
        if (i % 6000 == 0) printf("  t=%.0f c: pos=(%.1f, %.1f, %.1f) режим=%d погода=%d\n",
                                  (float)i / 60.0f, G.player.pos.x, G.player.pos.y, G.player.pos.z,
                                  (int)G.player.mode, (int)G.env.weather);
    }
    printf("Диапазон высот игрока за прогон: %.2f .. %.2f\n", minY, maxY);
    CHECK(maxY < 60.0f && minY > -35.0f, "игрок остаётся в пределах мира");
    CHECK(G.npc[0].name != NULL, "NPC живы после прогона");

    printf("\n=== ИТОГ: %d ошибок ===\n", failures);
    return failures == 0 ? 0 : 1;
}
