#include <std_include.hpp>
#include <loader/module_loader.hpp>
#include "game/game.hpp"

#include <utils/hook.hpp>

#include "command.hpp"
#include "file_system.hpp"
#include "log_file.hpp"

namespace
{
	utils::hook::detour sys_default_install_path_hook;

	const game::native::dvar_t** fs_homepath;
	const game::native::dvar_t** fs_debug;
	const game::native::dvar_t** fs_ignoreLocalized;

	FILE* file_for_handle(const int f)
	{
		assert(!game::native::fsh[f].zipFile);
		assert(game::native::fsh[f].handleFiles.file.o);

		return game::native::fsh[f].handleFiles.file.o;
	}

	unsigned int file_write(const void* ptr, const unsigned int len, FILE* stream)
	{
		return std::fwrite(ptr, sizeof(char), len, stream);
	}

	FILE* file_open_append_text(const char* filename)
	{
		errno = 0;
		auto* file = std::fopen(filename, "at");
		if (file)
		{
			return file;
		}

		log_file::info("Couldn't open file: %s %s\n", filename, std::strerror(errno));
		return nullptr;
	}

	FILE* file_open_write_binary(const char* filename)
	{
		errno = 0;
		auto* file = std::fopen(filename, "wb");
		if (file)
		{
			return file;
		}

		log_file::info("Couldn't open file: %s %s\n", filename, std::strerror(errno));
		return nullptr;
	}

	void replace_separators(char* path)
	{
		char* src, * dst;

		bool was_sep = false;

		for (src = path, dst = path; *src; ++src)
		{
			if (*src == '/' || *src == '\\')
			{
				if (!was_sep)
				{
					was_sep = true;
					*dst++ = '\\';
				}
			}
			else
			{
				was_sep = false;
				*dst++ = *src;
			}
		}
		*dst = 0;
	}

	void build_os_path_for_thread(const char* base, const char* game, const char* qpath, char* ospath, game::native::FsThread thread)
	{
		assert(base);
		assert(qpath);
		assert(ospath);

		if (!game)
		{
			game = "";
		}
		else if (!game[0])
		{
			game = game::native::fs_gamedir;
		}

		auto len_base = std::strlen(base);
		auto len_game = std::strlen(game);
		auto len_qpath = std::strlen(qpath);
		if (len_game + 1 + len_base + len_qpath + 1 >= game::native::MAX_OSPATH)
		{
			if (thread)
			{
				*ospath = '\0';
				return;
			}

			game::native::Com_Error(game::native::ERR_FATAL, "\x15" "FS_BuildOSPath: os path length exceeded\n");
		}

		std::memcpy(ospath, base, len_base);
		ospath[len_base] = '/';

		std::memcpy(&ospath[len_base + 1], game, len_game);
		ospath[len_base + 1 + len_game] = '/';

		std::memcpy(ospath + len_base + 2 + len_game, qpath, len_qpath + 1);
		replace_separators(ospath);
	}

	game::native::FsThread get_current_thread()
	{
		if (game::native::Sys_IsMainThread())
		{
			return game::native::FS_THREAD_MAIN;
		}
		if (game::native::Sys_IsDatabaseThread())
		{
			return game::native::FS_THREAD_DATABASE;
		}
		if (game::native::Sys_IsStreamThread())
		{
			return game::native::FS_THREAD_STREAM;
		}
		if (game::native::Sys_IsRenderThread())
		{
			return game::native::FS_THREAD_BACKEND;
		}
		if (game::native::Sys_IsServerThread())
		{
			return game::native::FS_THREAD_SERVER;
		}
		return game::native::FS_THREAD_INVALID;
	}

	int handle_for_file_current_thread()
	{
		return game::native::FS_HandleForFile(get_current_thread());
	}

	int open_file_append(const char* filename)
	{
		char ospath[game::native::MAX_OSPATH]{};

		game::native::FS_CheckFileSystemStarted();
		const auto* basepath = (*fs_homepath)->current.string;
		build_os_path_for_thread(basepath, game::native::fs_gamedir, filename, ospath, game::native::FS_THREAD_MAIN);
		if ((*fs_debug)->current.integer)
		{
			log_file::info("FS_FOpenFileAppend: %s\n", ospath);
		}

		if (game::native::FS_CreatePath(ospath))
		{
			return 0;
		}

		auto* f = file_open_append_text(ospath);
		if (!f)
		{
			return 0;
		}

		auto h = handle_for_file_current_thread();
		game::native::fsh[h].zipFile = nullptr;
		strncpy_s(game::native::fsh[h].name, filename, _TRUNCATE);
		game::native::fsh[h].handleFiles.file.o = f;
		game::native::fsh[h].handleSync = 0;

		if (!game::native::fsh[h].handleFiles.file.o)
		{
			game::native::FS_FCloseFile(h);
			h = 0;
		}

		return h;
	}

	int get_handle_and_open_file(const char* filename, const char* ospath, game::native::FsThread thread)
	{
		auto* fp = file_open_write_binary(ospath);
		if (!fp)
		{
			return 0;
		}

		const auto f = game::native::FS_HandleForFile(thread);
		game::native::fsh[f].zipFile = nullptr;
		game::native::fsh[f].handleFiles.file.o = fp;

		strncpy_s(game::native::fsh[f].name, filename, _TRUNCATE);
		game::native::fsh[f].handleSync = 0;

		return f;
	}

	int open_file_write_to_dir_for_thread(const char* filename, const char* dir, const char* osbasepath, game::native::FsThread thread)
	{
		char ospath[game::native::MAX_OSPATH]{};

		game::native::FS_CheckFileSystemStarted();

		const char* basepath = (*fs_homepath)->current.string;
		build_os_path_for_thread(basepath, dir, filename, ospath, game::native::FS_THREAD_MAIN);

		if ((*fs_debug)->current.integer)
		{
			log_file::info("FS_FOpenFileWriteToDirForThread: %s\n", ospath);
		}

		if (game::native::FS_CreatePath(ospath))
		{
			return 0;
		}

		return get_handle_and_open_file(filename, ospath, thread);
	}

	int open_file_write(const char* filename)
	{
		return open_file_write_to_dir_for_thread(filename, game::native::fs_gamedir, "", game::native::FS_THREAD_MAIN);
	}

	void convert_path(char* s)
	{
		while (*s)
		{
			if (*s == '\\' || *s == ':')
			{
				*s = '/';
			}
			++s;
		}
	}

	int path_cmp(const char* s1, const char* s2)
	{
		int c1;

		do
		{
			c1 = *s1++;
			int c2 = *s2++;

			if (game::native::I_islower(c1))
			{
				c1 -= ('a' - 'A');
			}
			if (game::native::I_islower(c2))
			{
				c2 -= ('a' - 'A');
			}
			if (c1 == '\\' || c1 == ':')
			{
				c1 = '/';
			}
			if (c2 == '\\' || c2 == ':')
			{
				c2 = '/';
			}

			if (c1 < c2)
			{
				return -1; // strings not equal
			}

			if (c1 > c2)
			{
				return 1;
			}
		} while (c1);

		return 0; // strings are equal
	}

	void sort_file_list(char** filelist, int numfiles)
	{
		int j;

		const char** sortedlist = static_cast<const char**>(game::native::Z_Malloc((numfiles * sizeof(*sortedlist)) + 4));
		sortedlist[0] = nullptr;
		auto numsortedfiles = 0;
		for (auto i = 0; i < numfiles; ++i)
		{
			for (j = 0; j < numsortedfiles; j++)
			{
				if (path_cmp(filelist[i], sortedlist[j]) < 0)
				{
					break;
				}
			}

			for (auto k = numsortedfiles; k > j; --k)
			{
				sortedlist[k] = sortedlist[k - 1];
			}
			sortedlist[j] = filelist[i];
			++numsortedfiles;
		}

		std::memcpy(filelist, sortedlist, numfiles * sizeof(*filelist));
		std::free(sortedlist);
	}

	int use_search_path(game::native::searchpath_s* pSearch)
	{
		if (pSearch->bLocalized && (*fs_ignoreLocalized)->current.enabled)
		{
			return 0;
		}

		if (pSearch->bLocalized && pSearch->language != game::native::SEH_GetCurrentLanguage())
		{
			return 0;
		}

		return 1;
	}

	int iwd_is_pure(game::native::iwd_t* iwd)
	{
		if (*game::native::fs_numServerIwds)
		{
			for (auto i = 0; i < *game::native::fs_numServerIwds; ++i)
			{
				if (iwd->checksum == game::native::fs_serverIwds[i])
				{
					return 1;
				}
			}

			return 0;
		}

		return 1;
	}

	char** list_files(const char* path, const char* extension, game::native::FsListBehavior_e behavior, int* numfiles, int allocTrackType)
	{
		return game::native::FS_ListFilteredFiles(*game::native::fs_searchpaths, path, extension, nullptr, behavior, numfiles, allocTrackType);
	}

	void display_path(bool b_language_cull)
	{
		auto i_language = game::native::SEH_GetCurrentLanguage();
		const auto* psz_language_name = game::native::SEH_GetLanguageName(i_language);
		log_file::info("Current language: %s\n", psz_language_name);
		if ((*fs_ignoreLocalized)->current.enabled)
		{
			log_file::info("    localized assets are being ignored\n");
		}

		log_file::info("Current search path:\n");
		for (auto* s = *game::native::fs_searchpaths; s; s = s->next)
		{
			if (b_language_cull && !use_search_path(s))
			{
				continue;
			}

			if (s->iwd)
			{
				log_file::info("%s (%i files)\n", s->iwd->iwdFilename, s->iwd->numfiles);
				if (s->bLocalized)
				{
					log_file::info("    localized assets iwd file for %s\n", game::native::SEH_GetLanguageName(s->language));
				}

				if (*game::native::fs_numServerIwds)
				{
					if (iwd_is_pure(s->iwd))
					{
						log_file::info("    on the pure list\n");
					}
					else
					{
						log_file::info("    not on the pure list\n");
					}
				}
			}
			else
			{
				log_file::info("%s/%s\n", s->dir->path, s->dir->gamedir);
				if (s->bLocalized)
				{
					log_file::info("    localized assets game folder for %s\n", game::native::SEH_GetLanguageName(s->language));
				}
			}
		}

		log_file::info("\nFile Handles:\n");
		for (int i = 1; i < 64; ++i)
		{
			if (game::native::fsh[i].handleFiles.file.o)
			{
				log_file::info("handle %i: %s\n", i, game::native::fsh[i].name);
			}
		}
	}

	bool touch_file(const char* name)
	{
		*game::native::com_fileAccessed = 1;
		auto ret = game::native::FS_FOpenFileReadForThread(name, nullptr, game::native::FS_THREAD_MAIN);
		return ret != -1;
	}

	void path_f()
	{
		display_path(true);
	}

	void full_path_f()
	{
		display_path(false);
	}

	void dir_f()
	{
		const char* path;
		const char* extension;
		int ndirs;

		if (game::native::Cmd_Argc() < 2 || game::native::Cmd_Argc() > 3)
		{
			log_file::info("usage: dir <directory> [extension]\n");
			return;
		}

		if (game::native::Cmd_Argc() == 2)
		{
			path = game::native::Cmd_Argv(1);
			extension = "";
		}
		else
		{
			path = game::native::Cmd_Argv(1);
			extension = game::native::Cmd_Argv(2);
		}

		log_file::info("Directory of %s %s\n", path, extension);
		log_file::info("---------------\n");

		auto** dirnames = list_files(path, extension, game::native::FS_LIST_PURE_ONLY, &ndirs, 3);

		for (int i = 0; i < ndirs; ++i)
		{
			log_file::info("%s\n", dirnames[i]);
		}

		game::native::Sys_FreeFileList(dirnames);
	}

	void new_dir_f()
	{
		int ndirs;

		if (game::native::Cmd_Argc() < 2)
		{
			log_file::info("usage: fdir <filter>\n");
			log_file::info("example: fdir *q3dm*.bsp\n");
			return;
		}

		const auto* filter = game::native::Cmd_Argv(1);

		log_file::info("---------------\n");

		auto** dirnames = game::native::FS_ListFilteredFiles(*game::native::fs_searchpaths, "", "", filter, game::native::FS_LIST_PURE_ONLY, &ndirs, 3);
		sort_file_list(dirnames, ndirs);

		for (auto i = 0; i < ndirs; ++i)
		{
			convert_path(dirnames[i]);
			log_file::info("%s\n", dirnames[i]);
		}
		log_file::info("%d files listed\n", ndirs);
		game::native::Sys_FreeFileList(dirnames);
	}

	void touch_file_f()
	{
		if (game::native::Cmd_Argc() != 2)
		{
			log_file::info("Usage: touchFile <file>\n");
			return;
		}

		touch_file(game::native::Cmd_Argv(1));
	}

	void add_commands()
	{
		Cmd_AddCommand("path", path_f);
		Cmd_AddCommand("fullpath", full_path_f);
		Cmd_AddCommand("dir", dir_f);
		Cmd_AddCommand("fdir", new_dir_f);
		Cmd_AddCommand("touchFile", touch_file_f);
	}

	void fs_startup_stub(char* game_name)
	{
		log_file::info("----- FS_Startup -----\n");

		utils::hook::invoke<void>(0x5B1070, game_name);

		add_commands();
		display_path(true);

		log_file::info("----------------------\n");
		log_file::info("%d files in iwd files\n", *game::native::fs_iwdFileCount);
	}

	void fs_shutdown_stub(int closemfp)
	{
		utils::hook::invoke<void>(0x5B0D30, closemfp);

		game::native::Cmd_RemoveCommand("path");
		game::native::Cmd_RemoveCommand("fullpath");
		game::native::Cmd_RemoveCommand("dir");
		game::native::Cmd_RemoveCommand("fdir");
		game::native::Cmd_RemoveCommand("touchFile");
	}

	const char* sys_default_install_path_stub()
	{
		static auto current_path = std::filesystem::current_path().string();
		return current_path.data();
	}
}

int file_system::open_file_by_mode(const char* qpath, int* f, game::native::fsMode_t mode)
{
	auto r = 6969;
	auto sync = 0;

	switch (mode)
	{
	case game::native::FS_READ:
		*game::native::com_fileAccessed = TRUE;
		r = game::native::FS_FOpenFileReadForThread(qpath, f, game::native::FS_THREAD_MAIN);
		break;
	case game::native::FS_WRITE:
		*f = open_file_write(qpath);
		r = 0;
		if (!*f)
		{
			r = -1;
		}
		break;
	case game::native::FS_APPEND_SYNC:
		sync = 1;
	case game::native::FS_APPEND:
		*f = open_file_append(qpath);
		r = 0;
		if (!*f )
		{
			r = -1;
		}
		break;
	default:
		game::native::Com_Error(game::native::ERR_FATAL, "\x15" "FSH_FOpenFile: bad mode");
		break;
	}

	if (!f)
	{
		return r;
	}

	if (*f)
	{
		game::native::fsh[*f].fileSize = r;
		game::native::fsh[*f].streamed = 0;
	}

	game::native::fsh[*f].handleSync = sync;
	return r;
}

int file_system::write(const char* buffer, int len, int h)
{
	game::native::FS_CheckFileSystemStarted();
	if (!h)
	{
		return 0;
	}

	auto* f = file_for_handle(h);
	auto* buf = const_cast<char*>(buffer);
	auto remaining = len;
	auto tries = 0;
	while (remaining)
	{
		const auto block = remaining;
		const auto written = static_cast<int>(file_write(buf, block, f));
		if (!written)
		{
			if (tries)
			{
				return 0;
			}
			tries = 1;
		}

		if (written == -1)
		{
			return 0;
		}

		remaining -= written;
		buf += written;
	}

	if (game::native::fsh[h].handleSync)
	{
		std::fflush(f);
	}

	return len;
}

void file_system::post_load()
{
	fs_homepath = reinterpret_cast<const game::native::dvar_t**>(SELECT_VALUE(0x1C2B538, 0x59ADD18));
	fs_debug = reinterpret_cast<const game::native::dvar_t**>(SELECT_VALUE(0x1C2B32C, 0x59A9A08));
	fs_ignoreLocalized = reinterpret_cast<const game::native::dvar_t**>(SELECT_VALUE(0x1C2B21C, 0x59A99F8));

	if (game::is_mp())
	{
		utils::hook(0x5B20AA, fs_startup_stub, HOOK_CALL).install()->quick(); // FS_InitFilesystem
		utils::hook(0x5B2148, fs_startup_stub, HOOK_CALL).install()->quick(); // FS_Restart

		utils::hook(0x5557CC, fs_shutdown_stub, HOOK_CALL).install()->quick(); // Com_Quit_f
		utils::hook(0x5B2115, fs_shutdown_stub, HOOK_CALL).install()->quick(); // FS_Restart
	}

	// Make open-iw5 work outside of the game directory
	sys_default_install_path_hook.create(SELECT_VALUE(0x487E50, 0x5C4A80), &sys_default_install_path_stub);

	// fs_basegame
	utils::hook::set<const char*>(SELECT_VALUE(0x629031, 0x5B0FD1), "userraw");
}

REGISTER_MODULE(file_system)
