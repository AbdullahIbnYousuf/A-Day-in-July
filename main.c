#include <raylib.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <raymath.h>
#include <stdio.h>

#define MAX_PROTESTERS 120
#define MAX_POLICE 100
#define MAX_PROJECTILES 1000
#define MAX_BARRIERS 20
#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 720
#define ENTITY_SPEED 150.0f
#define ADVANCE_SPEED 75.0f
#define STONE_SPEED 600.0f
#define BULLET_SPEED 1400.0f
#define STONE_RANGE 600.0f
#define BULLET_RANGE 700.0f
#define PROTESTER_BULLET_HEALTH 4
#define PROTESTER_MELEE_HEALTH 7
#define POLICE_HEALTH 5
#define HELICOPTER_HEALTH 20
#define HELICOPTER_COOLDOWN 0.8f
#define HELICOPTER_RANGE 800.0f
#define HELICOPTER_SPEED 120.0f
#define SPREAD_ANGLE 15.0f
#define CAR_BARRIER_HEALTH 600
#define CONCRETE_BARRIER_HEALTH 1200
#define PROTESTER_COUNTDOWN 1.0f
#define POLICE_SHOOTER_COUNTDOWN 0.4f
#define POLICE_MELEE_COUNTDOWN 1.0f
#define MELEE_RANGE 30.0f
#define COVER_WIDTH 10.0f
#define COVER_HEIGHT 80.0f
#define EXPLOSION_RANGE 50.0f
#define PROTESTER_TERRITORY_X 1000.0f
#define POLICE_TERRITORY_X 280.0f
#define TERRITORY_RANGE 200.0f
#define WIN_HOLD_TIME 20.0f
#define FLOCKING_RADIUS 50.0f
#define FLOCKING_WEIGHT 0.15f
#define DENSITY_RADIUS 200.0f
#define BARRIER_AVOIDANCE_RANGE 60.0f
#define BARRIER_AVOIDANCE_FORCE 100.0f
#define ANIMATION_DURATION 0.2f
#define RETREAT_HEALTH_THRESHOLD 0.25f
#define COVER_CYCLE_DURATION 5.0f
#define FLANKING_OFFSET 50.0f
#define MORALE_PENALTY_DURATION 3.0f
#define MORALE_PENALTY_FACTOR 0.7f
#define DYING_DURATION 2.0f

typedef enum { PROTESTER, POLICE } EntityType;
typedef enum { IDLE, MOVING, ATTACKING, RETREATING, TAKING_COVER, DYING } AIState;
typedef enum { SHOOTER, MELEE, HELICOPTER } PoliceType;
typedef enum { CAR, CONCRETE } BarrierType;
typedef enum { START, PLAYING, PROTESTER_WIN, POLICE_WIN } GameState;

typedef struct {
    Vector2 position;
    Vector2 velocity;
    EntityType type;
    PoliceType police_type;
    int bullet_health;
    int melee_health;
    bool active;
    AIState ai_state;
    float cooldown;
    int target_id;
    bool is_player_controlled;
    float animation_timer;
    float morale_boost;
    bool is_taking_cover;
    int cover_barrier_id;
    float morale_penalty_timer;
    Vector2 wander_target;
    float wander_timer;
} Entity;

typedef struct {
    Vector2 position;
    Vector2 velocity;
    EntityType type;
    bool active;
    float distance_traveled;
} Projectile;

typedef struct {
    Vector2 start;
    Vector2 end;
    BarrierType type;
    bool active;
} Barrier;

typedef struct {
    GameState state;
    float territory_hold_timer;
    float protester_morale;
    float police_morale;
    float cover_cycle_timer;
    int last_police_count;
    float police_defeat_timer;
    int cover_cycle_phase;
} Game;

Entity protesters[MAX_PROTESTERS] = {0};
Entity police[MAX_POLICE] = {0};
Projectile projectiles[MAX_PROJECTILES] = {0};
Barrier barriers[MAX_BARRIERS] = {0};
Game game = {0};

int selected_entity = -1;
EntityType selected_type = PROTESTER;

float distance(Vector2 p1, Vector2 p2) {
    return sqrtf((p1.x - p2.x) * (p1.x - p2.x) + (p1.y - p2.y) * (p1.y - p2.y));
}

bool point_near_line(Vector2 point, Vector2 line_start, Vector2 line_end, float threshold) {
    float line_length = distance(line_start, line_end);
    if (line_length == 0) return false;
    float t = ((point.x - line_start.x) * (line_end.x - line_start.x) + 
               (point.y - line_start.y) * (line_end.y - line_start.y)) / (line_length * line_length);
    t = fmaxf(0, fminf(1, t));
    Vector2 projection = {line_start.x + t * (line_end.x - line_start.x),
                          line_start.y + t * (line_end.y - line_start.y)};
    return distance(point, projection) < threshold;
}

bool has_clear_shot(Vector2 start, Vector2 target) {
    for (int i = 0; i < MAX_BARRIERS; i++) {
        if (barriers[i].active) {
            Vector2 barrier_center = {barriers[i].start.x, 
                                     (barriers[i].start.y + barriers[i].end.y) / 2};
            if (target.x > start.x && barrier_center.x > start.x && barrier_center.x < target.x) {
                float t = (barriers[i].start.x - start.x) / (target.x - start.x);
                if (t >= 0 && t <= 1) {
                    float y_intersect = start.y + t * (target.y - start.y);
                    if (y_intersect >= barriers[i].start.y - COVER_WIDTH / 2 &&
                        y_intersect <= barriers[i].end.y + COVER_WIDTH / 2) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

void init_entity(Entity *entity, Vector2 pos, EntityType type, PoliceType police_type) {
    entity->position = pos;
    entity->velocity = (Vector2){0, 0};
    entity->type = type;
    entity->police_type = police_type;
    if (type == POLICE && police_type == HELICOPTER) {
        entity->bullet_health = HELICOPTER_HEALTH;
    } else {
        entity->bullet_health = (type == PROTESTER) ? PROTESTER_BULLET_HEALTH : POLICE_HEALTH;
    }
    entity->melee_health = (type == PROTESTER) ? PROTESTER_MELEE_HEALTH : 0;
    entity->active = true;
    entity->ai_state = ATTACKING;
    entity->cooldown = 0;
    entity->target_id = -1;
    entity->is_player_controlled = false;
    entity->animation_timer = 0;
    entity->morale_boost = 1.0f;
    entity->is_taking_cover = false;
    entity->cover_barrier_id = -1;
    entity->morale_penalty_timer = 0.0f;
    entity->wander_target = (Vector2){0, 0};
    entity->wander_timer = 0.0f;
}

void init_barrier(Barrier *barrier, Vector2 pos, BarrierType type) {
    barrier->start.x = pos.x;
    barrier->start.y = pos.y - COVER_HEIGHT / 2;
    barrier->end.x = pos.x;
    barrier->end.y = pos.y + COVER_HEIGHT / 2;
    barrier->type = type;
    barrier->active = true;
}

void init_game() {
    srand((unsigned int)time(NULL));
    game.state = START;
    game.territory_hold_timer = 0.0f;
    game.protester_morale = 1.0f;
    game.police_morale = 1.0f;
    game.cover_cycle_timer = 0.0f;
    game.last_police_count = 0;
    game.police_defeat_timer = 0.0f;
    game.cover_cycle_phase = 0;
    for (int i = 0; i < 80; i++) {
        Vector2 pos = {100 + (rand() % 600), 50 + (rand() % 620)};
        init_entity(&protesters[i], pos, PROTESTER, SHOOTER);
    }
    for (int i = 0; i < 59; i++) {
        Vector2 pos = {680 + (rand() % 500), 50 + (rand() % 620)};
        init_entity(&police[i], pos, POLICE, (rand() % 2 == 0) ? SHOOTER : MELEE);
    }
    Vector2 heli_pos = {900, 360};
    init_entity(&police[59], heli_pos, POLICE, HELICOPTER);
    game.last_police_count = 60;
    for (int i = 0; i < MAX_BARRIERS; i++) {
        barriers[i].active = false;
    }
    int barrier_index = 0;
    int num_barriers = 12;
    float x_start = 400.0f;
    float x_end = 800.0f;
    float x_spacing = (x_end - x_start) / (num_barriers - 1);
    for (int i = 0; i < num_barriers && barrier_index < MAX_BARRIERS; i++) {
        float x_pos = x_start + i * x_spacing + (float)(rand() % 50 - 25);
        float y_pos = 100.0f + (rand() % (int)(SCREEN_HEIGHT - 200));
        Vector2 pos = {x_pos, y_pos};
        init_barrier(&barriers[barrier_index], pos, (rand() % 2 == 0) ? CAR : CONCRETE);
        barrier_index++;
    }
}

void spawn_entity(Entity *entities, int max_entities, Vector2 pos, EntityType type, PoliceType police_type) {
    for (int i = 0; i < max_entities; i++) {
        if (!entities[i].active) {
            init_entity(&entities[i], pos, type, police_type);
            break;
        }
    }
}

int find_nearest_barrier(Vector2 pos) {
    float closest_dist = 100.0f;
    int closest_id = -1;
    for (int i = 0; i < MAX_BARRIERS; i++) {
        if (barriers[i].active && barriers[i].start.x < pos.x) {
            float dist = point_near_line(pos, barriers[i].start, barriers[i].end, COVER_WIDTH) ? 
                         distance(pos, (Vector2){barriers[i].start.x, pos.y}) : 100.0f;
            if (dist < closest_dist) {
                closest_dist = dist;
                closest_id = i;
            }
        }
    }
    return closest_id;
}

void fire_projectile(Vector2 pos, Vector2 dir, EntityType type, Entity *entity) {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (!projectiles[i].active) {
            projectiles[i].position = pos;
            projectiles[i].velocity = Vector2Scale(Vector2Normalize(dir), 
                (type == PROTESTER ? STONE_SPEED : BULLET_SPEED));
            projectiles[i].type = type;
            projectiles[i].active = true;
            projectiles[i].distance_traveled = 0.0f;
            entity->animation_timer = ANIMATION_DURATION;
            break;
        }
    }
}

Vector2 compute_flocking(Entity *entity, int index, Entity *entities, int max_entities) {
    Vector2 alignment = {0, 0};
    Vector2 cohesion = {0, 0};
    int count = 0;
    for (int i = 0; i < max_entities; i++) {
        if (i != index && entities[i].active && entities[i].ai_state != RETREATING && !entities[i].is_taking_cover && entities[i].ai_state != DYING) {
            float dist = distance(entity->position, entities[i].position);
            if (dist < FLOCKING_RADIUS && dist > 0) {
                alignment = Vector2Add(alignment, entities[i].velocity);
                cohesion = Vector2Add(cohesion, entities[i].position);
                count++;
            }
        }
    }
    if (count > 0) {
        alignment = Vector2Scale(alignment, 1.0f / count);
        cohesion = Vector2Scale(cohesion, 1.0f / count);
        cohesion = Vector2Subtract(cohesion, entity->position);
        alignment = Vector2Normalize(alignment);
        cohesion = Vector2Normalize(cohesion);
        Vector2 flocking = Vector2Add(alignment, cohesion);
        flocking = Vector2Scale(flocking, FLOCKING_WEIGHT * ENTITY_SPEED);
        return flocking;
    }
    return (Vector2){0, 0};
}

Vector2 avoid_collisions(Entity *entity, int index, Entity *entities, int max_entities) {
    Vector2 avoidance = {0, 0};
    int count = 0;
    for (int i = 0; i < max_entities; i++) {
        if (i != index && entities[i].active) {
            float dist = distance(entity->position, entities[i].position);
            if (dist < 20.0f && dist > 0) {
                Vector2 dir = Vector2Subtract(entity->position, entities[i].position);
                avoidance = Vector2Add(avoidance, Vector2Scale(dir, 1.0f / dist));
                count++;
            }
        }
    }
    for (int i = 0; i < MAX_BARRIERS; i++) {
        if (barriers[i].active) {
            float dist = point_near_line(entity->position, barriers[i].start, barriers[i].end, BARRIER_AVOIDANCE_RANGE) ?
                         distance(entity->position, (Vector2){barriers[i].start.x, entity->position.y}) : 10000.0f;
            if (dist < BARRIER_AVOIDANCE_RANGE && dist > 0) {
                Vector2 dir = Vector2Subtract(entity->position, (Vector2){barriers[i].start.x, entity->position.y});
                avoidance = Vector2Add(avoidance, Vector2Scale(dir, BARRIER_AVOIDANCE_FORCE / dist));
                count++;
            }
        }
    }
    if (count > 0) {
        avoidance = Vector2Scale(avoidance, 1.0f / count);
        avoidance = Vector2Normalize(avoidance);
        avoidance = Vector2Scale(avoidance, ENTITY_SPEED);
    }
    return avoidance;
}

Vector2 find_densest_enemy_area(Entity *entity, EntityType type) {
    Entity *enemies = (type == PROTESTER) ? police : protesters;
    int max_enemies = (type == PROTESTER) ? MAX_POLICE : MAX_PROTESTERS;
    Vector2 center = {0, 0};
    float max_score = 0;
    for (int i = 0; i < max_enemies; i++) {
        if (enemies[i].active && enemies[i].ai_state != DYING) {
            int count = 0;
            Vector2 sum = {0, 0};
            for (int j = 0; j < max_enemies; j++) {
                if (enemies[j].active && enemies[j].ai_state != DYING && distance(enemies[i].position, enemies[j].position) < DENSITY_RADIUS) {
                    sum = Vector2Add(sum, enemies[j].position);
                    count++;
                }
            }
            if (count > 0) {
                Vector2 avg_pos = Vector2Scale(sum, 1.0f / count);
                float score = count * (type == PROTESTER ? 
                    (1.0f + 0.5f * (SCREEN_WIDTH - avg_pos.x) / SCREEN_WIDTH) : 1.0f);
                if (score > max_score) {
                    max_score = score;
                    center = avg_pos;
                }
            }
        }
    }
    return max_score > 0 ? center : (Vector2){PROTESTER_TERRITORY_X, entity->position.y};
}

void find_closest_enemy(Entity *entity, EntityType type, float *closest_dist, int *closest_enemy, Vector2 *target_pos) {
    Entity *enemies = (type == PROTESTER) ? police : protesters;
    int max_enemies = (type == PROTESTER) ? MAX_POLICE : MAX_PROTESTERS;
    *closest_dist = 10000.0f;
    *closest_enemy = -1;
    for (int i = 0; i < max_enemies; i++) {
        if (enemies[i].active && enemies[i].ai_state != DYING && (!type == PROTESTER || !enemies[i].is_taking_cover)) {
            float dist = distance(entity->position, enemies[i].position);
            float score = dist;
            if (type == PROTESTER) {
                score *= (1.0f + 0.5f * (SCREEN_WIDTH - enemies[i].position.x) / SCREEN_WIDTH);
            }
            if (score < *closest_dist) {
                *closest_dist = score;
                *closest_enemy = i;
                *target_pos = enemies[i].position;
            }
        }
    }
}

void update_morale() {
    int active_protesters = 0, active_police = 0;
    for (int i = 0; i < MAX_PROTESTERS; i++) if (protesters[i].active) active_protesters++;
    for (int i = 0; i < MAX_POLICE; i++) if (police[i].active && police[i].ai_state != DYING) active_police++;
    if (game.last_police_count - active_police > 5 && game.police_defeat_timer <= 0) {
        game.police_defeat_timer = MORALE_PENALTY_DURATION;
    }
    game.last_police_count = active_police;
    float total = active_protesters + active_police;
    game.protester_morale = total > 0 ? (float)active_protesters / total : 0.5f;
    game.police_morale = total > 0 ? (float)active_police / total : 0.5f;
    for (int i = 0; i < MAX_PROTESTERS; i++) {
        if (protesters[i].active) {
            protesters[i].morale_boost = 1.0f + 0.2f * game.protester_morale;
        }
    }
    for (int i = 0; i < MAX_POLICE; i++) {
        if (police[i].active) {
            float penalty = (game.police_defeat_timer > 0) ? MORALE_PENALTY_FACTOR : 1.0f;
            police[i].morale_boost = (1.0f + 0.2f * game.police_morale) * penalty;
            if (police[i].morale_penalty_timer > 0) {
                police[i].morale_penalty_timer -= GetFrameTime();
            }
        }
    }
    if (game.police_defeat_timer > 0) {
        game.police_defeat_timer -= GetFrameTime();
    }
}

void update_protester_cover() {
    game.cover_cycle_timer += GetFrameTime();
    if (game.cover_cycle_timer >= COVER_CYCLE_DURATION) {
        game.cover_cycle_timer = 0.0f;
        int active_protesters = 0;
        for (int i = 0; i < MAX_PROTESTERS; i++) {
            if (protesters[i].active && !protesters[i].is_player_controlled) active_protesters++;
        }
        int cover_count;
        switch (game.cover_cycle_phase) {
            case 0: cover_count = 8; break;
            case 1: cover_count = 13; break;
            case 2: cover_count = 3; break;
            default: cover_count = (active_protesters > 0) ? (rand() % (active_protesters > 15 ? 15 : active_protesters)) + 3 : 0; break;
        }
        game.cover_cycle_phase = (game.cover_cycle_phase + 1) % 4;
        for (int i = 0; i < MAX_PROTESTERS; i++) {
            if (protesters[i].active && !protesters[i].is_player_controlled) {
                protesters[i].is_taking_cover = false;
                protesters[i].cover_barrier_id = -1;
            }
        }
        for (int i = 0; i < cover_count; i++) {
            int index = rand() % MAX_PROTESTERS;
            int attempts = 0;
            while (attempts < MAX_PROTESTERS && 
                   (!protesters[index].active || protesters[index].is_player_controlled || protesters[index].is_taking_cover)) {
                index = (index + 1) % MAX_PROTESTERS;
                attempts++;
            }
            if (attempts < MAX_PROTESTERS) {
                protesters[index].is_taking_cover = true;
                protesters[index].cover_barrier_id = find_nearest_barrier(protesters[index].position);
                if (protesters[index].cover_barrier_id != -1) {
                    protesters[index].ai_state = TAKING_COVER;
                }
            }
        }
    }
}

void update_protester_combat(Entity *entity, float closest_dist, int closest_enemy, Vector2 target_pos) {
    entity->target_id = closest_enemy;
    if (closest_dist < STONE_RANGE && entity->cooldown <= 0 && has_clear_shot(entity->position, target_pos)) {
        Vector2 dir = {target_pos.x - entity->position.x, target_pos.y - entity->position.y};
        fire_projectile(entity->position, dir, PROTESTER, entity);
        entity->cooldown = PROTESTER_COUNTDOWN;
    }
}

void update_police_combat(Entity *entity, float closest_dist, int closest_enemy, Vector2 target_pos) {
    entity->target_id = closest_enemy;
    Vector2 dir = Vector2Subtract(target_pos, entity->position);
    dir = Vector2Normalize(dir);
    if (entity->police_type == SHOOTER) {
        if (closest_dist < BULLET_RANGE) {
            entity->ai_state = ATTACKING;
            if (entity->cooldown <= 0) {
                fire_projectile(entity->position, dir, POLICE, entity);
                entity->cooldown = POLICE_SHOOTER_COUNTDOWN;
            }
            entity->velocity = (Vector2){0, 0};
        } else {
            entity->ai_state = MOVING;
            entity->velocity = (Vector2){0, 0};
        }
    } else {
        if (closest_dist <= MELEE_RANGE) {
            entity->ai_state = ATTACKING;
            if (entity->cooldown <= 0) {
                Entity *enemies = protesters;
                enemies[closest_enemy].melee_health -= 2;
                enemies[closest_enemy].animation_timer = ANIMATION_DURATION;
                if (enemies[closest_enemy].melee_health <= 0 || enemies[closest_enemy].bullet_health <= 0) {
                    enemies[closest_enemy].active = false;
                }
                entity->cooldown = POLICE_MELEE_COUNTDOWN;
            }
            entity->velocity = (Vector2){0, 0};
        } else {
            entity->ai_state = MOVING;
            Vector2 dense_area = find_densest_enemy_area(entity, POLICE);
            float y_offset = (rand() % 2 == 0 ? 1 : -1) * FLANKING_OFFSET;
            Vector2 flank_pos = {dense_area.x, dense_area.y + y_offset};
            Vector2 dir_flank = Vector2Subtract(flank_pos, entity->position);
            dir_flank = Vector2Normalize(dir_flank);
            entity->velocity = Vector2Scale(dir_flank, ENTITY_SPEED * entity->morale_boost);
        }
    }
}

void update_protester_ai(Entity *entity, int index) {
    if (!entity->active || entity->is_player_controlled) return;
    if (entity->cooldown > 0) entity->cooldown -= GetFrameTime();
    if (entity->animation_timer > 0) entity->animation_timer -= GetFrameTime();
    float closest_dist;
    int closest_enemy;
    Vector2 target_pos;
    find_closest_enemy(entity, PROTESTER, &closest_dist, &closest_enemy, &target_pos);
    Vector2 police_territory_target = {PROTESTER_TERRITORY_X, entity->position.y};
    if (entity->is_taking_cover && entity->cover_barrier_id != -1 && barriers[entity->cover_barrier_id].active) {
        entity->ai_state = TAKING_COVER;
        Vector2 barrier_pos = {barriers[entity->cover_barrier_id].start.x, 
                       (barriers[entity->cover_barrier_id].start.y + barriers[entity->cover_barrier_id].end.y) / 2};
        float dist_to_cover = distance(entity->position, barrier_pos);
        if (dist_to_cover > 5.0f) {
            Vector2 dir = Vector2Subtract(barrier_pos, entity->position);
            dir = Vector2Normalize(dir);
            entity->velocity = Vector2Scale(dir, ENTITY_SPEED * entity->morale_boost);
        } else {
            entity->position = barrier_pos;
            entity->velocity = (Vector2){0, 0};
            update_protester_combat(entity, closest_dist, closest_enemy, target_pos);
        }
    } else {
        float health_ratio = (float)entity->bullet_health / PROTESTER_BULLET_HEALTH;
        Vector2 center_dir = {0, SCREEN_HEIGHT / 2 - entity->position.y};
        center_dir = Vector2Normalize(center_dir);
        center_dir = Vector2Scale(center_dir, ENTITY_SPEED * 0.05f * entity->morale_boost);
        Vector2 advance_dir = Vector2Subtract(police_territory_target, entity->position);
        advance_dir = Vector2Normalize(advance_dir);
        advance_dir = Vector2Scale(advance_dir, ENTITY_SPEED * 0.8f * entity->morale_boost);
        if (health_ratio < RETREAT_HEALTH_THRESHOLD && closest_enemy != -1) {
            entity->ai_state = RETREATING;
            Vector2 dir = Vector2Subtract(entity->position, target_pos);
            dir = Vector2Normalize(dir);
            entity->velocity = Vector2Scale(dir, ENTITY_SPEED * entity->morale_boost);
            entity->velocity = Vector2Add(entity->velocity, center_dir);
        } else if (closest_enemy != -1 && closest_dist < STONE_RANGE) {
            update_protester_combat(entity, closest_dist, closest_enemy, target_pos);
            entity->ai_state = ATTACKING;
            entity->velocity = Vector2Add(advance_dir, center_dir);
        } else {
            entity->ai_state = MOVING;
            Vector2 dense_area = find_densest_enemy_area(entity, PROTESTER);
            Vector2 dir_dense = Vector2Subtract(dense_area, entity->position);
            dir_dense = Vector2Normalize(dir_dense);
            entity->velocity = Vector2Scale(dir_dense, ENTITY_SPEED * 0.2f * entity->morale_boost);
            entity->velocity = Vector2Add(entity->velocity, advance_dir);
            entity->velocity = Vector2Add(entity->velocity, center_dir);
        }
    }
    Vector2 avoidance = avoid_collisions(entity, index, protesters, MAX_PROTESTERS);
    Vector2 flocking = compute_flocking(entity, index, protesters, MAX_PROTESTERS);
    entity->velocity = Vector2Add(entity->velocity, avoidance);
    entity->velocity = Vector2Add(entity->velocity, Vector2Scale(flocking, 0.3f));
    Vector2 prev_velocity = entity->velocity;
    Vector2 new_pos = Vector2Add(entity->position, Vector2Scale(entity->velocity, GetFrameTime()));
    bool collision = false;
    for (int i = 0; i < MAX_BARRIERS; i++) {
        if (barriers[i].active && point_near_line(new_pos, barriers[i].start, barriers[i].end, COVER_WIDTH)) {
            collision = true;
            break;
        }
    }
    if (!collision) {
        entity->position = new_pos;
        entity->velocity = Vector2Add(Vector2Scale(entity->velocity, 0.7f), Vector2Scale(prev_velocity, 0.3f));
    } else {
        Vector2 dir = Vector2Normalize(Vector2Subtract(entity->position, (Vector2){new_pos.x, entity->position.y}));
        entity->position = Vector2Add(entity->position, Vector2Scale(dir, 10.0f * GetFrameTime()));
        entity->velocity = Vector2Scale(dir, ENTITY_SPEED * entity->morale_boost);
    }
    if (entity->position.x < COVER_WIDTH) entity->position.x = COVER_WIDTH;
    if (entity->position.x > SCREEN_WIDTH - COVER_WIDTH) entity->position.x = SCREEN_WIDTH - COVER_WIDTH;
    if (entity->position.y < COVER_HEIGHT / 2) entity->position.y = COVER_HEIGHT / 2;
    if (entity->position.y > SCREEN_HEIGHT - COVER_HEIGHT / 2) entity->position.y = SCREEN_HEIGHT - COVER_HEIGHT / 2;
}

void update_police_ai(Entity *entity, int index) {
    if (!entity->active) return;
    if (entity->cooldown > 0) entity->cooldown -= GetFrameTime();
    if (entity->animation_timer > 0) entity->animation_timer -= GetFrameTime();
    if (entity->ai_state == DYING) {
        entity->position.y += 200.0f * GetFrameTime();
        if (entity->animation_timer <= 0) {
            entity->active = false;
        }
        return;
    }
    if (entity->police_type == HELICOPTER) {
        if (entity->wander_timer <= 0 || distance(entity->position, entity->wander_target) < 20.0f) {
            entity->wander_target.x = 600 + (rand() % (SCREEN_WIDTH - 600));
            entity->wander_target.y = 50 + (rand() % (SCREEN_HEIGHT - 100));
            entity->wander_timer = 3.0f + ((float)rand() / RAND_MAX) * 4.0f;
        }
        entity->wander_timer -= GetFrameTime();
        Vector2 dir = Vector2Subtract(entity->wander_target, entity->position);
        if (Vector2Length(dir) > 0) {
            dir = Vector2Normalize(dir);
            entity->velocity = Vector2Scale(dir, HELICOPTER_SPEED * entity->morale_boost);
        } else {
            entity->velocity = (Vector2){0, 0};
        }
        float closest_dist;
        int closest_enemy;
        Vector2 target_pos;
        find_closest_enemy(entity, POLICE, &closest_dist, &closest_enemy, &target_pos);
        if (closest_enemy != -1 && closest_dist < HELICOPTER_RANGE && entity->cooldown <= 0) {
            Vector2 shoot_dir = Vector2Normalize(Vector2Subtract(target_pos, entity->position));
            fire_projectile(entity->position, shoot_dir, POLICE, entity);
            fire_projectile(entity->position, Vector2Rotate(shoot_dir, SPREAD_ANGLE), POLICE, entity);
            fire_projectile(entity->position, Vector2Rotate(shoot_dir, -SPREAD_ANGLE), POLICE, entity);
            entity->cooldown = HELICOPTER_COOLDOWN;
            entity->animation_timer = ANIMATION_DURATION;
        }
        Vector2 new_pos = Vector2Add(entity->position, Vector2Scale(entity->velocity, GetFrameTime()));
        entity->position = new_pos;
        if (entity->position.x < 0) entity->position.x = 0;
        if (entity->position.x > SCREEN_WIDTH) entity->position.x = SCREEN_WIDTH;
        if (entity->position.y < 0) entity->position.y = 0;
        if (entity->position.y > SCREEN_HEIGHT) entity->position.y = SCREEN_HEIGHT;
        return;
    }
    float closest_dist;
    int closest_enemy;
    Vector2 target_pos;
    find_closest_enemy(entity, POLICE, &closest_dist, &closest_enemy, &target_pos);
    if (closest_enemy != -1) {
        update_police_combat(entity, closest_dist, closest_enemy, target_pos);
    } else {
        entity->ai_state = MOVING;
        Vector2 dense_area = find_densest_enemy_area(entity, POLICE);
        Vector2 dir = Vector2Subtract(dense_area, entity->position);
        dir = Vector2Normalize(dir);
        entity->velocity = (entity->police_type == SHOOTER) ? (Vector2){0, 0} : 
                           Vector2Scale(dir, ENTITY_SPEED * entity->morale_boost);
    }
    Vector2 avoidance = avoid_collisions(entity, index, police, MAX_POLICE);
    Vector2 flocking = compute_flocking(entity, index, police, MAX_POLICE);
    entity->velocity = Vector2Add(entity->velocity, avoidance);
    entity->velocity = Vector2Add(entity->velocity, flocking);
    Vector2 prev_velocity = entity->velocity;
    Vector2 new_pos = Vector2Add(entity->position, Vector2Scale(entity->velocity, GetFrameTime()));
    bool collision = false;
    for (int i = 0; i < MAX_BARRIERS; i++) {
        if (barriers[i].active && point_near_line(new_pos, barriers[i].start, barriers[i].end, COVER_WIDTH)) {
            collision = true;
            break;
        }
    }
    if (!collision) {
        entity->position = new_pos;
        entity->velocity = Vector2Add(Vector2Scale(entity->velocity, 0.7f), Vector2Scale(prev_velocity, 0.3f));
    } else {
        Vector2 dir = Vector2Normalize(Vector2Subtract(entity->position, (Vector2){new_pos.x, entity->position.y}));
        entity->position = Vector2Add(entity->position, Vector2Scale(dir, 10.0f * GetFrameTime()));
        entity->velocity = Vector2Scale(dir, ENTITY_SPEED * entity->morale_boost);
    }
    if (entity->position.x < COVER_WIDTH) entity->position.x = COVER_WIDTH;
    if (entity->position.x > SCREEN_WIDTH - COVER_WIDTH) entity->position.x = SCREEN_WIDTH - COVER_WIDTH;
    if (entity->position.y < COVER_HEIGHT / 2) entity->position.y = COVER_HEIGHT / 2;
    if (entity->position.y > SCREEN_HEIGHT - COVER_HEIGHT / 2) entity->position.y = SCREEN_HEIGHT - COVER_HEIGHT / 2;
}

void update_player_controlled(Entity *entity) {
    if (!entity->active) return;
    entity->velocity = (Vector2){0, 0};
    float speed = ENTITY_SPEED * entity->morale_boost;
    if (IsKeyDown(KEY_W)) entity->velocity.y -= speed;
    if (IsKeyDown(KEY_S)) entity->velocity.y += speed;
    if (IsKeyDown(KEY_A)) entity->velocity.x -= speed;
    if (IsKeyDown(KEY_D)) entity->velocity.x += speed;
    Vector2 new_pos = Vector2Add(entity->position, Vector2Scale(entity->velocity, GetFrameTime()));
    bool collision = false;
    for (int i = 0; i < MAX_BARRIERS; i++) {
        if (barriers[i].active && point_near_line(new_pos, barriers[i].start, barriers[i].end, COVER_WIDTH)) {
            collision = true;
            break;
        }
    }
    if (!collision) {
        entity->position = new_pos;
    }
    if (entity->position.x < COVER_WIDTH) entity->position.x = COVER_WIDTH;
    if (entity->position.x > SCREEN_WIDTH - COVER_WIDTH) entity->position.x = SCREEN_WIDTH - COVER_WIDTH;
    if (entity->position.y < COVER_HEIGHT / 2) entity->position.y = COVER_HEIGHT / 2;
    if (entity->position.y > SCREEN_HEIGHT - COVER_HEIGHT / 2) entity->position.y = SCREEN_HEIGHT - COVER_HEIGHT / 2;
    if (entity->cooldown > 0) entity->cooldown -= GetFrameTime();
    if (entity->animation_timer > 0) entity->animation_timer -= GetFrameTime();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && entity->cooldown <= 0) {
        Vector2 mouse_pos = GetMousePosition();
        Vector2 dir = {mouse_pos.x - entity->position.x, mouse_pos.y - entity->position.y};
        fire_projectile(entity->position, dir, entity->type, entity);
        entity->cooldown = PROTESTER_COUNTDOWN;
    }
}

void update_projectiles() {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (projectiles[i].active) {
            projectiles[i].position.x += projectiles[i].velocity.x * GetFrameTime();
            projectiles[i].position.y += projectiles[i].velocity.y * GetFrameTime();
            projectiles[i].distance_traveled += Vector2Length(projectiles[i].velocity) * GetFrameTime();
            if (projectiles[i].distance_traveled > (projectiles[i].type == PROTESTER ? STONE_RANGE : BULLET_RANGE)) {
                projectiles[i].active = false;
                continue;
            }
            for (int j = 0; j < MAX_BARRIERS; j++) {
                if (barriers[j].active && point_near_line(projectiles[i].position, barriers[j].start, barriers[j].end, COVER_WIDTH)) {
                    projectiles[i].active = false;
                    break;
                }
            }
            if (!projectiles[i].active) continue;
            Entity *targets = (projectiles[i].type == PROTESTER) ? police : protesters;
            int max_targets = (projectiles[i].type == PROTESTER) ? MAX_POLICE : MAX_PROTESTERS;
            for (int j = 0; j < max_targets; j++) {
                if (targets[j].active) {
                    float dist = distance(projectiles[i].position, targets[j].position);
                    if (dist < 10.0f) {
                        targets[j].bullet_health -= (projectiles[i].type == PROTESTER) ? 2 : 1;
                        targets[j].animation_timer = ANIMATION_DURATION;
                        if (targets[j].bullet_health <= 0 || (targets[j].type == PROTESTER && targets[j].melee_health <= 0)) {
                            if (targets[j].type == POLICE && targets[j].police_type == HELICOPTER) {
                                targets[j].ai_state = DYING;
                                targets[j].animation_timer = DYING_DURATION;
                                targets[j].velocity = (Vector2){0, 200.0f};
                            } else {
                                targets[j].active = false;
                            }
                        }
                        projectiles[i].active = false;
                        break;
                    }
                }
            }
        }
    }
}

void check_game_conditions() {
    bool helicopter_alive = false;
    for (int i = 0; i < MAX_POLICE; i++) {
        if (police[i].active && police[i].police_type == HELICOPTER && police[i].ai_state != DYING) {
            helicopter_alive = true;
            break;
        }
    }
    if (!helicopter_alive) {
        game.state = PROTESTER_WIN;
        return;
    }
    int active_protesters = 0;
    int protesters_in_territory = 0;
    for (int i = 0; i < MAX_PROTESTERS; i++) {
        if (protesters[i].active) {
            active_protesters++;
            if (distance(protesters[i].position, (Vector2){PROTESTER_TERRITORY_X, protesters[i].position.y}) < TERRITORY_RANGE) {
                protesters_in_territory++;
            }
        }
    }
    int active_police = 0;
    for (int i = 0; i < MAX_POLICE; i++) {
        if (police[i].active && police[i].ai_state != DYING) active_police++;
    }
    if (active_protesters == 0) {
        game.state = POLICE_WIN;
        return;
    }
    if (active_police == 0) {
        game.state = PROTESTER_WIN;
        return;
    }
    if (protesters_in_territory > 0) {
        game.territory_hold_timer += GetFrameTime();
        if (game.territory_hold_timer >= WIN_HOLD_TIME) {
            game.state = PROTESTER_WIN;
        }
    } else {
        game.territory_hold_timer = 0.0f;
    }
}

void draw_health_bar(Vector2 pos, int health, int max_health, Color c) {
    float width = 20.0f;
    float height = 4.0f;
    float health_ratio = (float)health / max_health;
    DrawRectangle(pos.x - width / 2, pos.y - 15, width, height, BLACK);
    DrawRectangle(pos.x - width / 2, pos.y - 15, width * health_ratio, height, c);
}

void draw_background() {
    for (int x = 0; x < SCREEN_WIDTH; x += 50) {
        DrawLine(x, 0, x, SCREEN_HEIGHT, Fade(GRAY, 0.2f));
    }
    for (int y = 0; y < SCREEN_HEIGHT; y += 50) {
        DrawLine(0, y, SCREEN_WIDTH, y, Fade(GRAY, 0.2f));
    }
    DrawRectangle(PROTESTER_TERRITORY_X - TERRITORY_RANGE, 0, TERRITORY_RANGE * 2, SCREEN_HEIGHT, Fade(GREEN, 0.1f));
    DrawRectangle(POLICE_TERRITORY_X - TERRITORY_RANGE, 0, TERRITORY_RANGE * 2, SCREEN_HEIGHT, Fade(BLUE, 0.1f));
}

void draw_barriers() {
    for (int i = 0; i < MAX_BARRIERS; i++) {
        if (barriers[i].active) {
            Color c = (barriers[i].type == CAR) ? RED : GREEN;
            DrawLineEx(barriers[i].start, barriers[i].end, 4.0f, c);
        }
    }
}

void draw_entities() {
    for (int i = 0; i < MAX_PROTESTERS; i++) {
        if (protesters[i].active) {
            float scale = 1.0f + 0.2f * (protesters[i].animation_timer / ANIMATION_DURATION);
            DrawCircleV(protesters[i].position, 10.0f * scale, RED);
            draw_health_bar(protesters[i].position, protesters[i].bullet_health, PROTESTER_BULLET_HEALTH, GREEN);
            if (i == selected_entity && selected_type == PROTESTER) {
                DrawCircleLines(protesters[i].position.x, protesters[i].position.y, 12.0f * scale, BLACK);
            }
            if (protesters[i].is_taking_cover) {
                DrawText("C", protesters[i].position.x - 5, protesters[i].position.y - 25, 10, BLACK);
            }
        }
    }
    for (int i = 0; i < MAX_POLICE; i++) {
        if (police[i].active) {
            float scale = 1.0f + 0.2f * (police[i].animation_timer / ANIMATION_DURATION);
            if (police[i].police_type == HELICOPTER) {
                Vector2 pos = police[i].position;
                if (police[i].ai_state == DYING) {
                    for (int k = 0; k < 5; k++) {
                        float offset_x = sinf(GetTime() * 10 + k) * 10.0f;
                        float offset_y = cosf(GetTime() * 10 + k) * 10.0f;
                        Vector2 exp_pos = {pos.x + offset_x, pos.y + offset_y};
                        float exp_size = 15.0f * (1.0f - police[i].animation_timer / DYING_DURATION);
                        DrawCircleV(exp_pos, exp_size, ORANGE);
                    }
                }
                DrawRectangleRounded((Rectangle){pos.x - 30, pos.y - 10, 60, 20}, 0.5, 10, BLUE);
                DrawRectangle(pos.x + 30, pos.y - 3, 25, 5, BLUE);
                float tail_angle = GetTime() * 720;
                Vector2 tail_center = {pos.x + 50, pos.y};
                for (int k = 0; k < 2; k++) {
                    float a = tail_angle + k * 180;
                    Vector2 end = {tail_center.x + cosf(a * DEG2RAD) * 7, tail_center.y + sinf(a * DEG2RAD) * 7};
                    DrawLineV(tail_center, end, BLACK);
                }
                Vector2 rotor_center = {pos.x, pos.y};
                float rotor_angle = GetTime() * 360;
                DrawCircle(rotor_center.x, rotor_center.y, 4, GRAY);
                for (int k = 0; k < 4; k++) {
                    float a = rotor_angle + k * 90;
                    Vector2 end = {rotor_center.x + cosf(a * DEG2RAD) * 35, rotor_center.y + sinf(a * DEG2RAD) * 35};
                    DrawLineEx(rotor_center, end, 3, BLACK);
                }
              DrawRectangle(pos.x - 28, pos.y - 8, 8, 8, YELLOW);
              DrawRectangle(pos.x - 28, pos.y - (-3), 8, 8, YELLOW);

                if (police[i].ai_state != DYING) {
                    draw_health_bar((Vector2){pos.x, pos.y + 20}, police[i].bullet_health, HELICOPTER_HEALTH, GREEN);
                }
            } else if (police[i].police_type == SHOOTER) {
                DrawCircleV(police[i].position, 10.0f * scale, BLUE);
                draw_health_bar(police[i].position, police[i].bullet_health, POLICE_HEALTH, GREEN);
            } else {
                DrawRectangleV(Vector2Subtract(police[i].position, (Vector2){10.0f * scale, 10.0f * scale}), 
                               (Vector2){20.0f * scale, 20.0f * scale}, BLUE);
                draw_health_bar(police[i].position, police[i].bullet_health, POLICE_HEALTH, GREEN);
            }
        }
    }
}

void draw_projectiles() {
    for (int i = 0; i < MAX_PROJECTILES; i++) {
        if (projectiles[i].active) {
            DrawCircleV(projectiles[i].position, 3.0f, projectiles[i].type == PROTESTER ? BROWN : WHITE);
        }
    }
}

void draw_ui() {
    char hold_time_text[32];
    snprintf(hold_time_text, sizeof(hold_time_text), "Territory Hold: %.1fs / %.1fs", game.territory_hold_timer, WIN_HOLD_TIME);
    DrawText(hold_time_text, 10, 40, 20, BLACK);
    int active_protesters = 0, active_police = 0;
    int attacking_protesters = 0, retreating_protesters = 0, cover_protesters = 0;
    int attacking_police = 0;
    for (int i = 0; i < MAX_PROTESTERS; i++) {
        if (protesters[i].active) {
            active_protesters++;
            if (protesters[i].ai_state == ATTACKING) attacking_protesters++;
            if (protesters[i].ai_state == RETREATING) retreating_protesters++;
            if (protesters[i].ai_state == TAKING_COVER) cover_protesters++;
        }
    }
    for (int i = 0; i < MAX_POLICE; i++) {
        if (police[i].active && police[i].ai_state != DYING) {
            active_police++;
            if (police[i].ai_state == ATTACKING) attacking_police++;
        }
    }
    char count_text[80];
    snprintf(count_text, sizeof(count_text), 
             "Protesters: %d (Attack: %d, Retreat: %d, Cover: %d) | Police: %d (Attack: %d)", 
             active_protesters, attacking_protesters, retreating_protesters, cover_protesters, 
             active_police, attacking_police);
    DrawText(count_text, 10, 60, 20, BLACK);
    char morale_text[64];
    snprintf(morale_text, sizeof(morale_text), "Morale - Protesters: %.2f | Police: %.2f", 
             game.protester_morale, game.police_morale);
    DrawText(morale_text, 10, 80, 20, BLACK);
}

void draw_start_screen() {
    ClearBackground(DARKGRAY);
    DrawText("Protest Simulation", SCREEN_WIDTH / 2 - MeasureText("Protest Simulation", 40) / 2, SCREEN_HEIGHT / 2 - 100, 40, BLACK);
    DrawText("Press SPACE to Start", SCREEN_WIDTH / 2 - MeasureText("Press SPACE to Start", 20) / 2, SCREEN_HEIGHT / 2, 20, BLACK);
    DrawText("Right-click to control protester, Left-click to throw stones", 
             SCREEN_WIDTH / 2 - MeasureText("Right-click to control protester, Left-click to throw stones", 20) / 2, 
             SCREEN_HEIGHT / 2 + 40, 20, BLACK);
}

void draw_end_screen() {
    ClearBackground(DARKGRAY);
    const char *message = game.state == PROTESTER_WIN ? "Protesters Win!" : "Police Win!";
    DrawText(message, SCREEN_WIDTH / 2 - MeasureText(message, 40) / 2, SCREEN_HEIGHT / 2 - 100, 40, BLACK);
    DrawText("Press SPACE to Restart", SCREEN_WIDTH / 2 - MeasureText("Press SPACE to Restart", 20) / 2, SCREEN_HEIGHT / 2, 20, BLACK);
}

void handle_selection() {
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        Vector2 mouse_pos = GetMousePosition();
        float closest_dist = 50.0f;
        int closest_entity = -1;
        EntityType closest_type = PROTESTER;
        for (int i = 0; i < MAX_PROTESTERS; i++) {
            if (protesters[i].active) {
                float dist = distance(mouse_pos, protesters[i].position);
                if (dist < closest_dist) {
                    closest_dist = dist;
                    closest_entity = i;
                    closest_type = PROTESTER;
                }
            }
        }
        if (closest_entity != -1) {
            if (selected_entity != -1) {
                Entity *prev_selected = &protesters[selected_entity];
                prev_selected->is_player_controlled = false;
            }
            selected_entity = closest_entity;
            selected_type = PROTESTER;
            Entity *new_selected = &protesters[selected_entity];
            new_selected->is_player_controlled = true;
            new_selected->is_taking_cover = false;
            new_selected->cover_barrier_id = -1;
            new_selected->ai_state = MOVING;
        } else {
            if (selected_entity != -1) {
                Entity *prev_selected = &protesters[selected_entity];
                prev_selected->is_player_controlled = false;
            }
            selected_entity = -1;
        }
    }
}

void update_game() {
    update_morale();
    update_protester_cover();
    handle_selection();
    for (int i = 0; i < MAX_PROTESTERS; i++) {
        if (protesters[i].active && !protesters[i].is_player_controlled) {
            update_protester_ai(&protesters[i], i);
        }
    }
    for (int i = 0; i < MAX_POLICE; i++) {
        if (police[i].active) {
            update_police_ai(&police[i], i);
        }
    }
    if (selected_entity != -1) {
        Entity *selected = &protesters[selected_entity];
        if (selected->active) {
            update_player_controlled(selected);
        } else {
            selected_entity = -1;
        }
    }
    update_projectiles();
    check_game_conditions();
}

void reset_game() {
    for (int i = 0; i < MAX_PROTESTERS; i++) protesters[i].active = false;
    for (int i = 0; i < MAX_POLICE; i++) police[i].active = false;
    for (int i = 0; i < MAX_PROJECTILES; i++) projectiles[i].active = false;
    for (int i = 0; i < MAX_BARRIERS; i++) barriers[i].active = false;
    selected_entity = -1;
    init_game();
    game.state = PLAYING;
}

void draw_game() {
    ClearBackground(GRAY);
    draw_background();
    draw_barriers();
    draw_entities();
    draw_projectiles();
    draw_ui();
}

int main() {
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Protest Simulation");
    SetTargetFPS(60);
    init_game();
    while (!WindowShouldClose()) {
        BeginDrawing();
        switch (game.state) {
            case START:
                draw_start_screen();
                if (IsKeyPressed(KEY_SPACE)) {
                    game.state = PLAYING;
                }
                break;
            case PLAYING:
                update_game();
                draw_game();
                break;
            case PROTESTER_WIN:
            case POLICE_WIN:
                draw_end_screen();
                if (IsKeyPressed(KEY_SPACE)) {
                    reset_game();
                }
                break;
        }
        EndDrawing();
    }
    CloseWindow();
    return 0;
}