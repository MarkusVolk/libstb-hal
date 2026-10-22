/*
 * webOS shell role for the SDL window
 *
 * Copyright (C) 2026 Markus Volk
 *
 * License: GPL-2.0-or-later
 */
#include "config.h"
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <initializer_list>
#include <wayland-client.h>
#include "webos-shell-client-protocol.h"
#include "glfb_webos.h"
#include "hal_debug.h"

#define hal_info(args...) _hal_info(HAL_DEBUG_INIT, NULL, args)

static struct
{
	struct wl_registry *registry;
	struct wl_shell *shell;
	struct wl_webos_shell *webos;
	struct wl_shell_surface *shell_surface;
	struct wl_webos_shell_surface *webos_surface;
	struct wl_display *display;
} g;

static void registry_global(void *, struct wl_registry *registry, uint32_t id, const char *iface, uint32_t)
{
	if (!strcmp(iface, "wl_shell"))
		g.shell = (struct wl_shell *)wl_registry_bind(registry, id, &wl_shell_interface, 1);
	else if (!strcmp(iface, "wl_webos_shell"))
		g.webos = (struct wl_webos_shell *)wl_registry_bind(registry, id, &wl_webos_shell_interface, 1);
}

static void registry_global_remove(void *, struct wl_registry *, uint32_t)
{
}

static const struct wl_registry_listener registry_listener = { registry_global, registry_global_remove };

static void shell_ping(void *, struct wl_shell_surface *surface, uint32_t serial)
{
	wl_shell_surface_pong(surface, serial);
}

static void shell_configure(void *, struct wl_shell_surface *, uint32_t, int32_t, int32_t)
{
}

static void shell_popup_done(void *, struct wl_shell_surface *)
{
}

static const struct wl_shell_surface_listener shell_listener = { shell_ping, shell_configure, shell_popup_done };

/* neutrino is the whole interface: these keys are its, not webOS' */
static void keyPolicies()
{
	for (const char *policy : { "_WEBOS_ACCESS_POLICY_KEYS_BACK", "_WEBOS_ACCESS_POLICY_KEYS_EXIT",
				    "_WEBOS_ACCESS_POLICY_KEYS_HOME", "_WEBOS_ACCESS_POLICY_KEYS_GUIDE" })
		wl_webos_shell_surface_set_property(g.webos_surface, policy, "true");
	/* and while it is in front, webOS opens neither its launcher, quick
	 * settings, input list, nudges or toasts, nor starts apps or switches
	 * inputs on a key (StarfishAccessControlPolicy.qml of the compositor) */
	for (const char *policy : { "_WEBOS_ACCESS_POLICY_RIBBON", "_WEBOS_ACCESS_POLICY_SETTINGS",
				    "_WEBOS_ACCESS_POLICY_INPUTS", "_WEBOS_ACCESS_POLICY_NUDGE", "_WEBOS_ACCESS_POLICY_TOASTS",
				    "_WEBOS_ACCESS_POLICY_KEYACTION_LAUNCH_APP", "_WEBOS_ACCESS_POLICY_KEYACTION_ACTIVATE_XINPUT" })
		wl_webos_shell_surface_set_property(g.webos_surface, policy, "false");
}

static void webos_state_changed(void *, struct wl_webos_shell_surface *, uint32_t state)
{
	hal_info("GLFB: webOS window state %u\n", state);
	/* set again once the window is shown, which is when the compositor
	 * takes them into account */
	keyPolicies();
	if (g.display)
		wl_display_flush(g.display);
}

static void webos_position_changed(void *, struct wl_webos_shell_surface *, int32_t, int32_t)
{
}

static void webos_close(void *, struct wl_webos_shell_surface *)
{
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = SDL_EVENT_QUIT;
	SDL_PushEvent(&ev);
}

static void webos_exposed(void *, struct wl_webos_shell_surface *, struct wl_array *)
{
}

static void webos_state_about_to_change(void *, struct wl_webos_shell_surface *, uint32_t)
{
}

static const struct wl_webos_shell_surface_listener webos_listener = {
	webos_state_changed, webos_position_changed, webos_close, webos_exposed, webos_state_about_to_change
};

static void register_app();

bool glfb_webos_attach(SDL_Window *window, const char *appid)
{
	SDL_PropertiesID props = SDL_GetWindowProperties(window);
	struct wl_display *display = (struct wl_display *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
	struct wl_surface *surface = (struct wl_surface *)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
	if (!display || !surface)
	{
		hal_info("GLFB: no Wayland display behind the SDL window\n");
		return false;
	}
	g.display = display;
	g.registry = wl_display_get_registry(display);
	wl_registry_add_listener(g.registry, &registry_listener, NULL);
	wl_display_roundtrip(display);
	if (!g.shell || !g.webos)
	{
		hal_info("GLFB: compositor offers no webOS shell (wl_shell %p, wl_webos_shell %p)\n", (void *)g.shell, (void *)g.webos);
		return false;
	}
	g.shell_surface = wl_shell_get_shell_surface(g.shell, surface);
	wl_shell_surface_add_listener(g.shell_surface, &shell_listener, NULL);
	wl_shell_surface_set_class(g.shell_surface, appid);
	wl_shell_surface_set_toplevel(g.shell_surface);
	g.webos_surface = wl_webos_shell_get_shell_surface(g.webos, surface);
	wl_webos_shell_surface_add_listener(g.webos_surface, &webos_listener, NULL);
	const char *env = getenv("APPID");
	wl_webos_shell_surface_set_property(g.webos_surface, "appId", env ? env : appid);
	keyPolicies();
	wl_webos_shell_surface_set_property(g.webos_surface, "_WEBOS_ACCESS_POLICY_FORCESTRETCH", "true");
	wl_webos_shell_surface_set_state(g.webos_surface, WL_WEBOS_SHELL_SURFACE_STATE_FULLSCREEN);
	wl_display_flush(display);
	hal_info("GLFB: webOS shell surface set up for %s\n", env ? env : appid);
	register_app();
	return true;
}

/* the app's life cycle through the TV's libhelpers, as Kodi does it:
 * registered, the app stays alive when it goes to the background, and a
 * relaunch from the launcher brings it back at once */
extern "C" {
typedef struct LSHandle LSHandle;
typedef struct LSMessage LSMessage;
typedef bool (*LSFilterFunc)(LSHandle *sh, LSMessage *reply, void *ctx);
typedef struct HContext
{
	LSFilterFunc callback;
	void *userdata;
	void *unknown;
	int multiple;
	int pub;
	unsigned long ret_token;
} HContext;
}

static HContext lifecycle;
static const char *(*luna_message)(LSMessage *);

static bool lifecycle_event(LSHandle *, LSMessage *reply, void *)
{
	const char *msg = luna_message ? luna_message(reply) : NULL;
	hal_info("GLFB: webOS life cycle %s\n", msg ? msg : "");
	if (msg && strstr(msg, "\"relaunch\"") && g.webos_surface)
	{
		wl_webos_shell_surface_set_state(g.webos_surface, WL_WEBOS_SHELL_SURFACE_STATE_FULLSCREEN);
		wl_display_flush(g.display);
	}
	return true;
}

static void register_app()
{
	void *helpers = dlopen("libhelpers.so.2", RTLD_NOW);
	if (!helpers)
	{
		hal_info("GLFB: no libhelpers: %s\n", dlerror());
		return;
	}
	int (*call)(const char *, const char *, HContext *) = (int (*)(const char *, const char *, HContext *))dlsym(helpers, "HLunaServiceCall");
	luna_message = (const char *(*)(LSMessage *))dlsym(helpers, "HLunaServiceMessage");
	if (!call || !luna_message)
	{
		hal_info("GLFB: libhelpers lacks HLunaServiceCall\n");
		return;
	}
	memset(&lifecycle, 0, sizeof(lifecycle));
	lifecycle.callback = lifecycle_event;
	lifecycle.multiple = 1;
	lifecycle.pub = 1;
	if (call("luna://com.webos.service.applicationmanager/registerApp", "{}", &lifecycle))
		hal_info("GLFB: registerApp failed\n");
}

void glfb_webos_release()
{
	if (g.webos_surface)
		wl_webos_shell_surface_destroy(g.webos_surface);
	if (g.shell_surface)
		wl_shell_surface_destroy(g.shell_surface);
	if (g.webos)
		wl_webos_shell_destroy(g.webos);
	if (g.shell)
		wl_shell_destroy(g.shell);
	if (g.registry)
		wl_registry_destroy(g.registry);
	memset(&g, 0, sizeof(g));
}
