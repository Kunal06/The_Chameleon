// header
#include "world.hpp"

// stlib
#include <string.h>
#include <cassert>
#include <sstream>
#include <iostream>

namespace
{
const size_t MAX_SPOTTERS = 5;
const size_t MAX_WANDERERS = 10;
const size_t SPOTTER_DELAY_MS = 2000;
const int MAX_COOLDOWN = 50;
int cooldown = MAX_COOLDOWN;

// TODO
vec2 spotter_loc[5];

namespace
{
void glfw_err_cb(int error, const char *desc)
{
	fprintf(stderr, "%d: %s", error, desc);
}
} // namespace
} // namespace

World::World() : m_control(0),
				 m_current_game_state(0),
				 m_game_state(0),
				 m_next_wanderer_spawn(0.f),
				 m_points(0)
{
	// send rng with random device
	m_rng = std::default_random_engine(std::random_device()());
}

World::~World()
{
}

// initialization
bool World::init(vec2 screen)
{
	// TODO
	spotter_loc[0] = {100, 100};
	spotter_loc[1] = {screen.x - 100, 100};
	spotter_loc[2] = {100, screen.y - 100};
	spotter_loc[3] = {screen.x - 100, screen.y - 100};
	spotter_loc[4] = {800, 500};

	// GLFW / OGL Initialization
	// Core Opengl 3.
	glfwSetErrorCallback(glfw_err_cb);
	if (!glfwInit())
	{
		fprintf(stderr, "Failed to initialize GLFW");
		return false;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);
#if __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
	glfwWindowHint(GLFW_RESIZABLE, 0);
	m_window = glfwCreateWindow((int)screen.x, (int)screen.y, "The Chameleon", nullptr, nullptr);
	if (m_window == nullptr)
		return false;

	glfwMakeContextCurrent(m_window);
	glfwSwapInterval(1); // vsync

	// load OpenGL function pointers
	gl3w_init();

	// set callbacks to member functions (that's why the redirect is needed)
	// input is handled using GLFW, for more info see
	// http://www.glfw.org/docs/latest/input_guide.html
	glfwSetWindowUserPointer(m_window, this);
	auto key_redirect = [](GLFWwindow *wnd, int _0, int _1, int _2, int _3) { ((World *)glfwGetWindowUserPointer(wnd))->on_key(wnd, _0, _1, _2, _3); };
	auto cursor_pos_redirect = [](GLFWwindow *wnd, double _0, double _1) { ((World *)glfwGetWindowUserPointer(wnd))->on_mouse_move(wnd, _0, _1); };
	glfwSetKeyCallback(m_window, key_redirect);
	glfwSetCursorPosCallback(m_window, cursor_pos_redirect);

	// create a frame buffer
	m_frame_buffer = 0;
	glGenFramebuffers(1, &m_frame_buffer);
	glBindFramebuffer(GL_FRAMEBUFFER, m_frame_buffer);

	// for some high DPI displays (ex. Retina Display on Macbooks)
	// https://stackoverflow.com/questions/36672935/why-retina-screen-coordinate-value-is-twice-the-value-of-pixel-value
	int fb_width, fb_height;
	glfwGetFramebufferSize(m_window, &fb_width, &fb_height);
	m_screen_scale = static_cast<float>(fb_width) / screen.x;

	// initialize the screen texture
	m_screen_tex.create_from_screen(m_window);

	// load music and sounds
	if (SDL_Init(SDL_INIT_AUDIO) < 0)
	{
		fprintf(stderr, "Failed to initialize SDL Audio");
		return false;
	}

	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) == -1)
	{
		fprintf(stderr, "Failed to open audio device");
		return false;
	}

	m_background_music = Mix_LoadMUS(audio_path("music.wav"));
	m_char_dead_sound = Mix_LoadWAV(audio_path("char_dead.wav"));
	m_char_green_sound = Mix_LoadWAV(audio_path("green_sound.wav"));
	m_char_win_sound = Mix_LoadWAV(audio_path("char_win.wav"));

	if (m_background_music == nullptr || m_char_dead_sound == nullptr || m_char_win_sound == nullptr || m_char_green_sound == nullptr)
	{
		fprintf(stderr, "Failed to load sounds\n %s\n %s\n %s\n %s\n make sure the data directory is present",
				audio_path("music.wav"),
				audio_path("char_dead.wav"),
				audio_path("green_sound.wav"),
				audio_path("char_win.wav"));
		return false;
	}

	// play background music
	Mix_PlayMusic(m_background_music, -1);
	fprintf(stderr, "Loaded music\n");

	m_current_speed = 1.f;

	return m_start_screen.init() &&
		   m_control_screen.init() &&
		   m_story_screen.init() &&
		   m_map.init() &&
		   m_char.init() &&
		   m_trophy.init() &&
		   m_complete_screen.init();
}

// release all the associated resources
void World::destroy()
{
	glDeleteFramebuffers(1, &m_frame_buffer);

	if (m_background_music != nullptr)
		Mix_FreeMusic(m_background_music);
	if (m_char_dead_sound != nullptr)
		Mix_FreeChunk(m_char_dead_sound);
	if (m_char_green_sound != nullptr)
		Mix_FreeChunk(m_char_green_sound);
	if (m_char_win_sound != nullptr)
		Mix_FreeChunk(m_char_win_sound);

	Mix_CloseAudio();

	for (auto &spotter : m_spotters)
		spotter.destroy();
	for (auto &wanderer : m_wanderers)
		wanderer.destroy();
	m_wanderers.clear();
	m_spotters.clear();
	m_trophy.destroy();
	m_char.destroy();
	m_map.destroy();

	glfwDestroyWindow(m_window);
}

// update our game world
bool World::update(float elapsed_ms)
{
	int w, h;
	glfwGetFramebufferSize(m_window, &w, &h);
	vec2 screen = {(float)w / m_screen_scale, (float)h / m_screen_scale};

	m_start_screen.update(m_current_game_state);
	m_control_screen.update(m_current_game_state);
	m_story_screen.update(m_current_game_state);
	m_complete_screen.update(m_current_game_state);

	// cooldown update
	if (!(cooldown >= MAX_COOLDOWN))
	{
		cooldown++;
	}
	if (m_game_state == 3)
	{
		// wall collisions
		m_map.is_wall(m_char);

		// collision, char-spotter
		for (const auto &spotter : m_spotters)
		{
			if (m_char.collides_with(spotter) && is_char_detectable(m_map))
			{
				if (m_char.is_alive())
				{
					Mix_PlayChannel(-1, m_char_dead_sound, 0);
					m_map.set_char_dead();
				}
				m_char.kill();
				break;
			}
		}

		// collision, char-wanderer
		for (const auto &wanderer : m_wanderers)
		{
			if (m_char.collides_with(wanderer) && is_char_detectable(m_map))
			{
				if (m_char.is_alive())
				{
					Mix_PlayChannel(-1, m_char_dead_sound, 0);
					m_map.set_char_dead();
				}
				m_char.kill();
				break;
			}
		}

		// collision, char-trophy
		if (m_char.collides_with(m_trophy))
		{
			if (m_char.is_alive())
			{
				Mix_PlayChannel(-1, m_char_win_sound, 0);
				m_map.set_char_dead();
				m_game_state = 5;
			}
			m_char.kill();
		}

		m_char.update(elapsed_ms);

		// TODO
		for (auto &wanderer : m_wanderers)
		{
			int xPos = wanderer.get_position().x;
			int yPos = wanderer.get_position().y;
			if (wanderer.m_direction_wanderer.x > 0)
			{
				if (xPos > screen.x - 150)
				{
					wanderer.m_direction_wanderer.y = 0.75;
					wanderer.m_direction_wanderer.x = 0;
				}
			}
			else if (wanderer.m_direction_wanderer.y > 0)
			{
				if (yPos > screen.y - 150)
				{
					wanderer.m_direction_wanderer.x = -0.75;
					wanderer.m_direction_wanderer.y = 0;

					wanderer.flip_in_x = 1;
				}
			}
			else if (wanderer.m_direction_wanderer.x < 0)
			{
				if (xPos < 150)
				{
					wanderer.m_direction_wanderer.x = 0;
					wanderer.m_direction_wanderer.y = -0.75;
				}
			}
			else if (wanderer.m_direction_wanderer.y < 1)
			{
				if (yPos < 150)
				{
					wanderer.m_direction_wanderer.x = 0.75;
					wanderer.m_direction_wanderer.y = 0;

					wanderer.flip_in_x = -1;
				}
			}
			wanderer.update(elapsed_ms * m_current_speed);
		}

		// update spotters
		for (auto &spotter : m_spotters)
			spotter.update(elapsed_ms * m_current_speed);

		// spawn spotter
		if (m_spotters.size() < MAX_SPOTTERS)
		{
			if (!spawn_spotter())
				return false;

			Spotter &new_spotter = m_spotters.back();

			new_spotter.set_position(spotter_loc[m_spotters.size() - 1]);
		}

		if (m_map.get_flash_time() > 1)
		{
			m_map.reset_flash_time();
			m_map.set_flash(0);
		}

		if (m_char.get_dash())
		{
			if (m_char.get_wall_collision())
			{
				m_char.set_dash(false);
				recent_dash = true;
			}
			else
			{
				m_char.dash();
			}
		}
		if (recent_dash)
		{
			recent_dash = false;
			cooldown = 0;
			// fprintf(stderr, "DIRECTION CHANGE - %f", m_char.get_direction_change());
			switch ((int)m_char.get_direction_change())
			{
			case 0:
				m_char.set_direction('L', true);
				m_char.move({-15.f, 0.f});
				m_char.set_direction('L', false);
				break;
			case 1:
				m_char.set_direction('R', true);
				m_char.move({15.f, 0.f});
				m_char.set_direction('R', false);
				break;
			case 2:
				m_char.set_direction('D', true);
				m_char.move({0.f, 15.f});
				m_char.set_direction('D', false);
				break;
			case 3:
				m_char.set_direction('U', true);
				m_char.move({0.f, -15.f});
				m_char.set_direction('U', false);
				break;
			}
		}
		// spawn wanderer
		m_next_wanderer_spawn -= elapsed_ms * m_current_speed;
		if (m_wanderers.size() < MAX_WANDERERS && m_next_wanderer_spawn < 0.f)
		{
			if (!spawn_wanderer())
				return false;

			Wanderer &new_wanderer = m_wanderers.back();

			// set random initial position
			new_wanderer.set_position({screen.x + 200, 50 + m_dist(m_rng) * (screen.y - 50)});

			// next spawn
			m_next_wanderer_spawn = (SPOTTER_DELAY_MS / 2) + m_dist(m_rng) * (SPOTTER_DELAY_MS / 2);
		}

		// restart game
		if (!m_char.is_alive() && m_map.get_char_dead_time() > 4)
		{
			m_char.destroy();
			m_trophy.destroy();
			m_char.init();
			m_trophy.init();
			m_spotters.clear();
			m_wanderers.clear();
			m_map.reset_char_dead_time();
			m_current_speed = 1.f;
		}
		return true;
	}

	return true;
}

// render
// http://www.opengl-tutorial.org/intermediate-tutorials/tutorial-14-render-to-texture/
void World::draw()
{
	// clear error buffer
	gl_flush_errors();

	// get size of window
	int w, h;
	glfwGetFramebufferSize(m_window, &w, &h);

	// update window title with points
	std::stringstream title_ss;
	title_ss << "Points: " << m_points;
	glfwSetWindowTitle(m_window, title_ss.str().c_str());

	// first render to the custom framebuffer
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// clear backbuffer
	glViewport(0, 0, w, h);
	glDepthRange(0.00001, 10);
	const float clear_color[3] = {0.3f, 0.3f, 0.3f};
	glClearColor(clear_color[0], clear_color[1], clear_color[2], 1.0);
	glClearDepth(1.f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	mat3 projection_2D = calculateProjectionMatrix(w, h);

	// game state
	switch (m_game_state)
	{
	case 0:
		m_start_screen.draw(projection_2D);
		break;
	case 1:
		m_control_screen.draw(projection_2D);
		break;
	case 2:
		glfwDestroyWindow(m_window);
		break;
	case 3:
		// draw map
		m_map.draw(projection_2D);
		if (m_map.get_flash() == 0)
		{
			// draw entities
			for (auto &spotter : m_spotters)
				spotter.draw(projection_2D);
			for (auto &wanderer : m_wanderers)
				wanderer.draw(projection_2D);
			m_trophy.draw(projection_2D);
			m_char.draw(projection_2D);
		}
		// bind our texture in Texture Unit 0
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, m_screen_tex.id);
		break;
	case 4:
		m_story_screen.draw(projection_2D);
		break;
	case 5:
		m_complete_screen.draw(projection_2D);
		break;
	}

	// present
	glfwSwapBuffers(m_window);
}

mat3 World::calculateProjectionMatrix(int width, int height)
{
	float left = 0.f; // *-0.5;
	float top = 0.f;  // (float)h * -0.5;
	float right = 0.f;
	float bottom = 0.f;

	if (m_game_state != 3)
	{
		right = (float)width / m_screen_scale;   // *0.5;
		bottom = (float)height / m_screen_scale; // *0.5;
	}
	else
	{
		left = m_char.get_position().x - ((float)width / (4 * m_screen_scale));
		top = m_char.get_position().y - ((float)height / (4 * m_screen_scale));
		right = m_char.get_position().x + ((float)width / (4 * m_screen_scale));
		bottom = m_char.get_position().y + ((float)height / (4 * m_screen_scale));
	}
	float sx = 2.f / (right - left);
	float sy = 2.f / (top - bottom);
	float tx = -(right + left) / (right - left);
	float ty = -(top + bottom) / (top - bottom);
	return {{sx, 0.f, 0.f}, {0.f, sy, 0.f}, {tx, ty, 1.f}};
}

bool World::is_over() const
{
	return glfwWindowShouldClose(m_window);
}

// spawn spotter
bool World::spawn_spotter()
{
	Spotter spotter;
	if (spotter.init())
	{
		m_spotters.emplace_back(spotter);
		return true;
	}
	fprintf(stderr, "Failed to spawn spotter");
	return false;
}

// spawn wanderer
bool World::spawn_wanderer()
{
	Wanderer wanderer;
	if (wanderer.init())
	{
		m_wanderers.emplace_back(wanderer);
		return true;
	}
	fprintf(stderr, "Failed to spawn wanderer");
	return false;
}

// key callback function
void World::on_key(GLFWwindow *, int key, int, int action, int mod)
{
	if (m_game_state != 3)
	{
		if (action == GLFW_PRESS && key == GLFW_KEY_DOWN)
		{
			if (m_current_game_state < 2)
				m_current_game_state++;
		}

		if (action == GLFW_PRESS && key == GLFW_KEY_UP)
		{
			if (m_current_game_state > 0)
				m_current_game_state--;
		}

		if (action == GLFW_PRESS && key == GLFW_KEY_ENTER)
		{
			if (m_game_state == 4)
			{
				m_game_state = 3;
			}
			else if (m_game_state == 5)
			{
				m_game_state = 0;
			}
			else if (m_current_game_state == 0)
			{
				m_game_state = 4;
			}
			else if (m_game_state == 1)
			{
				m_game_state = 0;
			}
			else
			{
				m_game_state = m_current_game_state;
			}
		}
	}

	if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
	{
		m_current_game_state = 0;
		m_game_state = 0;
	}

	if (action == GLFW_PRESS && m_game_state == 3 && !m_char.get_dash())
	{
		// opposite movements - when blue
		if (m_char.get_color_change() == 3.0)
		{
			if ((key == GLFW_KEY_D && m_control == 0) || (key == GLFW_KEY_RIGHT && m_control == 1))
			{
				m_char.change_direction(1.0);
				m_char.set_direction('L', true);
			}
			else if ((key == GLFW_KEY_A && m_control == 0) || (key == GLFW_KEY_LEFT && m_control == 1))
			{
				m_char.change_direction(0.0);
				m_char.set_direction('R', true);
			}
			else if ((key == GLFW_KEY_W && m_control == 0) || (key == GLFW_KEY_UP && m_control == 1))
			{
				m_char.change_direction(3.0);
				m_char.set_direction('D', true);
			}
			else if ((key == GLFW_KEY_S && m_control == 0) || (key == GLFW_KEY_DOWN && m_control == 1))
			{
				m_char.change_direction(2.0);
				m_char.set_direction('U', true);
			}
		}
		// proper movement
		else
		{
			if ((key == GLFW_KEY_D && m_control == 0) || (key == GLFW_KEY_RIGHT && m_control == 1))
			{
				m_char.change_direction(0.0);
				m_char.set_direction('R', true);
			}
			else if ((key == GLFW_KEY_A && m_control == 0) || (key == GLFW_KEY_LEFT && m_control == 1))
			{
				m_char.change_direction(1.0);
				m_char.set_direction('L', true);
			}
			else if ((key == GLFW_KEY_W && m_control == 0) || (key == GLFW_KEY_UP && m_control == 1))
			{
				m_char.change_direction(2.0);
				m_char.set_direction('U', true);
			}
			else if ((key == GLFW_KEY_S && m_control == 0) || (key == GLFW_KEY_DOWN && m_control == 1))
			{
				m_char.change_direction(3.0);
				m_char.set_direction('D', true);
			}
		}
	}

	if (action == GLFW_PRESS && m_game_state == 3 && cooldown >= MAX_COOLDOWN)
	{
		// red
		if (((key == GLFW_KEY_UP && m_control == 0) || (key == GLFW_KEY_W && m_control == 1)) && m_char.get_color_change() != 1.0)
		{
			m_char.change_color(1.0);
			m_char.set_dash(true);
			m_char.dash();
		}
		// green
		else if (((key == GLFW_KEY_DOWN && m_control == 0) || (key == GLFW_KEY_S && m_control == 1)) && m_char.get_color_change() != 2.0)
		{
			Mix_PlayChannel(-1, m_char_green_sound, 0);
			m_char.change_color(2.0);
			cooldown = 0;
		}
		// blue
		else if (((key == GLFW_KEY_LEFT && m_control == 0) || (key == GLFW_KEY_A && m_control == 1)) && m_char.get_color_change() != 3.0)
		{
			m_char.change_color(3.0);
			cooldown = 0;
		}
		// yellow
		else if (((key == GLFW_KEY_RIGHT && m_control == 0) || (key == GLFW_KEY_D && m_control == 1)) && m_char.get_color_change() != 4.0)
		{
			m_map.set_flash(1);
			m_char.change_color(4.0);
			cooldown = 0;
		}
	}

	// remove movement
	if (action == GLFW_RELEASE && m_game_state == 3)
	{
		if (m_char.get_color_change() == 3.0)
		{
			if ((key == GLFW_KEY_D && m_control == 0) || (key == GLFW_KEY_RIGHT && m_control == 1))
				m_char.set_direction('L', false);
			else if ((key == GLFW_KEY_A && m_control == 0) || (key == GLFW_KEY_LEFT && m_control == 1))
				m_char.set_direction('R', false);
			else if ((key == GLFW_KEY_W && m_control == 0) || (key == GLFW_KEY_UP && m_control == 1))
				m_char.set_direction('D', false);
			else if ((key == GLFW_KEY_S && m_control == 0) || (key == GLFW_KEY_DOWN && m_control == 1))
				m_char.set_direction('U', false);
		}
		else
		{
			if ((key == GLFW_KEY_D && m_control == 0) || (key == GLFW_KEY_RIGHT && m_control == 1))
				m_char.set_direction('R', false);
			else if ((key == GLFW_KEY_A && m_control == 0) || (key == GLFW_KEY_LEFT && m_control == 1))
				m_char.set_direction('L', false);
			else if ((key == GLFW_KEY_W && m_control == 0) || (key == GLFW_KEY_UP && m_control == 1))
				m_char.set_direction('U', false);
			else if ((key == GLFW_KEY_S && m_control == 0) || (key == GLFW_KEY_DOWN && m_control == 1))
				m_char.set_direction('D', false);
		}
	}

	// game mode
	if (action == GLFW_PRESS)
	{
		if (key == GLFW_KEY_1)
			m_control = 0;
		else if (key == GLFW_KEY_2)
			m_control = 1;
	}

	// reset
	if (action == GLFW_RELEASE && key == GLFW_KEY_R)
	{
		int w, h;
		glfwGetWindowSize(m_window, &w, &h);
		m_char.destroy();
		m_trophy.destroy();
		m_char.init();
		m_trophy.init();
		m_wanderers.clear();
		m_spotters.clear();
		m_map.reset_char_dead_time();
		m_current_speed = 1.f;
	}

	// game current speed
	if (action == GLFW_RELEASE && (mod & GLFW_MOD_SHIFT) && key == GLFW_KEY_COMMA)
		m_current_speed -= 0.1f;
	if (action == GLFW_RELEASE && (mod & GLFW_MOD_SHIFT) && key == GLFW_KEY_PERIOD)
		m_current_speed += 0.1f;

	m_current_speed = fmax(0.f, m_current_speed);
}

void World::on_mouse_move(GLFWwindow *window, double xpos, double ypos)
{
	//
}

bool World::is_char_detectable(Map m_map)
{
	float collision_tile = m_map.collides_with(m_char) - 1.0;
	return collision_tile != m_char.get_color_change();
}