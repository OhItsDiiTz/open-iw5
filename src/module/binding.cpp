#include <std_include.hpp>
#include <loader/module_loader.hpp>
#include "game/game.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>

namespace
{
	std::vector<std::string> custom_binds;

	utils::hook::detour key_get_binding_for_cmd_hook;

	utils::hook::detour cl_execute_key_hook;

	game::native::KeyState* keys;

	int key_write_bindings_to_buffer([[maybe_unused]] int local_client_num, char* buffer, int buffer_size)
	{
		buffer_size = buffer_size - 4;
		auto bytes_used = 0;

		for (auto keyIndex = 0; keyIndex < 256; ++keyIndex)
		{
			if (keys[keyIndex].binding && keys[keyIndex].binding < 91)
			{
				auto len = sprintf_s(&buffer[bytes_used], buffer_size - bytes_used, "bind %s \"%s\"\n",
					game::native::Key_KeynumToString(keyIndex, false), game::native::command_whitelist[keys[keyIndex].binding]);

				if (len < 0)
				{
					return bytes_used;
				}

				bytes_used += len;
			}
			else if (keys[keyIndex].binding >= 91)
			{
				auto value = keys[keyIndex].binding - 91;
				if (static_cast<std::size_t>(value) < custom_binds.size() && !custom_binds[value].empty())
				{
					auto len = sprintf_s(&buffer[bytes_used], buffer_size - bytes_used, "bind %s \"%s\"\n",
						game::native::Key_KeynumToString(keyIndex, false), custom_binds[value].data());

					if (len < 0)
					{
						return bytes_used;
					}

					bytes_used += len;
				}
			}
		}

		buffer[bytes_used] = '\0';
		return bytes_used;
	}

	__declspec(naked) void key_write_bindings_to_buffer_stub()
	{
		__asm
		{
			pushad

			push [esp + 0x20 + 0x8] // bufferSize
			push [esp + 0x20 + 0x8] // buffer
			push eax // localClientNum
			call key_write_bindings_to_buffer
			add esp, 0xC

			popad

			ret
		}
	}

	int get_binding_for_custom_command(const char* command)
	{
		auto index = 0;
		for (const auto& bind : custom_binds)
		{
			if (bind == command)
			{
				return index;
			}

			++index;
		}

		custom_binds.emplace_back(command);
		index = static_cast<int>(custom_binds.size()) - 1;

		return index;
	}

	int key_get_binding_for_cmd_stub(const char* command)
	{
		for (auto i = 0; i <= 91; i++)
		{
			if (game::native::command_whitelist[i] && !std::strcmp(command, game::native::command_whitelist[i]))
			{
				return i;
			}
		}

		return 91 + get_binding_for_custom_command(command);
	}

	void cl_execute_key_stub(game::native::LocalClientNum_t local_client_num, int key, int down)
	{
		if (key >= 91)
		{
			key -= 91;

			if (static_cast<std::uint32_t>(key) < custom_binds.size() && !custom_binds[key].empty())
			{
				game::native::Cbuf_AddText(local_client_num, utils::string::va("%s\n", custom_binds[key].data()));
			}

			return;
		}

		cl_execute_key_hook.invoke<void>(local_client_num, key, down);
	}
}

class binding final : public module
{
public:
	void post_load() override
	{
		if (game::is_sp())
		{
			return;
		}

		keys = reinterpret_cast<game::native::KeyState*>(0xB3C760);

		utils::hook(0x48C53A, key_write_bindings_to_buffer_stub, HOOK_CALL).install()->quick();

		key_get_binding_for_cmd_hook.create(0x48C1C0, &key_get_binding_for_cmd_stub);

		cl_execute_key_hook.create(0x48AF00, cl_execute_key_stub);
	}
};

REGISTER_MODULE(binding)
