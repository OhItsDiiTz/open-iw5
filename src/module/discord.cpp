#include <std_include.hpp>
#include <discord_rpc.h>
#include "loader/module_loader.hpp"
#include "scheduler.hpp"

class discord final : public module
{
public:
	void post_load() override
	{
		DiscordEventHandlers handlers;
		ZeroMemory(&handlers, sizeof(handlers));
		handlers.ready = ready;
		handlers.errored = errored;
		handlers.disconnected = errored;
		handlers.joinGame = nullptr;
		handlers.spectateGame = nullptr;
		handlers.joinRequest = nullptr;

		Discord_Initialize("531526691319971880", &handlers, 1, nullptr);

		scheduler::on_frame(Discord_RunCallbacks);
	}

	void pre_destroy() override
	{
		Discord_Shutdown();
	}

private:
	static void ready(const DiscordUser* request)
	{
		DiscordRichPresence discord_presence;
		ZeroMemory(&discord_presence, sizeof(discord_presence));

		discord_presence.state = "Cake!";
		discord_presence.instance = 1;
		Discord_UpdatePresence(&discord_presence);
	}

	static void errored(int error_code, const char* message)
	{
		printf("Discord: (%i) %s", error_code, message);
	}
};

REGISTER_MODULE(discord)
