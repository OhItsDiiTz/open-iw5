#include <std_include.hpp>
#include <loader/module_loader.hpp>
#include "game/game.hpp"

#include "script_loading.hpp"

#include "module/console.hpp"
#include "module/file_system.hpp"
#include "module/scripting.hpp"

#include <utils/hook.hpp>
#include <utils/memory.hpp>

#include <xsk/gsc/types.hpp>
#include <xsk/gsc/interfaces/compiler.hpp>
#include <xsk/gsc/interfaces/assembler.hpp>
#include <xsk/utils/compression.hpp>
#include <xsk/resolver.hpp>
#include <interface.hpp>

namespace gsc
{
	std::uint16_t scr_func_max_id = 0x1C7;

	namespace
	{
		auto compiler = ::gsc::compiler();
		auto assembler = ::gsc::assembler();

		utils::memory::allocator script_file_allocator;

		std::unordered_map<std::string, game::native::ScriptFile*> loaded_scripts;

		std::unordered_map<std::string, int> main_handles;
		std::unordered_map<std::string, int> init_handles;

		void clear()
		{
			loaded_scripts.clear();
			script_file_allocator.clear();
			main_handles.clear();
			init_handles.clear();
		}

		bool read_script_file(const std::string& name, std::string* data)
		{
			char* buffer{};
			const auto file_len = game::native::FS_ReadFile(name.data(), &buffer);
			if (file_len > 0 && buffer)
			{
				data->append(buffer, file_len);
				game::native::Hunk_FreeTempMemory(buffer);
				return true;
			}

			return false;
		}

		game::native::ScriptFile* load_custom_script(const char* file_name, const std::string& real_name)
		{
			if (const auto itr = loaded_scripts.find(real_name); itr != loaded_scripts.end())
			{
				return itr->second;
			}

			std::string source_buffer;
			if (!read_script_file(real_name + ".gsc", &source_buffer))
			{
				return nullptr;
			}

			std::vector<std::uint8_t> data;
			data.assign(source_buffer.begin(), source_buffer.end());

			try
			{
				compiler->compile(real_name, data);
			}
			catch (const std::exception& ex)
			{
				console::error("*********** script compile error *************\n");
				console::error("failed to compile '%s':\n%s", real_name.data(), ex.what());
				console::error("**********************************************\n");
				return nullptr;
			}

			auto assembly = compiler->output();

			try
			{
				assembler->assemble(real_name, assembly);
			}
			catch (const std::exception& ex)
			{
				console::error("*********** script compile error *************\n");
				console::error("failed to assemble '%s':\n%s", real_name.data(), ex.what());
				console::error("**********************************************\n");
				return nullptr;
			}

			const auto script_file_ptr = static_cast<game::native::ScriptFile*>(script_file_allocator.allocate(sizeof(game::native::ScriptFile)));
			script_file_ptr->name = file_name;

			const auto stack = assembler->output_stack();
			script_file_ptr->len = static_cast<int>(stack.size());

			const auto script = assembler->output_script();
			script_file_ptr->bytecodeLen = static_cast<int>(script.size());

			const auto compressed = xsk::utils::zlib::compress(stack);
			const auto stack_size = compressed.size();
			const auto byte_code_size = script.size() + 1;

			script_file_ptr->buffer = static_cast<char*>(game::native::Hunk_AllocateTempMemoryHighInternal(stack_size));
			std::memcpy(const_cast<char*>(script_file_ptr->buffer), compressed.data(), compressed.size());

			script_file_ptr->bytecode = static_cast<std::uint8_t*>(game::native::PMem_AllocFromSource_NoDebug(byte_code_size, 4, 0, game::native::PMEM_SOURCE_SCRIPT));
			std::memcpy(script_file_ptr->bytecode, script.data(), script.size());

			script_file_ptr->compressedLen = static_cast<int>(compressed.size());

			loaded_scripts[real_name] = script_file_ptr;

			return script_file_ptr;
		}

		std::string get_script_file_name(const std::string& name)
		{
			const auto id = xsk::gsc::iw5::resolver::token_id(name);
			if (!id)
			{
				return name;
			}

			return std::to_string(id);
		}

		int db_is_x_asset_default(game::native::XAssetType type, const char* name)
		{
			if (loaded_scripts.contains(name))
			{
				return 0;
			}

			return game::native::DB_IsXAssetDefault(type, name);
		}

		void g_scr_load_scripts_stub()
		{
cd			char path[game::native::MAX_OSPATH]{};

			auto num_files = 0;
			auto** files = file_system::list_files("scripts/", "gsc", game::native::FS_LIST_ALL, &num_files, 10);

			for (auto i = 0; i < num_files; ++i)
			{
				const auto* script_file = files[i];
				console::info("Loading script %s...\n", script_file);

				sprintf_s(path, "%s/%s", "scripts", script_file);

				// Scr_LoadScriptInternal will add the '.gsc' suffix so we remove it
				path[std::strlen(path) - 4] = '\0';

				if (!game::native::Scr_LoadScript(path))
				{
					console::error("Script %s encountered an error while loading\n", path);
					continue;
				}

				console::info("Script %s.gsc loaded successfully\n", path);

				const auto main_handle = game::native::Scr_GetFunctionHandle(path, xsk::gsc::iw5::resolver::token_id("main"));
				if (main_handle)
				{
					console::info("Loaded '%s::main'\n", path);
					main_handles[path] = main_handle;
				}

				const auto init_handle = game::native::Scr_GetFunctionHandle(path, xsk::gsc::iw5::resolver::token_id("init"));
				if (init_handle)
				{
					console::info("Loaded '%s::init'\n", path);
					init_handles[path] = init_handle;
				}
			}

			utils::hook::invoke<void>(0x523DA0);
		}

		void scr_load_level_stub()
		{
			for (const auto& handle : main_handles)
			{
				console::info("Executing '%s::main'\n", handle.first.data());
				const auto id = game::native::Scr_ExecThread(handle.second, 0);
				game::native::Scr_FreeThread(static_cast<std::uint16_t>(id));
			}

			utils::hook::invoke<void>(0x517410); // Scr_LoadLevel

			for (const auto& handle : init_handles)
			{
				console::info("Executing '%s::init'\n", handle.first.data());
				const auto id = game::native::Scr_ExecThread(handle.second, 0);
				game::native::Scr_FreeThread(static_cast<std::uint16_t>(id));
			}			
		}
	}

	game::native::ScriptFile* find_script(game::native::XAssetType type, const char* name, int allow_create_default)
	{
		std::string real_name = name;
		const auto id = static_cast<std::uint16_t>(std::strtol(name, nullptr, 10));
		if (id)
		{
			real_name = xsk::gsc::iw5::resolver::token_name(id);
		}

		auto* script = load_custom_script(name, real_name);
		if (script)
		{
			return script;
		}

		return game::native::DB_FindXAssetHeader(type, name, allow_create_default).scriptfile;
	}

	class script_loading final : public module
	{
	public:
		void post_load() override
		{
			if (game::is_mp()) this->patch_mp();

			// ProcessScript
			utils::hook(SELECT_VALUE(0x44685E, 0x56B13E), find_script, HOOK_CALL).install()->quick();
			utils::hook(SELECT_VALUE(0x446868, 0x56B148), db_is_x_asset_default, HOOK_CALL).install()->quick();

			// Allow custom scripts to include other custom scripts
			xsk::gsc::iw5::resolver::init([](const auto& include_name) -> std::vector<std::uint8_t>
			{
				const auto real_name = include_name + ".gsc";

				std::string file_buffer;
				if (!read_script_file(real_name, &file_buffer) || file_buffer.empty())
				{
					throw std::runtime_error(std::format("Could not load gsc file '{}'", real_name));
				}

				std::vector<std::uint8_t> result;
				result.assign(file_buffer.begin(), file_buffer.end());

				return result;
			});

			scripting::on_shutdown([](int free_scripts) -> void
			{
				if (free_scripts)
				{
					xsk::gsc::iw5::resolver::cleanup();
					clear();
				}
			});
		}

		static void patch_mp()
		{
			utils::hook(0x523F3E, g_scr_load_scripts_stub, HOOK_CALL).install()->quick();

			utils::hook(0x50D4ED, scr_load_level_stub, HOOK_CALL).install()->quick();
		}
	};
}

REGISTER_MODULE(gsc::script_loading)
