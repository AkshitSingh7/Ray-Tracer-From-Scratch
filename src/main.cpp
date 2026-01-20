#include "raylib.h"
#include "../include/rtweekend.h"
#include "../include/hittable_list.h"
#include "../include/sphere.h"
#include "../include/material.h"
#include <vector>
#include <string>
#include <omp.h> 

// higher resolution allowed by multithreading performance
const int RENDER_WIDTH = 800;
const int RENDER_HEIGHT = 450; 
const int MAX_DEPTH = 50; 

std::vector<color> accumulation_buffer(RENDER_WIDTH * RENDER_HEIGHT);
int frame_count = 0;

Color *pixel_buffer = new Color[RENDER_WIDTH * RENDER_HEIGHT];

// camera state
point3 camera_pos(0, 1, 4); 
double yaw = -90.0;
double pitch = -10.0;
bool cursor_locked = false;

color ray_color(const ray& r, const hittable& world, int depth) {
    hit_record rec;

    // bounce limit reached, return shadow
    if (depth <= 0) return color(0,0,0);

    if (world.hit(r, 0.001, infinity, rec)) {
        ray scattered;
        color attenuation;
        color emitted = rec.mat_ptr->emitted(0, 0, rec.p);

        if (!rec.mat_ptr->scatter(r, rec, attenuation, scattered))
            return emitted;

        return emitted + attenuation * ray_color(scattered, world, depth-1);
    }

    // background gradient
    vec3 unit_direction = unit_vector(r.direction());
    auto t = 0.5 * (unit_direction.y() + 1.0);
    return (1.0-t)*color(0.05, 0.05, 0.05) + t*color(0.1, 0.1, 0.2);
}

int main() {
    InitWindow(1200, 675, "Multi-Core Path Tracer"); 
    SetTargetFPS(60);

    hittable_list world;
    
    // scene materials
    auto mat_ground = make_shared<lambertian>(color(0.5, 0.5, 0.5));
    auto mat_center = make_shared<lambertian>(color(0.1, 0.2, 0.5));
    auto mat_left   = make_shared<metal>(color(0.8, 0.8, 0.8), 0.1); 
    auto mat_right  = make_shared<metal>(color(0.8, 0.6, 0.2), 0.8);
    auto mat_light  = make_shared<diffuse_light>(color(8, 8, 8));

    // scene objects
    world.add(make_shared<sphere>(point3( 0.0, -100.5, -1.0), 100.0, mat_ground));
    world.add(make_shared<sphere>(point3( 0.0,    0.0, -1.0),   0.5, mat_center));
    world.add(make_shared<sphere>(point3(-1.0,    0.0, -1.0),   0.5, mat_left));
    world.add(make_shared<sphere>(point3( 1.0,    0.0, -1.0),   0.5, mat_right));
    world.add(make_shared<sphere>(point3(-2.0, 4.0, -2.0), 2.0, mat_light));

    Image screenImage = GenImageColor(RENDER_WIDTH, RENDER_HEIGHT, BLACK);
    Texture2D screenTexture = LoadTextureFromImage(screenImage);

    while (!WindowShouldClose()) {
        bool camera_moved = false;

        // input handling
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { DisableCursor(); cursor_locked = true; }
        if (IsKeyPressed(KEY_LEFT_ALT)) { EnableCursor(); cursor_locked = false; }
        if (IsKeyPressed(KEY_P)) { TakeScreenshot("render_highres.png"); }

        if (cursor_locked) {
            Vector2 delta = GetMouseDelta();
            if (delta.x != 0 || delta.y != 0) camera_moved = true;
            yaw += delta.x * 0.1f;
            pitch -= delta.y * 0.1f;
            if (pitch > 89.0) pitch = 89.0;
            if (pitch < -89.0) pitch = -89.0;
        }

        // camera vectors
        vec3 front;
        front.e[0] = cos(degrees_to_radians(yaw)) * cos(degrees_to_radians(pitch));
        front.e[1] = sin(degrees_to_radians(pitch));
        front.e[2] = sin(degrees_to_radians(yaw)) * cos(degrees_to_radians(pitch));
        vec3 camera_front = unit_vector(front);
        vec3 world_up(0, 1, 0);
        vec3 camera_right = unit_vector(cross(camera_front, world_up));
        vec3 camera_up = unit_vector(cross(camera_right, camera_front));

        // movement logic
        float speed = 0.1f;
        if (IsKeyDown(KEY_W)) { camera_pos += camera_front * speed; camera_moved = true; }
        if (IsKeyDown(KEY_S)) { camera_pos += camera_front * -speed; camera_moved = true; }
        if (IsKeyDown(KEY_A)) { camera_pos += camera_right * -speed; camera_moved = true; }
        if (IsKeyDown(KEY_D)) { camera_pos += camera_right * speed; camera_moved = true; }
        if (IsKeyDown(KEY_SPACE)) { camera_pos += world_up * speed; camera_moved = true; }
        if (IsKeyDown(KEY_LEFT_SHIFT)) { camera_pos += world_up * -speed; camera_moved = true; }

        if (camera_moved) {
            frame_count = 0;
            std::fill(accumulation_buffer.begin(), accumulation_buffer.end(), color(0,0,0));
        }
        frame_count++;

        // viewport setup
        auto viewport_height = 2.0;
        auto viewport_width = 16.0/9.0 * viewport_height;
        auto w = unit_vector(camera_front * -1.0);
        auto u = unit_vector(cross(world_up, w));
        auto v = cross(w, u);
        auto horizontal = viewport_width * u;
        auto vertical = viewport_height * v;
        auto lower_left_corner = camera_pos - horizontal/2 - vertical/2 - w;

        // parallelize rendering loop across available cores
        // dynamic scheduling assists with load balancing (sky is faster to render than objects)
        #pragma omp parallel for schedule(dynamic)
        for (int j = 0; j < RENDER_HEIGHT; ++j) {
            for (int i = 0; i < RENDER_WIDTH; ++i) {
                // antialiasing via random jitter
                auto u_off = (double(i) + random_double()) / (RENDER_WIDTH-1);
                auto v_off = (double(RENDER_HEIGHT - 1 - j) + random_double()) / (RENDER_HEIGHT-1);
                
                ray r(camera_pos, lower_left_corner + u_off*horizontal + v_off*vertical - camera_pos);
                color pixel_color = ray_color(r, world, MAX_DEPTH);

                // calculate flat index
                int idx = j * RENDER_WIDTH + i;
                
                // accumulation buffer update
                // thread-safe without locks because each thread operates on a unique index
                if (frame_count == 1) accumulation_buffer[idx] = color(0,0,0);
                accumulation_buffer[idx] += pixel_color;
                
                color accum_color = accumulation_buffer[idx] / frame_count;
                
                // gamma correction and clamping
                auto clamp = [](double x) { return x > 0.999 ? 0.999 : x < 0 ? 0 : x; };
                pixel_buffer[idx] = Color{
                    (unsigned char)(255.99 * clamp(sqrt(accum_color.x()))),
                    (unsigned char)(255.99 * clamp(sqrt(accum_color.y()))),
                    (unsigned char)(255.99 * clamp(sqrt(accum_color.z()))),
                    255
                };
            }
        }

        UpdateTexture(screenTexture, pixel_buffer); 
        BeginDrawing();
            ClearBackground(BLACK);
            // draw updated texture scaled to window
            DrawTexturePro(screenTexture, 
                Rectangle{0, 0, (float)RENDER_WIDTH, (float)RENDER_HEIGHT}, 
                Rectangle{0, 0, 1200, 675}, 
                Vector2{0,0}, 0.0f, WHITE);
            
            DrawFPS(10, 10);
            DrawText("Using ALL CPU Cores", 10, 30, 20, GREEN);
        EndDrawing();
    }

    UnloadTexture(screenTexture);
    CloseWindow();
    return 0;
}
