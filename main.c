#include "raylib.h"
#include "raymath.h"
#include <stdlib.h>
#include <string.h>

#define MAX_PROTESTERS 100
#define MAX_PROJECTILES 200
#define MAX_POLICE 20
#define MAX_BARRICADES 0
#define MAX_GAS 5
#define GAME_DURATION 300.0f // 5 minutes
#define POLICE_COUNT MAX_POLICE

float police_cooldown[MAX_POLICE];

typedef enum
{
    IDLE,
    CHANT,
    RIOT,
    FLEE,
    ARRESTED
} ProtesterState;

typedef struct
{
    Vector2 pos;
    Vector2 vel;
    ProtesterState state;
    float morale;
    int group_id;
    Vector2 target_pos;
    float behavior_timer;
    float animation_timer;
    bool is_agitator;
    bool alive;
    Texture2D sprite; // legacy single sprite
    Texture2D sprites[2];    // idle/chant animation frames
    Texture2D run_sprites[3]; // riot/flee animation frames
    int anim_frame;
    float anim_timer;
    bool face_right;
    float stoneCooldown; // seconds left until can throw again
    float distance;
    float max_distance;
} Protester;

typedef enum
{
    PATROL,
    DEPLOY,
    ARREST,
    INTERVENE,
    RETREAT
} PoliceState;

typedef struct
{
    Vector2 pos;
    Vector2 vel;
    PoliceState state;
    float timer;
    float health;
    bool alive;
    Texture2D sprite;
    int id; // Add unique id for police
    Texture2D sprites[2];
    Texture2D run_sprites[3];
    int anim_frame;
    float anim_timer;
    bool face_right;
} Police;

typedef struct
{
    Vector2 pos;
    float radius;
    float timer;
    bool active;
} TearGas;

typedef enum { STONE, BULLET, HELICOPTER_BULLET } ProjectileType;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    int owner_id;  // Protester or police ID
    float lifetime;
    bool active;
    ProjectileType type;
    float damage;
    float distance;
    float max_distance;
    Texture2D sprite;  // Stub for later
} Projectile;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    bool active;
    float appear_timer;
    int shots_fired;
    float shot_cooldown;
    Texture2D sprite;
    float spawn_times[3];
    int current_spawn;
} Helicopter;

typedef enum
{
    MENU_START,
    MENU_PLAY,
    MENU_PAUSE,
    MENU_WIN,
    MENU_LOSE,
    MENU_TUTORIAL
} GameMenu;

typedef struct
{
    Protester protesters[MAX_PROTESTERS];
    Police police[MAX_POLICE];
    TearGas gas[MAX_GAS];
    Projectile projectiles[MAX_PROJECTILES];
    bool selected[MAX_PROTESTERS];
    bool isSelecting;
    Vector2 selectStart, selectEnd;
    float globalMorale;
    double lastMoraleTime;
    double gameStartTime;
    double policeSurgeTimer;
    bool policeSurgeActive;
    double policeSurgeEnd;
    GameMenu menuState;
    int protesterCount;
    int policeCount;
    float controlProgress;
    double controlStartTime;
    int protesters_arrested;
    float max_morale_reached;
} GameState;

Helicopter helicopter;

void InitHelicopter(GameState *game) {
    helicopter.active = 0;
    helicopter.current_spawn = 0;
    for (int i = 0; i < 3; i++) {
        helicopter.spawn_times[i] = GetRandomValue(0, 300);
    }
    for (int i = 0; i < 2; i++) {
        for (int j = i+1; j < 3; j++) {
            if (helicopter.spawn_times[i] > helicopter.spawn_times[j]) {
                float temp = helicopter.spawn_times[i];
                helicopter.spawn_times[i] = helicopter.spawn_times[j];
                helicopter.spawn_times[j] = temp;
            }
        }
    }
    for (int i = 1; i < 3; i++) {
        if (helicopter.spawn_times[i] - helicopter.spawn_times[i-1] < 30.0f) {
            helicopter.spawn_times[i] += 30.0f;
            if (helicopter.spawn_times[i] > 300.0f) helicopter.spawn_times[i] = 300.0f;
        }
    }
}

void UpdateHelicopter(GameState *game, float dt) {
    double timer = GetTime() - game->gameStartTime;
    if (timer >= 300.0f) return;
    if (!helicopter.active && helicopter.current_spawn < 3 && timer >= helicopter.spawn_times[helicopter.current_spawn]) {
        helicopter.active = 1;
        helicopter.pos = (Vector2){1600 + 32, 50.0f};
        helicopter.vel = (Vector2){-4.0f, 0.0f};
        helicopter.appear_timer = GetRandomValue(10, 20);
        helicopter.shots_fired = 0;
        helicopter.shot_cooldown = 0.0f;
        helicopter.current_spawn++;
    }
    if (helicopter.active) {
        helicopter.pos.x += helicopter.vel.x;
        helicopter.pos.y += sinf(timer * 2.0f) * 0.5f;
        helicopter.appear_timer -= dt;
        helicopter.shot_cooldown -= dt;
        int shot_limit = 4;
        if (helicopter.pos.x < -32 || helicopter.shots_fired >= shot_limit) {
            helicopter.active = 0;
            return;
        }
        if (helicopter.shot_cooldown <= 0) {
            int target_idx = -1;
            for (int tries = 0; tries < 10; tries++) {
                int j = GetRandomValue(0, MAX_PROTESTERS - 1);
                if (game->protesters[j].alive && game->protesters[j].state == RIOT) {
                    target_idx = j;
                    break;
                }
            }
            if (target_idx == -1) {
                for (int j = 0; j < MAX_PROTESTERS; j++) {
                    if (game->protesters[j].alive) {
                        target_idx = j;
                        break;
                    }
                }
            }
            if (target_idx != -1) {
                Vector2 target = game->protesters[target_idx].pos;
                for (int i = 0; i < MAX_PROJECTILES; i++) {
                    if (!game->projectiles[i].active) {
                        game->projectiles[i].pos = helicopter.pos;
                        Vector2 dir = Vector2Normalize(Vector2Subtract(target, helicopter.pos));
                        game->projectiles[i].vel = Vector2Scale(dir, 400.0f);
                        game->projectiles[i].owner_id = -1;
                        game->projectiles[i].lifetime = 0.0f;
                        game->projectiles[i].active = 1;
                        game->projectiles[i].type = HELICOPTER_BULLET;
                        game->projectiles[i].damage = 100.0f;
                        game->projectiles[i].distance = 0.0f;
                        game->projectiles[i].max_distance = 1600.0f;
                        helicopter.shots_fired++;
                        helicopter.shot_cooldown = GetRandomValue(2, 4);
                        break;
                    }
                }
            }
        }
    }
}

void DrawHelicopter(GameState *game, Texture2D helicopterSprite) {
    if (helicopter.active) {
        if (helicopterSprite.id != 0) {
            DrawTexture(helicopterSprite, (int)(helicopter.pos.x - 16), (int)(helicopter.pos.y - 8), WHITE);
        } else {
            DrawRectangle((int)(helicopter.pos.x - 16), (int)(helicopter.pos.y - 8), 32, 16, GRAY);
        }
    }
}

void InitGame(GameState *game);
void UpdateGame(GameState *game);
void UpdateProtesters(GameState *game);
void ShootBullet(GameState *game, Police* p, Vector2 target);
void UpdatePolice(GameState *game);
void UpdateTearGas(GameState *game);
void HandleInput(GameState *game);
void DrawGame(GameState *game, Font pixelFont, Texture2D *textures);
void DrawUI(GameState *game, Font pixelFont, Texture2D *textures);
bool CheckWinCondition(GameState *game);
bool CheckLoseCondition(GameState *game);

void EnforceProtesterBoundaries(Protester *protester, GameState *game, int index)
{
    const float minDistance = 20.0f;
    Vector2 separation = {0, 0};
    int sepCount = 0;

    for (int j = 0; j < MAX_PROTESTERS; j++)
    {
        if (index == j || !game->protesters[j].alive)
            continue;

        float dist = Vector2Distance(protester->pos, game->protesters[j].pos);
        if (dist < minDistance && dist > 0)
        {
            Vector2 diff = Vector2Subtract(protester->pos, game->protesters[j].pos);
            diff = Vector2Scale(Vector2Normalize(diff), minDistance / (dist + 1));
            separation = Vector2Add(separation, diff);
            sepCount++;
        }
    }

    for (int p = 0; p < MAX_POLICE; p++)
    {
        if (!game->police[p].alive)
            continue;
        float dist = Vector2Distance(protester->pos, game->police[p].pos);
        if (protester->state != RIOT && dist < 80.0f && dist > 0)
        {
            Vector2 diff = Vector2Subtract(protester->pos, game->police[p].pos);
            diff = Vector2Scale(Vector2Normalize(diff), 100.0f / (dist + 1));
            separation = Vector2Add(separation, diff);
            sepCount++;
        }
    }

    if (sepCount > 0)
    {
        separation = Vector2Scale(separation, 1.0f / sepCount);
        protester->vel = Vector2Add(protester->vel, Vector2Scale(separation, 2.0f));
    }
}

void InitGame(GameState *game)
{
    memset(game, 0, sizeof(GameState));

    game->globalMorale = 50.0f;
    game->lastMoraleTime = GetTime();
    game->gameStartTime = GetTime();
    game->policeSurgeTimer = GetTime();
    game->menuState = MENU_START;
    game->protesterCount = MAX_PROTESTERS;
    game->policeCount = MAX_POLICE;

    for (int i = 0; i < MAX_PROTESTERS; i++) {
        game->protesters[i].pos = (Vector2){GetRandomValue(50, 300), GetRandomValue(302, 740)};
        game->protesters[i].vel = (Vector2){0, 0};
        game->protesters[i].state = (i % 3 == 0) ? CHANT : IDLE;
        game->protesters[i].morale = GetRandomValue(80, 100);
        game->protesters[i].is_agitator = (i < 10);
        game->protesters[i].alive = true;
        game->protesters[i].group_id = i / 10;
        game->protesters[i].target_pos = game->protesters[i].pos;
        game->protesters[i].stoneCooldown = 0.0f;
        game->protesters[i].distance = 0.0f;
        game->protesters[i].max_distance = 320.0f;
        game->protesters[i].anim_frame = 0;
        game->protesters[i].anim_timer = 0.0f;
        game->protesters[i].face_right = true;
        game->protesters[i].sprites[0] = LoadTexture("protester.png");
        game->protesters[i].sprites[1] = LoadTexture("protester2.png");
        game->protesters[i].run_sprites[0] = LoadTexture("protestersRun1.png");
        game->protesters[i].run_sprites[1] = LoadTexture("protestersRun2.png");
        game->protesters[i].run_sprites[2] = LoadTexture("protestersRun3.png");
        for (int j = 0; j < 2; j++) if (game->protesters[i].sprites[j].id != 0) SetTextureFilter(game->protesters[i].sprites[j], TEXTURE_FILTER_POINT);
        for (int j = 0; j < 3; j++) if (game->protesters[i].run_sprites[j].id != 0) SetTextureFilter(game->protesters[i].run_sprites[j], TEXTURE_FILTER_POINT);
    }

    for (int i = 0; i < MAX_POLICE; i++) {
        game->police[i].pos = (Vector2){GetRandomValue(1200, 1500), GetRandomValue(302, 740)};
        game->police[i].vel = (Vector2){0, 0};
        game->police[i].state = PATROL;
        game->police[i].timer = 0.0f;
        game->police[i].health = 100.0f;
        game->police[i].alive = true;
        game->police[i].id = i;
        game->police[i].sprites[0] = LoadTexture("police.png");
        game->police[i].sprites[1] = LoadTexture("police2.png");
        game->police[i].run_sprites[0] = LoadTexture("policeRun1.png");
        game->police[i].run_sprites[1] = LoadTexture("policeRun2.png");
        game->police[i].run_sprites[2] = LoadTexture("policeRun3.png");
        for (int j = 0; j < 2; j++) if (game->police[i].sprites[j].id != 0) SetTextureFilter(game->police[i].sprites[j], TEXTURE_FILTER_POINT);
        for (int j = 0; j < 3; j++) if (game->police[i].run_sprites[j].id != 0) SetTextureFilter(game->police[i].run_sprites[j], TEXTURE_FILTER_POINT);
        game->police[i].anim_frame = 0;
        game->police[i].anim_timer = 0.0f;
        game->police[i].face_right = true;
        police_cooldown[i] = 0.0f;
    }

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        game->projectiles[i].active = false;
        game->projectiles[i].lifetime = 0.0f;
        game->projectiles[i].owner_id = -1;
        game->projectiles[i].pos = (Vector2){0,0};
        game->projectiles[i].vel = (Vector2){0,0};
        game->projectiles[i].distance = 0.0f;
        game->projectiles[i].max_distance = 320.0f;
    }

    InitHelicopter(game);
}

void UpdateProtesters(GameState *game)
{
    int chantingCount = 0;
    int activeProtesters = 0;
    for (int i = 0; i < MAX_PROTESTERS; i++) {
        Protester *p = &game->protesters[i];
        if (!p->alive || p->state == ARRESTED) continue;
        activeProtesters++;
        float cycle_time = (p->state == RIOT || p->state == FLEE) ? 0.2f : 0.4f;
        p->anim_timer += GetFrameTime();
        int frame_count = (p->state == RIOT || p->state == FLEE) ? 3 : 2;
        if (p->anim_timer >= cycle_time) {
            p->anim_frame = (p->anim_frame + 1) % frame_count;
            p->anim_timer = 0.0f;
        }
        p->face_right = (p->vel.x >= 0);

        if (p->stoneCooldown > 0.0f) {
            p->stoneCooldown -= GetFrameTime();
            if (p->stoneCooldown < 0.0f) p->stoneCooldown = 0.0f;
        }

        Vector2 targetForce = {0, 0};
        if (Vector2Distance(p->pos, p->target_pos) > 10.0f) {
            targetForce = Vector2Scale(Vector2Normalize(Vector2Subtract(p->target_pos, p->pos)), 0.8f);
        }

        Vector2 stateForce = {0, 0};
        float speedMultiplier = 1.0f;
        switch (p->state) {
            case CHANT:
                chantingCount++;
                speedMultiplier = 0.1f;
                for (int j = 0; j < MAX_PROTESTERS; j++) {
                    if (i != j && game->protesters[j].alive && Vector2Distance(p->pos, game->protesters[j].pos) < 60.0f) {
                        game->protesters[j].morale += 0.1f;
                    }
                }
                break;
            case RIOT:
                speedMultiplier = 2.0f;
                float closestDist = 9999.0f;
                Vector2 closestTarget = p->pos;
                for (int j = 0; j < MAX_POLICE; j++) {
                    if (game->police[j].alive) {
                        float dist = Vector2Distance(p->pos, game->police[j].pos);
                        if (dist < closestDist) {
                            closestDist = dist;
                            closestTarget = game->police[j].pos;
                        }
                    }
                }
                if (closestDist < 9999.0f) {
                    stateForce = Vector2Scale(Vector2Normalize(Vector2Subtract(closestTarget, p->pos)), 1.5f);
                }
                break;
            case FLEE:
                speedMultiplier = 3.0f;
                for (int j = 0; j < MAX_POLICE; j++) {
                    if (game->police[j].alive) {
                        float dist = Vector2Distance(p->pos, game->police[j].pos);
                        if (dist < 100.0f) {
                            Vector2 away = Vector2Scale(Vector2Normalize(Vector2Subtract(p->pos, game->police[j].pos)), 2.0f);
                            stateForce = Vector2Add(stateForce, away);
                        }
                    }
                }
                p->behavior_timer += GetFrameTime();
                if (p->behavior_timer > 5.0f) {
                    p->state = IDLE;
                    p->behavior_timer = 0.0f;
                }
                break;
            case IDLE:
            default:
                speedMultiplier = 1.0f;
                break;
        }
        EnforceProtesterBoundaries(p, game, i);
        Vector2 totalForce = Vector2Add(targetForce, stateForce);
        float maxSpeed = 2.5f * speedMultiplier;
        if (Vector2Length(totalForce) > maxSpeed) {
            totalForce = Vector2Scale(Vector2Normalize(totalForce), maxSpeed);
        }
        p->vel = Vector2Lerp(p->vel, totalForce, 0.3f);
        p->pos = Vector2Add(p->pos, p->vel);
        p->pos.x = Clamp(p->pos.x, 16, 1584);
        p->pos.y = Clamp(p->pos.y, 318, 724);
        if (p->state == CHANT) p->morale += 0.2f;
        if (p->state == FLEE) p->morale -= 0.5f;
        p->morale = Clamp(p->morale, 0.0f, 100.0f);

        if (p->state == RIOT) {
            for (int k = 0; k < MAX_POLICE; k++) {
                if (game->police[k].alive && Vector2Distance(p->pos, game->police[k].pos) < 20.0f) {
                    game->police[k].health -= 10.0f * GetFrameTime();
                    if (game->police[k].health <= 0.0f) {
                        game->police[k].alive = false;
                        game->globalMorale += 3.0f;
                    }
                }
            }
        }
    }

    if (chantingCount > 0) {
        game->globalMorale += (float)chantingCount * 0.1f;
    }

    double now = GetTime();
    if (now - game->lastMoraleTime > 1.0) {
        game->globalMorale -= 0.2f;
        game->lastMoraleTime = now;
    }

    game->globalMorale = Clamp(game->globalMorale, 0, 100);
    game->protesterCount = activeProtesters;

    if (game->globalMorale > game->max_morale_reached) {
        game->max_morale_reached = game->globalMorale;
    }
}

void UpdatePolice(GameState *game)
{
    int activePolice = 0;
    for (int i = 0; i < MAX_POLICE; i++) {
        Police *p = &game->police[i];
        if (!p->alive) continue;
        activePolice++;

        float cycle_time = (p->state == INTERVENE || p->state == DEPLOY) ? 0.2f : 0.4f;
        p->anim_timer += GetFrameTime();
        int frame_count = (p->state == INTERVENE || p->state == DEPLOY) ? 3 : 2;
        if (p->anim_timer >= cycle_time) {
            p->anim_frame = (p->anim_frame + 1) % frame_count;
            p->anim_timer = 0.0f;
        }
        p->face_right = (p->vel.x >= 0);

        p->timer -= GetFrameTime();

        float closestDist = 120.0f;
        int targetIdx = -1;
        for (int j = 0; j < MAX_PROTESTERS; j++) {
            if (!game->protesters[j].alive) continue;
            float dist = Vector2Distance(p->pos, game->protesters[j].pos);
            if (dist < closestDist) {
                closestDist = dist;
                targetIdx = j;
            }
        }
        if (targetIdx != -1 && police_cooldown[p->id] <= 0.0f) {
            ShootBullet(game, p, game->protesters[targetIdx].pos);
        }

        switch (p->state) {
        case PATROL: {
            Vector2 patrolTarget = {GetRandomValue(800, 1500), p->pos.y + GetRandomValue(-50, 50)};
            Vector2 toTarget = Vector2Subtract(patrolTarget, p->pos);
            if (Vector2Length(toTarget) > 5.0f) {
                p->vel = Vector2Scale(Vector2Normalize(toTarget), 1.0f);
            } else {
                p->vel = Vector2Scale(p->vel, 0.9f);
            }

            float closestDist = 150.0f;
            for (int j = 0; j < MAX_PROTESTERS; j++) {
                if (!game->protesters[j].alive) continue;
                float dist = Vector2Distance(p->pos, game->protesters[j].pos);
                if (dist < closestDist && game->protesters[j].state != FLEE) {
                    p->state = DEPLOY;
                    p->timer = 3.0f;
                    break;
                }
            }
            break;
        }
        case DEPLOY: {
            for (int g = 0; g < MAX_GAS; g++) {
                if (!game->gas[g].active) {
                    float closestDist = 200.0f;
                    Vector2 targetPos = p->pos;
                    for (int j = 0; j < MAX_PROTESTERS; j++) {
                        if (!game->protesters[j].alive) continue;
                        float dist = Vector2Distance(p->pos, game->protesters[j].pos);
                        if (dist < closestDist) {
                            closestDist = dist;
                            targetPos = game->protesters[j].pos;
                        }
                    }
                    if (closestDist < 200.0f) {
                        game->gas[g].pos = targetPos;
                        game->gas[g].radius = 5.0f;
                        game->gas[g].timer = 0.0f;
                        game->gas[g].active = true;
                    }
                    break;
                }
            }
            if (p->timer <= 0.0f) {
                p->state = PATROL;
            }
            break;
        }
        case INTERVENE: {
            Vector2 centerOfProtest = {0, 0};
            int protestCount = 0;
            for (int j = 0; j < MAX_PROTESTERS; j++) {
                if (game->protesters[j].alive && game->protesters[j].state != FLEE) {
                    centerOfProtest = Vector2Add(centerOfProtest, game->protesters[j].pos);
                    protestCount++;
                }
            }
            if (protestCount > 0) {
                centerOfProtest = Vector2Scale(centerOfProtest, 1.0f / protestCount);
                Vector2 toCenter = Vector2Subtract(centerOfProtest, p->pos);
                if (Vector2Length(toCenter) > 5.0f) {
                    p->vel = Vector2Scale(Vector2Normalize(toCenter), 2.0f);
                }
            }
            break;
        }
        case ARREST: {
            for (int j = 0; j < MAX_PROTESTERS; j++) {
                if (game->protesters[j].alive && game->protesters[j].state == FLEE &&
                    Vector2Distance(p->pos, game->protesters[j].pos) < 25.0f) {
                    game->protesters[j].state = ARRESTED;
                    game->protesters[j].alive = false;
                    game->globalMorale -= 5.0f;
                    game->protesters_arrested++;
                    p->state = PATROL;
                    break;
                }
            }
            break;
        }
        }
        p->pos = Vector2Add(p->pos, p->vel);
        p->pos.x = Clamp(p->pos.x, 16, 1584);
        p->pos.y = Clamp(p->pos.y, 318, 724);
    }
    game->policeCount = activePolice;
}

void UpdateTearGas(GameState *game)
{
    for (int g = 0; g < MAX_GAS; g++)
    {
        if (game->gas[g].active)
        {
            game->gas[g].radius += 25.0f * GetFrameTime();
            game->gas[g].timer += GetFrameTime();

            for (int j = 0; j < MAX_PROTESTERS; j++)
            {
                if (!game->protesters[j].alive || game->protesters[j].state == FLEE)
                    continue;

                float dist = Vector2Distance(game->gas[g].pos, game->protesters[j].pos);
                if (dist < game->gas[g].radius)
                {
                    game->protesters[j].state = FLEE;
                    game->protesters[j].morale -= 15;
                    game->protesters[j].behavior_timer = 0.0f;
                    game->globalMorale -= 0.5f;
                    game->protesters[j].target_pos = (Vector2){50, game->protesters[j].pos.y};
                }
            }

            if (game->gas[g].timer > 5.0f || game->gas[g].radius > 100.0f)
            {
                game->gas[g].active = false;
            }
        }
    }
}

int FindInactiveProjectile(GameState *game) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!game->projectiles[i].active) return i;
    }
    return -1;
}

void FireStone(GameState *game, Vector2 pos, Vector2 dir, int owner_id) {
    int idx = FindInactiveProjectile(game);
    if (idx < 0) return;
    
    // Find the nearest police as the target
    Vector2 targetDir = dir;
    float closestDist = 9999.0f;
    int closestPolice = -1;
    for (int i = 0; i < MAX_POLICE; i++) {
        if (game->police[i].alive) {
            float dist = Vector2Distance(pos, game->police[i].pos);
            if (dist < closestDist) {
                closestDist = dist;
                closestPolice = i;
            }
        }
    }
    if (closestPolice != -1) {
        targetDir = Vector2Subtract(game->police[closestPolice].pos, pos);
    }
    
    game->projectiles[idx].active = true;
    game->projectiles[idx].pos = pos;
    Vector2 norm = Vector2Normalize(targetDir);
    if (Vector2Length(norm) < 0.01f) norm = (Vector2){1,0};
    game->projectiles[idx].vel = Vector2Scale(norm, 350.0f);
    game->projectiles[idx].owner_id = owner_id;
    game->projectiles[idx].lifetime = 0.0f;
    game->projectiles[idx].distance = 0.0f;
    game->projectiles[idx].max_distance = 320.0f;
    game->projectiles[idx].type = STONE;
    game->projectiles[idx].damage = 25.0f;
}

void ShootBullet(GameState *game, Police* p, Vector2 target) {
    if (police_cooldown[p->id] > 0) return;
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!game->projectiles[i].active) {
            game->projectiles[i].pos = p->pos;
            Vector2 dir = Vector2Normalize(Vector2Subtract(target, p->pos));
            float angle = GetRandomValue(-5, 5) * DEG2RAD;
            game->projectiles[i].vel = Vector2Scale(Vector2Rotate(dir, angle), 350.0f);
            game->projectiles[i].owner_id = p->id;
            game->projectiles[i].lifetime = 0.0f;
            game->projectiles[i].active = true;
            game->projectiles[i].type = BULLET;
            game->projectiles[i].damage = 30.0f;
            game->projectiles[i].distance = 0.0f;
            game->projectiles[i].max_distance = 320.0f;
            police_cooldown[p->id] = 2.5f;
            break;
        }
    }
}

void HandleInput(GameState *game)
{
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        game->isSelecting = true;
        game->selectStart = GetMousePosition();
        game->selectEnd = game->selectStart;
    }

    if (game->isSelecting && IsMouseButtonDown(MOUSE_LEFT_BUTTON))
    {
        game->selectEnd = GetMousePosition();
    }

    if (game->isSelecting && IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
    {
        float minX = fminf(game->selectStart.x, game->selectEnd.x);
        float maxX = fmaxf(game->selectStart.x, game->selectEnd.x);
        float minY = fminf(game->selectStart.y, game->selectEnd.y);
        float maxY = fmaxf(game->selectStart.y, game->selectEnd.y);

        for (int i = 0; i < MAX_PROTESTERS; i++)
        {
            if (!game->protesters[i].alive)
                continue;
            Vector2 pos = game->protesters[i].pos;
            game->selected[i] = (pos.x >= minX && pos.x <= maxX &&
                                 pos.y >= minY && pos.y <= maxY);
        }
        game->isSelecting = false;
    }

    if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
    {
        Vector2 mousePos = GetMousePosition();
        for (int i = 0; i < MAX_PROTESTERS; i++)
        {
            if (game->selected[i] && game->protesters[i].alive)
            {
                game->protesters[i].target_pos = mousePos;
                switch (game->protesters[i].state)
                {
                case IDLE:
                    game->protesters[i].state = CHANT;
                    break;
                case CHANT:
                    game->protesters[i].state = RIOT;
                    break;
                case RIOT:
                case FLEE:
                    game->protesters[i].state = IDLE;
                    break;
                }
                if (game->protesters[i].stoneCooldown <= 0.0f) {
                    Vector2 dir = Vector2Subtract(mousePos, game->protesters[i].pos);
                    FireStone(game, game->protesters[i].pos, dir, i);
                    game->protesters[i].stoneCooldown = 0.2f;
                }
            }
        }
    }

    if (IsKeyPressed(KEY_A))
    {
        for (int i = 0; i < MAX_PROTESTERS; i++)
        {
            game->selected[i] = game->protesters[i].alive;
        }
    }

    if (IsKeyPressed(KEY_SPACE))
    {
        for (int i = 0; i < MAX_PROTESTERS; i++)
        {
            if (game->selected[i] && game->protesters[i].alive)
            {
                game->protesters[i].state = FLEE;
                game->protesters[i].target_pos = (Vector2){50, game->protesters[i].pos.y};
            }
        }
    }

    if (IsKeyPressed(KEY_T)) {
        Vector2 mousePos = GetMousePosition();
        for (int i = 0; i < MAX_PROTESTERS; i++) {
            if (game->selected[i] && game->protesters[i].alive) {
                if (game->protesters[i].stoneCooldown <= 0.0f) {
                    Vector2 dir = Vector2Subtract(mousePos, game->protesters[i].pos);
                    FireStone(game, game->protesters[i].pos, dir, i);
                    game->protesters[i].stoneCooldown = 0.2f;
                }
            }
        }
    }
}

bool CheckWinCondition(GameState *game)
{
    // Win if all police are defeated
    if (game->policeCount == 0) {
        return true;
    }

    // Calculate territory control
    int advancedProtesters = 0;
    for (int i = 0; i < MAX_PROTESTERS; i++)
    {
        if (game->protesters[i].alive && game->protesters[i].pos.x > 800)
        {
            advancedProtesters++;
        }
    }

    float controlPercentage = game->protesterCount > 0 ? (float)advancedProtesters / game->protesterCount : 0.0f;

    // Lowered threshold for morale and control percentage to make winning more achievable
    if (game->globalMorale > 60.0f && controlPercentage > 0.5f)
    {
        if (game->controlStartTime == 0)
        {
            game->controlStartTime = GetTime();
        }
        else if (GetTime() - game->controlStartTime > 10.0f) // Reduced time to hold control
        {
            return true;
        }
    }
    else
    {
        game->controlStartTime = 0;
    }

    return false;
}

bool CheckLoseCondition(GameState *game)
{
    double elapsed = GetTime() - game->gameStartTime;
    return (game->globalMorale < 10.0f ||
            game->protesterCount < 20 ||
            elapsed > GAME_DURATION);
}

void UpdateGame(GameState *game)
{
    if (game->menuState != MENU_PLAY)
        return;

    HandleInput(game);
    UpdateProtesters(game);
    UpdatePolice(game);
    UpdateTearGas(game);
    UpdateHelicopter(game, GetFrameTime());

    double now = GetTime();
    if (now - game->policeSurgeTimer > 45.0 && !game->policeSurgeActive)
    {
        game->policeSurgeActive = true;
        game->policeSurgeEnd = now + 15.0;
        for (int i = 0; i < MAX_POLICE; i++)
        {
            if (game->police[i].alive)
            {
                game->police[i].state = INTERVENE;
            }
        }
    }

    if (game->policeSurgeActive && now > game->policeSurgeEnd)
    {
        game->policeSurgeActive = false;
        game->policeSurgeTimer = now;
        for (int i = 0; i < MAX_POLICE; i++)
        {
            if (game->police[i].alive)
            {
                game->police[i].state = PATROL;
            }
        }
    }

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *proj = &game->projectiles[i];
        if (!proj->active) continue;
        float moveStep = Vector2Length(proj->vel) * GetFrameTime();
        proj->pos = Vector2Add(proj->pos, Vector2Scale(proj->vel, GetFrameTime()));
        proj->distance += moveStep;
        proj->lifetime += GetFrameTime();
        if (proj->distance > proj->max_distance || proj->lifetime > 2.0f ||
            proj->pos.x < 0 || proj->pos.x > 1600 ||
            proj->pos.y < 0 || proj->pos.y > 900) {
            proj->active = false;
            continue;
        }
        if (proj->type == HELICOPTER_BULLET) {
            for (int j = 0; j < MAX_PROTESTERS; j++) {
                Protester *pr = &game->protesters[j];
                if (pr->alive && Vector2Distance(proj->pos, pr->pos) < 8.0f) {
                    pr->morale -= proj->damage;
                    if (pr->morale <= 0) {
                        pr->alive = false;
                        game->globalMorale -= 10.0f;
                    }
                    proj->active = false;
                    break;
                }
            }
        } else if (proj->type == BULLET) {
            for (int j = 0; j < MAX_PROTESTERS; j++) {
                Protester *pr = &game->protesters[j];
                if (pr->alive && Vector2Distance(proj->pos, pr->pos) < 8.0f) {
                    pr->morale -= proj->damage;
                    if (pr->morale <= 0) {
                        pr->alive = false;
                        game->globalMorale -= 5.0f;
                    }
                    proj->active = false;
                    break;
                }
            }
        } else if (proj->type == STONE) {
            for (int j = 0; j < MAX_POLICE; j++) {
                Police *pol = &game->police[j];
                if (!pol->alive) continue;
                float dist = Vector2Distance(proj->pos, pol->pos);
                if (dist < 24.0f) {
                    pol->health -= proj->damage;
                    pol->vel = Vector2Add(pol->vel, Vector2Scale(proj->vel, 0.5f));
                    proj->active = false;
                    game->globalMorale += 2.0f;
                    if (pol->health <= 0.0f) {
                        pol->alive = false;
                    }
                    break;
                }
            }
        }
    }

    for (int i = 0; i < POLICE_COUNT; i++) {
        if (police_cooldown[i] > 0) police_cooldown[i] -= GetFrameTime();
    }

    if (CheckWinCondition(game))
    {
        game->menuState = MENU_WIN;
    }
    else if (CheckLoseCondition(game))
    {
        game->menuState = MENU_LOSE;
    }
}

typedef struct {
    float y;
    int type; // 1 = protester, 2 = police
    int index;
} DrawEntity;

void DrawGame(GameState *game, Font pixelFont, Texture2D *textures)
{
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    DrawEntity drawList[MAX_PROTESTERS + MAX_POLICE];
    int drawCount = 0;

    for (int i = 0; i < MAX_PROTESTERS; i++) {
        if (game->protesters[i].alive) {
            drawList[drawCount].y = game->protesters[i].pos.y;
            drawList[drawCount].type = 1;
            drawList[drawCount].index = i;
            drawCount++;
        }
    }

    for (int i = 0; i < MAX_POLICE; i++) {
        if (game->police[i].alive) {
            drawList[drawCount].y = game->police[i].pos.y;
            drawList[drawCount].type = 2;
            drawList[drawCount].index = i;
            drawCount++;
        }
    }

    if (textures[6].id != 0) {
        Rectangle src = {0, 0, (float)textures[6].width, (float)textures[6].height};
        Rectangle dest = {0, 0, (float)screenWidth, (float)screenHeight};
        DrawTexturePro(textures[6], src, dest, (Vector2){0, 0}, 0.0f, WHITE);
    }

    DrawHelicopter(game, textures[9]);

    for (int i = 1; i < drawCount; i++) {
        DrawEntity key = drawList[i];
        int j = i - 1;
        while (j >= 0 && drawList[j].y > key.y) {
            drawList[j + 1] = drawList[j];
            j = j - 1;
        }
        drawList[j + 1] = key;
    }

    for (int i = 0; i < drawCount; i++) {
        DrawEntity entity = drawList[i];
        switch (entity.type) {
        case 1: {
            Protester *p = &game->protesters[entity.index];
            Texture2D anim_sprite = (p->state == RIOT || p->state == FLEE) ? p->run_sprites[p->anim_frame] : p->sprites[p->anim_frame];
            Vector2 pos = (Vector2){p->pos.x - anim_sprite.width/2, p->pos.y - anim_sprite.height/2};
            Color tint = WHITE;
            switch (p->state) {
                case CHANT: tint = SKYBLUE; break;
                case RIOT: tint = ORANGE; break;
                case FLEE: tint = PINK; break;
                case IDLE:
                default: tint = WHITE; break;
            }
            if (anim_sprite.id != 0) {
                DrawTextureV(anim_sprite, pos, tint);
            } else {
                DrawCircleV(p->pos, 8, tint);
            }
            if (game->selected[entity.index]) {
                DrawRectangleLines((int)pos.x, (int)pos.y, 39, 69, BLUE);
                DrawCircleLines((int)p->pos.x, (int)p->pos.y, 18, BLUE);
            }
            if (p->state == CHANT) {
                const char *slogans[] = {
                    "Tumi ke ami ke",
                    "Quota na medha",
                    "Medha!!",
                    "Odhikar chai",
                    "Nyay chai"};
                int sloganIdx = entity.index % 5;
                Vector2 bubblePos = {p->pos.x - 30, p->pos.y - 35};
                int textWidth = MeasureText(slogans[sloganIdx], 12);
                DrawRectangle((int)bubblePos.x, (int)bubblePos.y, textWidth + 10, 20, WHITE);
                DrawRectangleLines((int)bubblePos.x, (int)bubblePos.y, textWidth + 10, 20, DARKBLUE);
                DrawTextEx(pixelFont, slogans[sloganIdx], (Vector2){bubblePos.x + 5, bubblePos.y + 4}, 12, 1, DARKBLUE);
            }
            if (game->selected[entity.index]) {
                DrawRectangle((int)(p->pos.x - 8), (int)(p->pos.y + 10), 16, 3, RED);
                DrawRectangle((int)(p->pos.x - 8), (int)(p->pos.y + 10), (int)(16 * p->morale / 100.0f), 3, GREEN);
            }
            break;
        }
        case 2: {
            Police *p = &game->police[entity.index];
            Texture2D anim_sprite = (p->state == INTERVENE || p->state == DEPLOY) ? p->run_sprites[p->anim_frame] : p->sprites[p->anim_frame];
            Vector2 pos = (Vector2){p->pos.x - anim_sprite.width/2, p->pos.y - anim_sprite.height/2};
            Color tint = WHITE;
            if (p->state == INTERVENE || p->state == DEPLOY) {
                tint = RED;
            } else if (p->state == PATROL) {
                tint = LIGHTGRAY;
            }
            if (anim_sprite.id != 0) {
                DrawTextureV(anim_sprite, pos, tint);
            } else {
                DrawCircleV(p->pos, 8, tint);
            }
            if (p->state == DEPLOY) {
                DrawCircleLines((int)p->pos.x, (int)p->pos.y, 20, YELLOW);
            } else if (p->state == INTERVENE) {
                DrawCircleLines((int)p->pos.x, (int)p->pos.y, 15, RED);
            }
            break;
        }
        }
    }

    for (int g = 0; g < MAX_GAS; g++) {
        if (game->gas[g].active) {
            DrawCircleV(game->gas[g].pos, game->gas[g].radius, Fade(YELLOW, 0.5f));
        }
    }

    for (int i = 0; i < MAX_PROJECTILES; i++) {
        Projectile *proj = &game->projectiles[i];
        if (proj->active) {
            if (proj->type == STONE) {
                DrawCircleV(proj->pos, 3, GRAY);
            } else if (proj->type == BULLET) {
                DrawCircleV(proj->pos, 2, RED);
            } else if (proj->type == HELICOPTER_BULLET) {
                DrawCircleV(proj->pos, 3, ORANGE);
            }
        }
    }

    if (textures[7].id != 0) {
        DrawTexture(textures[7], 0, 0, WHITE);
    }

    if (textures[8].id != 0) {
        Rectangle src = {0, 0, (float)textures[8].width, (float)textures[8].height};
        Rectangle dest = {0, 0, 1600, 900};
        DrawTexturePro(textures[8], src, dest, (Vector2){0, 0}, 0.0f, WHITE);
    }

    DrawUI(game, pixelFont, textures);
}

void DrawUI(GameState *game, Font pixelFont, Texture2D *textures)
{
    const int screenWidth = 1600;
    const int screenHeight = 900;

    DrawRectangle(0, 0, screenWidth, 110, Fade(BLACK, 0.6f));
    DrawRectangle(20, 20, 300, 25, LIGHTGRAY);
    Color moraleColor = (game->globalMorale > 70) ? GREEN : (game->globalMorale > 30) ? YELLOW : RED;
    DrawRectangle(20, 20, (int)(3 * game->globalMorale), 25, moraleColor);
    DrawRectangleLines(20, 20, 300, 25, WHITE);
    DrawTextEx(pixelFont, TextFormat("Movement Morale: %.1f%%", game->globalMorale),
               (Vector2){330, 22}, 20, 1, WHITE);

    double elapsed = GetTime() - game->gameStartTime;
    int timeLeft = (int)(GAME_DURATION - elapsed);
    if (timeLeft < 0) timeLeft = 0;
    int minutes = timeLeft / 60;
    int seconds = timeLeft % 60;

    DrawTextEx(pixelFont, TextFormat("Time: %02d:%02d", minutes, seconds),
               (Vector2){screenWidth - 200, 20}, 24, 1, WHITE);

    DrawTextEx(pixelFont, TextFormat("Active Protesters: %d", game->protesterCount),
               (Vector2){20, 60}, 18, 1, WHITE);
    DrawTextEx(pixelFont, TextFormat("Arrested: %d", game->protesters_arrested),
               (Vector2){20, 85}, 18, 1, WHITE);

    int advancedProtesters = 0;
    for (int i = 0; i < MAX_PROTESTERS; i++)
    {
        if (game->protesters[i].alive && game->protesters[i].pos.x > 800)
        {
            advancedProtesters++;
        }
    }
    float controlPercentage = game->protesterCount > 0 ? (float)advancedProtesters / game->protesterCount : 0.0f;

    DrawTextEx(pixelFont, "Territory Control:", (Vector2){screenWidth - 300, 60}, 18, 1, WHITE);
    DrawRectangle(screenWidth - 300, 85, 200, 15, LIGHTGRAY);
    DrawRectangle(screenWidth - 300, 85, (int)(200 * controlPercentage), 15, GREEN);
    DrawRectangleLines(screenWidth - 300, 85, 200, 15, WHITE);

    // Debug information
    DrawTextEx(pixelFont, TextFormat("Control: %.1f%%", controlPercentage * 100), (Vector2){screenWidth - 300, 110}, 16, 1, WHITE);
    DrawTextEx(pixelFont, TextFormat("Control Time: %.1f", game->controlStartTime > 0 ? GetTime() - game->controlStartTime : 0), (Vector2){screenWidth - 300, 130}, 16, 1, WHITE);
    DrawTextEx(pixelFont, TextFormat("Police Left: %d", game->policeCount), (Vector2){screenWidth - 300, 150}, 16, 1, WHITE);

    if (game->isSelecting) {
        float minX = fminf(game->selectStart.x, game->selectEnd.x);
        float maxX = fmaxf(game->selectStart.x, game->selectEnd.x);
        float minY = fminf(game->selectStart.y, game->selectEnd.y);
        float maxY = fmaxf(game->selectStart.y, game->selectEnd.y);
        DrawRectangleLines((int)minX, (int)minY, (int)(maxX - minX), (int)(maxY - minY), BLUE);
    }

    if (game->policeSurgeActive) {
        double timeLeft = game->policeSurgeEnd - GetTime();
        if (timeLeft > 0) {
            DrawTextEx(pixelFont, TextFormat("Police Surge: %.1f sec", timeLeft),
                       (Vector2){screenWidth / 2 - 100, 20}, 24, 1, RED);
        }
    }
}

void UnloadGameTextures(GameState *game) {
    for (int i = 0; i < MAX_PROTESTERS; i++) {
        for (int j = 0; j < 2; j++) {
            if (game->protesters[i].sprites[j].id != 0) UnloadTexture(game->protesters[i].sprites[j]);
        }
        for (int j = 0; j < 3; j++) {
            if (game->protesters[i].run_sprites[j].id != 0) UnloadTexture(game->protesters[i].run_sprites[j]);
        }
    }
    for (int i = 0; i < MAX_POLICE; i++) {
        for (int j = 0; j < 2; j++) {
            if (game->police[i].sprites[j].id != 0) UnloadTexture(game->police[i].sprites[j]);
        }
        for (int j = 0; j < 3; j++) {
            if (game->police[i].run_sprites[j].id != 0) UnloadTexture(game->police[i].run_sprites[j]);
        }
    }
}

int main(void)
{
    const int screenWidth = 1600;
    const int screenHeight = 900;
    InitWindow(screenWidth, screenHeight, "A Day In July");
    InitAudioDevice(); // Initialize audio device
    SetTargetFPS(60);

    GameState game;
    InitGame(&game);

    Music bgm = LoadMusicStream("game_bgm.mp3"); // Load background music
    SetMusicVolume(bgm, 0.5f); // Set volume to 50%

    Font pixelFont = LoadFont("pixel_font.ttf");
    if (pixelFont.texture.id == 0) {
        pixelFont = GetFontDefault();
    }

    Texture2D textures[10] = {0};
    const char *textureFiles[] = {
        "protester.png", "police.png", "bus.png", "car.png", "tear_gas.png",
        "agitator.png", "bg2.png", "dhaka_skyline.png", "bottom_building.png", "helicopter.png"
    };

    for (int i = 0; i < 10; i++) {
        textures[i] = LoadTexture(textureFiles[i]);
        if (textures[i].id != 0) {
            SetTextureFilter(textures[i], TEXTURE_FILTER_POINT);
        }
    }

    while (!WindowShouldClose()) {
        UpdateMusicStream(bgm); // Update music stream
        switch (game.menuState) {
            case MENU_START:
                StopMusicStream(bgm); // Ensure music is stopped in menu
                if (IsKeyPressed(KEY_ENTER)) {
                    game.menuState = MENU_PLAY;
                    PlayMusicStream(bgm); // Start music when entering play state
                }
                if (IsKeyPressed(KEY_T)) game.menuState = MENU_TUTORIAL;
                break;
            case MENU_TUTORIAL:
                StopMusicStream(bgm); // Stop music in tutorial
                if (IsKeyPressed(KEY_ENTER)) game.menuState = MENU_START;
                break;
            case MENU_PAUSE:
                PauseMusicStream(bgm); // Pause music during pause
                if (IsKeyPressed(KEY_ENTER)) {
                    game.menuState = MENU_PLAY;
                    ResumeMusicStream(bgm); // Resume music when unpausing
                }
                break;
            case MENU_WIN:
            case MENU_LOSE:
                StopMusicStream(bgm); // Stop music on win or lose
                if (IsKeyPressed(KEY_ENTER)) {
                    UnloadGameTextures(&game);
                    InitGame(&game);
                    game.menuState = MENU_START;
                }
                break;
            default:
                if (IsKeyPressed(KEY_P)) {
                    game.menuState = MENU_PAUSE;
                    PauseMusicStream(bgm); // Pause music when pausing
                }
                UpdateGame(&game);
                break;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        if (game.menuState == MENU_START) {
            DrawTextEx(pixelFont, "July Uprising Simulator", (Vector2){screenWidth / 2 - 300, 200}, 64, 2, DARKBLUE);
            DrawTextEx(pixelFont, "Quota Movement", (Vector2){screenWidth / 2 - 150, 280}, 36, 2, DARKGRAY);
            DrawTextEx(pixelFont, "Press ENTER to Start", (Vector2){screenWidth / 2 - 180, 400}, 32, 2, DARKGRAY);
            DrawTextEx(pixelFont, "Press T for Tutorial", (Vector2){screenWidth / 2 - 160, 450}, 24, 2, GRAY);
        } else if (game.menuState == MENU_TUTORIAL) {
            DrawTextEx(pixelFont, "Tutorial", (Vector2){screenWidth / 2 - 100, 100}, 48, 2, DARKBLUE);
            DrawTextEx(pixelFont, "Left Click + Drag: Select protesters", (Vector2){50, 200}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "Right Click: Command selected protesters & throw stones (any state)", (Vector2){50, 240}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "T: Throw stones at nearest police (selected protesters)", (Vector2){50, 280}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "States: Idle -> Chant -> Riot -> Idle (cycle with right click)", (Vector2){50, 320}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "A: Select all protesters", (Vector2){50, 360}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "SPACE: Emergency retreat", (Vector2){50, 400}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "P: Pause game (pauses background music)", (Vector2){50, 440}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "Helicopter: Appears from right, attacks, then leaves left side", (Vector2){50, 480}, 24, 2, ORANGE);
            DrawTextEx(pixelFont, "Background Music: Plays during game, stops on win/lose", (Vector2){50, 520}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "Goal: Control territory (>50%) with high morale (>60) or defeat all police", (Vector2){50, 560}, 24, 2, GREEN);
            DrawTextEx(pixelFont, "Press ENTER to return", (Vector2){screenWidth / 2 - 180, 600}, 32, 2, DARKGRAY);
        } else if (game.menuState == MENU_PAUSE) {
            DrawTextEx(pixelFont, "Paused", (Vector2){screenWidth / 2 - 100, 200}, 64, 2, DARKBLUE);
            DrawTextEx(pixelFont, "Press ENTER to Resume", (Vector2){screenWidth / 2 - 200, 400}, 32, 2, DARKGRAY);
        } else if (game.menuState == MENU_WIN) {
            DrawTextEx(pixelFont, "Victory!", (Vector2){screenWidth / 2 - 150, 200}, 64, 2, GREEN);
            DrawTextEx(pixelFont, "The movement succeeded!", (Vector2){screenWidth / 2 - 220, 280}, 36, 2, DARKGREEN);
            DrawTextEx(pixelFont, TextFormat("Protesters arrested: %d", game.protesters_arrested), (Vector2){screenWidth / 2 - 200, 350}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, TextFormat("Max morale reached: %.1f", game.max_morale_reached), (Vector2){screenWidth / 2 - 200, 380}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "Press ENTER to Restart", (Vector2){screenWidth / 2 - 200, 450}, 32, 2, DARKGRAY);
        } else if (game.menuState == MENU_LOSE) {
            DrawTextEx(pixelFont, "Movement Suppressed", (Vector2){screenWidth / 2 - 250, 200}, 54, 2, RED);
            DrawTextEx(pixelFont, TextFormat("Protesters arrested: %d", game.protesters_arrested), (Vector2){screenWidth / 2 - 200, 300}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, TextFormat("Final morale: %.1f", game.globalMorale), (Vector2){screenWidth / 2 - 200, 330}, 24, 2, DARKGRAY);
            DrawTextEx(pixelFont, "Press ENTER to Restart", (Vector2){screenWidth / 2 - 200, 400}, 32, 2, DARKGRAY);
        } else {
            DrawGame(&game, pixelFont, textures);
        }

        EndDrawing();
    }

    UnloadGameTextures(&game);
    for (int i = 0; i < 10; i++) {
        if (textures[i].id != 0) UnloadTexture(textures[i]);
    }
    if (pixelFont.texture.id != 0) UnloadFont(pixelFont);
    if (bgm.stream.buffer != NULL) UnloadMusicStream(bgm); // Unload music
    CloseAudioDevice(); // Close audio device
    CloseWindow();
    return 0;
} 