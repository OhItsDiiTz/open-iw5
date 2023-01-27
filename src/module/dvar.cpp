#include <std_include.hpp>
#include <loader/module_loader.hpp>
#include "game/game.hpp"

#include "command.hpp"
#include "console.hpp"
#include "log_file.hpp"
#include "dvar.hpp"

int dvar::values_equal(unsigned char type, game::native::DvarValue val0, game::native::DvarValue val1)
{
	switch (type)
	{
	case game::native::DVAR_TYPE_BOOL:
		return val0.enabled == val1.enabled;
	case game::native::DVAR_TYPE_FLOAT:
		return val0.value == val1.value;
	case game::native::DVAR_TYPE_FLOAT_2:
		return val0.vector[0] == val1.vector[0] && val0.vector[1] == val1.vector[1];
	case game::native::DVAR_TYPE_FLOAT_3:
	case game::native::DVAR_TYPE_FLOAT_3_COLOR:
		return val0.vector[0] == val1.vector[0] && val0.vector[1] == val1.vector[1] &&
			val0.vector[2] == val1.vector[2] && val0.vector[3] == val1.vector[3];
	case game::native::DVAR_TYPE_FLOAT_4:
		return game::native::Vec4Compare(val0.vector, val1.vector);
	case game::native::DVAR_TYPE_INT:
		return val0.integer == val1.integer;
	case game::native::DVAR_TYPE_ENUM:
		return val0.integer == val1.integer;
	case game::native::DVAR_TYPE_STRING:
		assert(val0.string);
		assert(val1.string);
		return std::strcmp(val0.string, val1.string) == 0;
	case game::native::DVAR_TYPE_COLOR:
		return val0.integer == val1.integer;
	default:
		assert(0 && "unhandled dvar type");
		return 0;
	}
}

bool dvar::has_latched_value(const game::native::dvar_t* dvar)
{
	return values_equal(dvar->type, dvar->current, dvar->latched) == 0;
}

void dvar::list_single(const game::native::dvar_t* dvar, void* user_data)
{
	if ((user_data != nullptr) && !game::native::Com_Filter(static_cast<const char*>(user_data), dvar->name, 0))
	{
		return;
	}

	if (dvar->flags & game::native::DVAR_SERVERINFO)
	{
		console::info("S");
	}
	else
	{
		console::info(" ");
	}

	if (dvar->flags & game::native::DVAR_USERINFO)
	{
		console::info("U");
	}
	else
	{
		console::info(" ");
	}

	if (dvar->flags & game::native::DVAR_ROM)
	{
		console::info("R");
	}
	else
	{
		console::info(" ");
	}

	if (dvar->flags & game::native::DVAR_INIT)
	{
		console::info("I");
	}
	else
	{
		console::info(" ");
	}

	if (dvar->flags & game::native::DVAR_ARCHIVE)
	{
		console::info("A");
	}
	else
	{
		console::info(" ");
	}

	if (dvar->flags & game::native::DVAR_LATCH)
	{
		console::info("L");
	}
	else
	{
		console::info(" ");
	}

	if (dvar->flags & game::native::DVAR_CHEAT)
	{
		console::info("C");
	}
	else
	{
		console::info(" ");
	}

	console::info(" %s \"%s\"\n", dvar->name, game::native::Dvar_DisplayableValue(dvar));
}

void dvar::com_dvar_dump_single(const game::native::dvar_t* dvar, void* user_data)
{
	char message[2048];

	auto* dumpInfo = static_cast<game::native::DvarDumpInfo*>(user_data);

	++dumpInfo->count;
	if (dumpInfo->match && !game::native::Com_Filter(dumpInfo->match, dvar->name, 0))
	{
		return;
	}

	if (has_latched_value(dvar))
	{
		sprintf_s(message, "      %s \"%s\" -- latched \"%s\"\n", dvar->name, game::native::Dvar_DisplayableValue(dvar), game::native::Dvar_DisplayableLatchedValue(dvar));
	}
	else
	{
		sprintf_s(message, "      %s \"%s\"\n", dvar->name, game::native::Dvar_DisplayableValue(dvar));
	}

	console::info("%s", message);
}

void dvar::com_dvar_dump(int channel, const char* match)
{
	game::native::DvarDumpInfo dump_info;
	char summary[128];

	if (!log_file::com_logfile->current.integer)
	{
		return;
	}

	console::info("=============================== DVAR DUMP ========================================\n");
	dump_info.count = 0;
	dump_info.channel = channel;
	dump_info.match = match;
	game::native::Dvar_ForEach(com_dvar_dump_single, &dump_info);
	sprintf_s(summary, "\n%i total dvars\n%i dvar indexes\n", dump_info.count, *game::native::dvarCount);
	console::info("%s", summary);
	console::info("=============================== END DVAR DUMP =====================================\n");
}

void dvar::dump_f(const command::params& params)
{
	const char* match;
	if (params.size() > 1)
	{
		match = params.get(1);
	}
	else
	{
		match = nullptr;
	}

	com_dvar_dump(0, match);
}

void dvar::list_f(const command::params& params)
{
	const char* match;
	if (params.size() > 1)
	{
		match = params.get(1);
	}
	else
	{
		match = nullptr;
	}

	game::native::Dvar_ForEach(list_single, (void*)match);
	console::info("\n%i total dvars\n", *game::native::dvarCount);
}

void dvar::post_load()
{
	command::add("dvardump", dump_f);
	command::add("dvarlist", list_f);
}

REGISTER_MODULE(dvar)
