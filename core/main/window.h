#pragma once

#include "framework/delegate.h"
#include "notification.h"

struct SDL_Window;

namespace feather {

class Window {
	friend class Renderer;

public:
	struct WindowProperties {
		int width;
		int height;
		int x;
		int y;
	};

	enum class FullscreenMode {
		WINDOWED,
		FULLSCREEN,
		BORDERLESS
	};

private:
	SDL_Window* _internal_window = nullptr;

	WindowProperties _properties;
	FullscreenMode _fullscreen_mode;

	using NotificationDelegate = Delegate<>;
	std::array<NotificationDelegate, std::to_underlying(Notification::COUNT)> _notification_listeners;

	void _on_resize();
	void _on_move();

public:
	Window();

	// Resolve events
	bool update();

	const WindowProperties& properties = _properties;
	const FullscreenMode& fullscreen_mode = _fullscreen_mode;

	void set_fullscreen_mode(const FullscreenMode mode) const;
	void register_notification(Notification notification, const std::function<void()>& delegate);
};

} //namespace feather