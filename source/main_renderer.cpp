/*
 * Super primitive "game" framework.
 * It works without dynamic polymorphism! Yey!
*/
#include <stdlib.h>
#include <SDL.h>

extern int initialize(int argc, char *argv[]);
extern int fixed_update(uint64_t delta_micros);
extern int post_update(uint64_t delta_micros);
extern int render(void);
extern void cleanup(void);

static const uint64_t MICROS_PER_FRAME = 16666ULL; // 60 Hz
static const uint64_t MICROSECONDS_IN_SECOND = 1000000ULL;
static uint64_t TIMER_RESOLUTION;
static uint64_t _curr_app_time; //!< Application time measured in microseconds (counting from zero)
static uint64_t _prev_update_time; //!< Previous application update time in microseconds
static uint64_t _prev_real_time;
static int _status;


static
void terminate(void)
{
	cleanup();
	SDL_Quit();
}


static
void frame(void)
{
	// Keep track of elapsed time
	const uint64_t current_real_time = SDL_GetPerformanceCounter();
	const uint64_t real_time_diff = current_real_time - _prev_real_time;
	const uint64_t app_time_diff = real_time_diff / TIMER_RESOLUTION;
	_curr_app_time += app_time_diff;
	_prev_real_time = current_real_time;

	// Call update() with fixed frequency
	while ((_curr_app_time - _prev_update_time >= MICROS_PER_FRAME) && (_status == 0)) {
		_status = fixed_update(MICROS_PER_FRAME);
		_prev_update_time += MICROS_PER_FRAME;
	}

	if (_status == 0)
		_status = post_update(app_time_diff);

	if (_status == 0)
		_status = render();
}


int main(int argc, char *argv[])
{
	SDL_Init(SDL_INIT_EVERYTHING);
	atexit(terminate);
	_status = initialize(argc, argv);
	
	TIMER_RESOLUTION = SDL_GetPerformanceFrequency() / MICROSECONDS_IN_SECOND;
	_prev_real_time = SDL_GetPerformanceCounter();
	while (_status == 0)
		frame();

	return _status;
}
