#include "stdafx.h"
#include <iostream>
#include <SDL.h>
#include <vector>
#include <chrono>
#include <numeric>
#include <tuple>
#include <functional>

#define PI 3.14159265

const int WIDTH = 800, HEIGHT = 600, PIXELS = HEIGHT * WIDTH;
const float ASPECT_RATIO = (float)WIDTH / (float)HEIGHT;
const int VIEWPORT_SIZE = 1;
const int PROJECTION_PLANE_Z = 1;

bool LIGHTING_ENABLED = false;
bool SPECULAR_LIGHTING_ENABLED = false;
bool CASTING_SHADOWS_ENABLED = false;

SDL_Renderer *RENDER;
SDL_Color BACKGROUND_COLOR = { 0, 0, 0, 255 };

struct Vector3D {
	float x;
	float y;
	float z;
};

Vector3D CAMERA_POSITION = { 0, 0, 0 };

Vector3D canvasToViewport(float x, float y) {
	Vector3D coords_3d = { x * VIEWPORT_SIZE / WIDTH * ASPECT_RATIO, y * VIEWPORT_SIZE / HEIGHT, PROJECTION_PLANE_Z };
	return coords_3d;
}

inline float dotProduct(Vector3D v1, Vector3D v2) {
	return (v1.x*v2.x) + (v1.y*v2.y) + (v1.z*v2.z);
}

inline Vector3D vecSubtract(Vector3D v1, Vector3D v2) {
	Vector3D result = { v1.x - v2.x, v1.y - v2.y, v1.z - v2.z };
	return result;
}

inline Vector3D vecAdd(Vector3D v1, Vector3D v2) {
	Vector3D result = { v1.x + v2.x, v1.y + v2.y, v1.z + v2.z };
	return result;
}

inline Vector3D vecMultiply(float factor, Vector3D v) {
	Vector3D result = { factor * v.x, factor * v.y, factor * v.z };
	return result;
}

inline float vecLength(Vector3D v) {
	return sqrt(dotProduct(v, v));
}

struct SphereObject {
	Vector3D center;
	float radius;
	SDL_Color color;
	float specular;
};

enum LightType { Ambient = 0, Point = 1, Directional = 2 };

struct Light {
	LightType type;
	Vector3D position;
	Vector3D direction;
	float intensity;
	SDL_Color color;
};

inline float* intersectRaySphere(Vector3D origin, Vector3D direction, SphereObject sphere) {
	static float results[2];
	Vector3D oc = vecSubtract(origin, sphere.center);

	float k1 = dotProduct(direction, direction);
	float k2 = 2 * dotProduct(oc, direction);
	float k3 = dotProduct(oc, oc) - sphere.radius*sphere.radius;
	
	float discriminant = k2 * k2 - 4 * k1*k3;
	if (discriminant < 0) {
		results[0] = FLT_MAX;
		results[1] = FLT_MAX;
		return results;
	}

	results[0] = (-k2 + sqrt(discriminant)) / (2 * k1);
	results[1] = (-k2 - sqrt(discriminant)) / (2 * k1);
	return results;
}



inline std::tuple<float, SphereObject *> ClosestIntersection(SphereObject * spheres, int sphere_count, Vector3D origin, Vector3D direction, float mint_t, float max_t) {
	float closest_t = FLT_MAX;
	SphereObject * closest_sphere = NULL;

	float * ts;
	for (int i = 0; i < sphere_count; i++) {
		ts = intersectRaySphere(origin, direction, spheres[i]);
		if (ts[0] < closest_t && mint_t < ts[0] && ts[0] < max_t) {
			closest_t = ts[0];
			closest_sphere = &spheres[i];
		}

		if (ts[1] < closest_t && mint_t < ts[1] && ts[1] < max_t) {
			closest_t = ts[1];
			closest_sphere = &spheres[i];
		}
	}

	return std::make_tuple(closest_t, closest_sphere); // Always works
}

inline float computeLightingIntensity(SphereObject * spheres, int sphere_count, Light * lights, int light_count, Vector3D point, Vector3D normal, Vector3D view_angle, float specular) {
	if (!LIGHTING_ENABLED) return 1;

	float intensity = 0.0;
	float length_normal = vecLength(normal);
	float length_view = vecLength(view_angle);
	float shadow_t = FLT_MAX;
	SphereObject * shadow_sphere = NULL;

	Vector3D light_direction;
	for (int i = 0; i < light_count; i++)
	{
		if (lights[i].type == Ambient) {
			intensity += lights[i].intensity;
		}
		else {
			light_direction = lights[i].type == Point ? vecSubtract(lights[i].position, point) : lights[i].direction;
			
			// Check for shadows:
			if (CASTING_SHADOWS_ENABLED) {
				float t_max = lights[i].type == Point ? 1 : FLT_MAX;

				
				std::tie(shadow_t, shadow_sphere) = ClosestIntersection(spheres, sphere_count, point, light_direction, 0.001, t_max);
				if (shadow_sphere) continue;
			}
			
			// Diffuse Lighting:
			float n_dot_l = dotProduct(normal, light_direction);
			if (n_dot_l > 0) intensity += lights[i].intensity * n_dot_l / (length_normal * vecLength(light_direction));

			// Specular Lighting:
			if (SPECULAR_LIGHTING_ENABLED && specular != -1)
			{
				Vector3D reflection = vecSubtract(vecMultiply(2.0 * dotProduct(normal, light_direction), normal), light_direction);
				float r_dot_v = dotProduct(reflection, view_angle);
				if (r_dot_v > 0) intensity += lights[i].intensity * pow(r_dot_v / (vecLength(reflection) * length_view), specular);
			}
		}
	}
	return intensity;
}

inline SDL_Color traceRay(SphereObject * spheres, int sphere_count, Light * lights, int light_count, Vector3D origin, Vector3D direction, float mint_t, float max_t) {
	float closest_t = FLT_MAX;
	SphereObject * closest_sphere = NULL;

	std::tie(closest_t, closest_sphere) = ClosestIntersection(spheres, sphere_count, origin, direction, mint_t, max_t);

	float * ts;
	for (int i = 0; i < sphere_count; i++) {
		ts = intersectRaySphere(origin, direction, spheres[i]);
		if (ts[0] < closest_t && mint_t < ts[0] && ts[0] < max_t) {
			closest_t = ts[0];
			closest_sphere = &spheres[i];
		}

		if (ts[1] < closest_t && mint_t < ts[1] && ts[1] < max_t) {
			closest_t = ts[1];
			closest_sphere = &spheres[i];
		}
	}

	if (closest_sphere) {
		Vector3D point = vecAdd(origin, vecMultiply(closest_t, direction));
		Vector3D normal = vecSubtract(point, closest_sphere->center);
		normal = vecMultiply(1.0 / vecLength(normal), normal);
		float intensity = computeLightingIntensity(spheres, sphere_count, lights, light_count, point, normal, vecMultiply(-1, direction), closest_sphere->specular);

		SDL_Color lit_color = {
			closest_sphere->color.r * (intensity > 1 ? 1 : intensity),
			closest_sphere->color.g * (intensity > 1 ? 1 : intensity),
			closest_sphere->color.b * (intensity > 1 ? 1 : intensity),
			closest_sphere->color.a
		};
		return lit_color;
	} 
	return BACKGROUND_COLOR;
}


void setPixel(SDL_Surface *surface, int x, int y, SDL_Color color) {
	x = WIDTH / 2 + x;
	y = HEIGHT / 2 - y - 1;

	if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;

	Uint8 *target_pixel = (Uint8 *)surface->pixels + y * surface->pitch + x * 4;
	*(Uint32 *)target_pixel = SDL_MapRGB(surface->format, color.r, color.g, color.b);
}

float average(int numbers[], int size) {
	int sum = 0;
	for (int i = 0; i < size; ++i)
	{
		sum += numbers[i];
	}
	return ((float)sum) / size;
}
 

int main(int argc, char *argv[])
{
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0)
	{
		std::cout << "SDL could not be initialized: " << SDL_GetError() << std::endl;
		return EXIT_FAILURE;
	}

	SDL_Window *window;
	SDL_Surface* screen_surface = NULL;
	SDL_CreateWindowAndRenderer(WIDTH, HEIGHT, 0, &window, &RENDER);
	SDL_SetWindowTitle(window, "Ray trace demo");

	if (NULL == window)
	{
		std::cout << "Could not create window: " << SDL_GetError() << std::endl;
		return EXIT_FAILURE;
	}

	SDL_Event windowEvent;
	screen_surface = SDL_GetWindowSurface(window);


	// Setup Scence
	SDL_Color point_color = { 255, 0, 0, 255 };
		
	int sphere_count = 4;
	SphereObject spheres[] = {
		{ { 0, 1, 3 }, 1, { 255, 0, 0, 255 }, 500 },
		{ { 2, 0, 4 }, 1, { 0, 255, 0, 255 }, 500 },
		{ { -2, 0, 4 }, 1, { 0, 0, 255, 255 }, 10 },
		{ { 0, -5001, 0 }, 5000, { 255, 255, 0, 255 }, 1000 },
	};

	int light_count = 3;
	Light lights[3] = {
		{ Ambient, { 0, 0, 0 } ,{ 0, 0, 0 }, 0.2 },
		{ Point, { 2, 1, 0 },{ 0, 0, 0 }, 0.6 },
		{ Directional,{ 0, 0, 0 }, { 1, 4, 4 }, 0.2 }
	};
		
	int render_time_milli = 0;

	float scene_key_frame = 0;
	while (true)
	{
		bool quit = false;
		if (SDL_PollEvent(&windowEvent))
		{
			switch (windowEvent.type)
			{
			case SDL_QUIT: {
				quit = true;
				break;
			}
			case SDL_KEYUP: {
				switch (windowEvent.key.keysym.sym)
				{
				case SDLK_1: {
					std::cout << "Render mode 1\n";
					LIGHTING_ENABLED = false;
					SPECULAR_LIGHTING_ENABLED = false;
					CASTING_SHADOWS_ENABLED = false;
					break;
				}
				case SDLK_2: {
					std::cout << "Render mode 2\n";
					LIGHTING_ENABLED = true;
					SPECULAR_LIGHTING_ENABLED = false;
					CASTING_SHADOWS_ENABLED = false;
					break;
				}
				case SDLK_3: {
					std::cout << "Render mode 3\n";
					LIGHTING_ENABLED = true;
					SPECULAR_LIGHTING_ENABLED = true;
					CASTING_SHADOWS_ENABLED = false;
					break;
				}
				case SDLK_4: {
					std::cout << "Render mode 4\n";
					LIGHTING_ENABLED = true;
					SPECULAR_LIGHTING_ENABLED = true;
					CASTING_SHADOWS_ENABLED = true;
					break;
				}
				default:
					break;
				}
			}
			}
		}

		if (quit) break;
	
	    auto start = std::chrono::high_resolution_clock::now();
		if (SDL_MUSTLOCK(screen_surface)) SDL_LockSurface(screen_surface);

		scene_key_frame += 5;
		if (scene_key_frame >= 360) scene_key_frame = 0;
		spheres[0].center.y = 0.1*(float)sin(scene_key_frame*PI / 180);

		lights[1].position.x = 1.5*(float)cos(scene_key_frame*PI / 180);
		lights[1].position.z = 1.5*(float)sin(scene_key_frame*PI / 180);


		for (int x = (-WIDTH / 2); x < (WIDTH / 2); x++) {
			for (int y = (-HEIGHT / 2); y < (HEIGHT / 2); y++) {
				Vector3D direction = canvasToViewport(x, y);
				SDL_Color draw_color = traceRay(spheres, sphere_count, lights, light_count, CAMERA_POSITION, direction, 1, FLT_MAX);
				
				setPixel(screen_surface, x, y, draw_color);
			}
		}

		if (SDL_MUSTLOCK(screen_surface)) SDL_UnlockSurface(screen_surface);
		auto finish = std::chrono::high_resolution_clock::now();
		render_time_milli = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start).count();

		SDL_UpdateWindowSurface(window);

		std::cout << "Scene Render Time: " << render_time_milli << "milli\n";
	}

	while (true)
	{
		if (SDL_PollEvent(&windowEvent))
		{
			if (SDL_QUIT == windowEvent.type) break;
		}
	}

	SDL_DestroyRenderer(RENDER);
	SDL_DestroyWindow(window);
	SDL_Quit();

    return EXIT_SUCCESS;
}


